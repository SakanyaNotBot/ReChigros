#requires -Version 5.1
<#
    Unified build entry point for every supported target.

    Targets
      desktop    Host-native SDL application (Ninja + MSVC on Windows).
      core       Cross-compiled static core library, no application or tests.
      android    Gradle application package.
      ios        Xcode project generation (macOS only).
      verify-ios Static consistency check of the iOS packaging inputs.
verify-render Render the fixture matrix and check every renderer input reaches the picture.
verify-app   Drive the real application loop headlessly and check its states.
      all        Every target that the current host can produce.
      clean      Delete build/.

    The script discovers its toolchain instead of relying on a prepared shell,
    so it can be started from any ordinary command prompt.
#>
[CmdletBinding()]
param(
    [ValidateSet('desktop', 'core', 'android', 'ios', 'verify-ios', 'verify-render', 'verify-app', 'all', 'clean')]
    [string]$Target = 'desktop',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    # Skip the test stage of the desktop build.
    [switch]$SkipTests,

    # Parallel compile job count. 0 keeps the generator default.
    [int]$Jobs = 0,

    # Proxy used for dependency downloads and for the Gradle daemon.
    [string]$Proxy = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

# Native tools emit console-codepage text; decode it accordingly so that
# localized compiler diagnostics stay readable.
try {
    $oem = [System.Text.Encoding]::GetEncoding([System.Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
    [Console]::OutputEncoding = $oem
}
catch {
    # Encoding is cosmetic; never fail the build over it.
}

$Root = Split-Path -Parent $PSScriptRoot
$ScriptDir = $PSScriptRoot

$script:SdkRoot = ''
$script:ProxyUrl = ''

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Write-Failure {
    param([string]$Message, [string]$LogPath, $Tail)
    Write-Host ''
    Write-Host "!! $Message" -ForegroundColor Red
    if ($Tail -and $Tail.Count -gt 0) {
        Write-Host '--- last output lines ---' -ForegroundColor Yellow
        foreach ($line in $Tail) { Write-Host "  $line" -ForegroundColor Yellow }
    }
    if ($LogPath -and (Test-Path -LiteralPath $LogPath)) {
        Write-Host "--- full log: $LogPath ---" -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# Tool discovery
# ---------------------------------------------------------------------------

function Get-AndroidSdkRoot {
    foreach ($candidate in @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, 'D:\Studio-SDK')) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    $localProperties = Join-Path $Root 'platform\android\local.properties'
    if (Test-Path -LiteralPath $localProperties) {
        $match = Select-String -LiteralPath $localProperties -Pattern '^sdk\.dir=(.+)$' | Select-Object -First 1
        if ($match) {
            $path = $match.Matches[0].Groups[1].Value.Trim().Replace('/', '\')
            if (Test-Path -LiteralPath $path) { return $path }
        }
    }
    return ''
}

function Get-ToolCandidates {
    param([string]$Name, [string[]]$Suffixes)

    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $results = New-Object 'System.Collections.Generic.List[string]'

    foreach ($suffix in $Suffixes) {
        $path = if ($script:SdkRoot) { Join-Path $script:SdkRoot $suffix } else { '' }
        if ($path -and (Test-Path -LiteralPath $path) -and $seen.Add($path)) { $results.Add($path) }
    }

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $seen.Add($command.Source)) { $results.Add($command.Source) }

    return $results
}

function Find-CMake {
    if ($env:RECHIGROS_CMAKE -and (Test-Path -LiteralPath $env:RECHIGROS_CMAKE)) { return $env:RECHIGROS_CMAKE }
    $candidates = @(Get-ToolCandidates -Name 'cmake' -Suffixes @('cmake\3.22.1\bin\cmake.exe'))
    if ($candidates.Count -eq 0) { throw 'CMake 3.20 or newer was not found. Set RECHIGROS_CMAKE to its full path.' }
    return $candidates[0]
}

function Find-Ninja {
    if ($env:RECHIGROS_NINJA -and (Test-Path -LiteralPath $env:RECHIGROS_NINJA)) { return $env:RECHIGROS_NINJA }
    $candidates = @(Get-ToolCandidates -Name 'ninja' -Suffixes @('cmake\3.22.1\bin\ninja.exe'))
    if ($candidates.Count -eq 0) { return '' }
    return $candidates[0]
}

function Find-CTest {
    param([string]$CMakePath)
    $sibling = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
    if (Test-Path -LiteralPath $sibling) { return $sibling }
    $command = Get-Command 'ctest' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return ''
}

# ---------------------------------------------------------------------------
# MSVC environment
# ---------------------------------------------------------------------------

function Find-VcVars {
    if ($env:RECHIGROS_VCVARS -and (Test-Path -LiteralPath $env:RECHIGROS_VCVARS)) { return $env:RECHIGROS_VCVARS }

    $relative = 'VC\Auxiliary\Build\vcvars64.bat'
    $probeRoots = @(${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ }

    # A registered instance is authoritative when the installer database exists.
    foreach ($root in $probeRoots) {
        $vswhere = Join-Path $root 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { continue }
        $instances = & $vswhere -latest -prerelease -products '*' -property installationPath 2>$null
        foreach ($instance in @($instances)) {
            if (-not $instance) { continue }
            $candidate = Join-Path $instance $relative
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    # Conventional layout, newest toolkit first.
    $editions = @('BuildTools', 'Community', 'Professional', 'Enterprise', 'Preview')
    $versions = @('2022', '2019', '2017')
    foreach ($root in $probeRoots) {
        foreach ($version in $versions) {
            foreach ($edition in $editions) {
                $candidate = Join-Path $root "Microsoft Visual Studio\$version\$edition\$relative"
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }
    }

    # Last resort: search the installation trees.
    foreach ($root in $probeRoots) {
        $tree = Join-Path $root 'Microsoft Visual Studio'
        if (-not (Test-Path -LiteralPath $tree)) { continue }
        $match = Get-ChildItem -LiteralPath $tree -Filter 'vcvars64.bat' -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\VC\Auxiliary\Build\vcvars64.bat' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($match) { return $match.FullName }
    }

    return ''
}

function Import-VcVars {
    param([string]$VcVarsPath)

    $scriptFile = Join-Path $env:TEMP ('rechigros-vcvars-{0}.bat' -f [Guid]::NewGuid().ToString('N'))
    $lines = @(
        '@echo off',
        "call `"$VcVarsPath`" >nul 2>nul",
        'if errorlevel 1 exit /b 1',
        'set'
    )
    try {
        Set-Content -LiteralPath $scriptFile -Value $lines -Encoding Default
        $dump = & cmd.exe /c "`"$scriptFile`"" 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Failed to initialise the C++ environment from $VcVarsPath" }
    }
    finally {
        Remove-Item -LiteralPath $scriptFile -Force -ErrorAction SilentlyContinue
    }

    $applied = 0
    foreach ($line in $dump) {
        $text = [string]$line
        $separator = $text.IndexOf('=')
        if ($separator -le 0) { continue }
        $name = $text.Substring(0, $separator)
        $value = $text.Substring($separator + 1)
        if ($name.StartsWith('=')) { continue }
        [System.Environment]::SetEnvironmentVariable($name, $value)
        $applied++
    }
    if ($applied -eq 0) { throw "The C++ environment at $VcVarsPath produced no variables." }
}

function Initialize-HostToolchain {
    if (-not $IsWindowsHost) { return }

    $vcvars = Find-VcVars
    if (-not $vcvars) {
        throw 'No Visual C++ build environment was found. Install the C++ build tools or set RECHIGROS_VCVARS to vcvars64.bat.'
    }
    Write-Info "C++ environment: $vcvars"
    Import-VcVars -VcVarsPath $vcvars

    $cl = Get-Command 'cl' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cl) { throw 'The C++ environment was initialised but cl.exe is still unavailable.' }
    Write-Info "Compiler:        $($cl.Source)"
}

# ---------------------------------------------------------------------------
# Command execution
# ---------------------------------------------------------------------------

function Invoke-Tool {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][string]$Activity
    )

    $logDirectory = Split-Path -Parent $LogPath
    if ($logDirectory) { New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null }

    Write-Host "    $Activity" -ForegroundColor Gray
    Write-Host "    > `"$FilePath`" $($ArgumentList -join ' ')" -ForegroundColor DarkGray

    $tail = New-Object 'System.Collections.Generic.Queue[string]'
    $writer = New-Object System.IO.StreamWriter($LogPath, $false, (New-Object System.Text.UTF8Encoding($false)))
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object {
            $line = [string]$_
            $writer.WriteLine($line)
            Write-Host "      $line"
            $tail.Enqueue($line)
            while ($tail.Count -gt 80) { [void]$tail.Dequeue() }
        }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $writer.Flush()
        $writer.Dispose()
    }

    return [pscustomobject]@{ ExitCode = $exitCode; Tail = @($tail); LogPath = $LogPath }
}

function Invoke-ToolChecked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][string]$Activity
    )

    $result = Invoke-Tool -FilePath $FilePath -ArgumentList $ArgumentList -LogPath $LogPath -Activity $Activity
    if ($result.ExitCode -ne 0) {
        Write-Failure -Message "$Activity failed with exit code $($result.ExitCode)." -LogPath $result.LogPath -Tail $result.Tail
        throw "$Activity failed with exit code $($result.ExitCode)."
    }
    return $result
}

# ---------------------------------------------------------------------------
# Shared build helpers
# ---------------------------------------------------------------------------

function Resolve-Proxy {
    $value = if ($Proxy) { $Proxy }
             elseif ($env:RECHIGROS_PROXY) { $env:RECHIGROS_PROXY }
             elseif ($env:HTTPS_PROXY) { $env:HTTPS_PROXY }
             elseif ($env:HTTP_PROXY) { $env:HTTP_PROXY }
             else { 'http://127.0.0.1:7899' }
    return $value.TrimEnd('/')
}

function Get-ProxyEndpoint {
    $url = $script:ProxyUrl
    if (-not $url) { return $null }
    try {
        $uri = [Uri]$url
    }
    catch {
        return $null
    }
    return [pscustomobject]@{ Host = $uri.Host; Port = $uri.Port }
}

function Get-BuildDirectory {
    param([string]$Name)
    return Join-Path (Join-Path $Root 'build') $Name
}

function Get-LogPath {
    param([string]$Directory, [string]$Name)
    return Join-Path (Join-Path $Directory 'logs') "$Name.log"
}

# A generator change inside an existing tree is irrecoverable for CMake.
function Reset-ForeignGenerator {
    param([string]$Directory, [string]$Generator)

    $cache = Join-Path $Directory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { return }

    $match = Select-String -LiteralPath $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
    if (-not $match) { return }

    $existing = $match.Matches[0].Groups[1].Value.Trim()
    if ($existing -eq $Generator) { return }

    Write-Info "Discarding $Directory (generator '$existing' is not '$Generator')."
    Remove-Item -LiteralPath $Directory -Recurse -Force
}

function Get-StandardConfigureArguments {
    param([string]$Directory)

    $arguments = @('-S', $Root, '-B', $Directory, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration")
    if ($script:Ninja) { $arguments += "-DCMAKE_MAKE_PROGRAM=$($script:Ninja)" }
    $sdl = Join-Path $Root 'third_party\SDL'
    if (Test-Path -LiteralPath (Join-Path $sdl 'CMakeLists.txt')) {
        $arguments += "-DRECHIGROS_SDL_SOURCE=$($sdl.Replace('\', '/'))"
    }
    return $arguments
}

function Ensure-Sdl {
    $path = Join-Path $Root 'third_party\SDL'
    if (Test-Path -LiteralPath (Join-Path $path 'CMakeLists.txt')) { return $path }

    Write-Step 'Fetching SDL2 source'
    $proxyUrl = $script:ProxyUrl
    if ($proxyUrl) {
        Write-Info "Proxy: $proxyUrl"
        $env:HTTPS_PROXY = $proxyUrl
        $env:HTTP_PROXY = $proxyUrl
    }

    $thirdParty = Split-Path -Parent $path
    $archive = Join-Path $thirdParty 'SDL2-2.30.11.tar.gz'
    $expanded = Join-Path $thirdParty 'SDL2-2.30.11'
    New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null

    $download = @{
        Uri = 'https://www.libsdl.org/release/SDL2-2.30.11.tar.gz'
        OutFile = $archive
        UseBasicParsing = $true
    }
    if ($proxyUrl) { $download.Proxy = $proxyUrl }
    Invoke-WebRequest @download

    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($hash -ne '8B8D4AEF2038533DA814965220F88F77D60DFA0F32685F80EAD65E501337DA7F') {
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
        throw 'The SDL2 archive checksum does not match the expected value.'
    }

    & tar -xzf $archive -C $thirdParty '--exclude=SDL2-2.30.11/android-project-ant'
    if ($LASTEXITCODE -ne 0) { throw 'Failed to extract the SDL2 archive.' }
    if (-not (Test-Path -LiteralPath (Join-Path $expanded 'CMakeLists.txt'))) { throw 'The SDL2 archive layout is unexpected.' }

    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    Move-Item -LiteralPath $expanded -Destination $path
    return $path
}

function Get-ParallelArgument {
    param([string]$Flag)
    if ($Jobs -gt 0) { return @($Flag, "$Jobs") }
    return @($Flag)
}

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

function Build-Desktop {
    Write-Step "Desktop $Configuration"

    $directory = Get-BuildDirectory "desktop-$($Configuration.ToLowerInvariant())"
    Reset-ForeignGenerator -Directory $directory -Generator 'Ninja'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $configure = Get-StandardConfigureArguments -Directory $directory
    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $configure `
        -LogPath (Get-LogPath $directory 'configure') -Activity 'Configure' | Out-Null

    $build = @('--build', $directory) + (Get-ParallelArgument '--parallel')
    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $build `
        -LogPath (Get-LogPath $directory 'build') -Activity 'Compile' | Out-Null

    $application = Join-Path $directory 'src\app\ReChigros.exe'
    if (-not $IsWindowsHost) { $application = Join-Path $directory 'src\app\ReChigros' }
    if (-not (Test-Path -LiteralPath $application)) {
        throw "The build finished but the application binary is missing: $application"
    }
    Write-Info "Application: $application"

    if ($SkipTests) {
        Write-Info 'Tests skipped.'
        return
    }
    if (-not (Test-Path -LiteralPath (Join-Path $directory 'CTestTestfile.cmake'))) {
        Write-Info 'No test definitions were generated; skipping ctest.'
        return
    }

    Write-Step "Tests $Configuration"
    $ctest = Find-CTest -CMakePath $script:CMake
    if (-not $ctest) { throw 'ctest was not found next to the configured CMake.' }
    Invoke-ToolChecked -FilePath $ctest `
        -ArgumentList @('--test-dir', $directory, '-C', $Configuration, '--output-on-failure') `
        -LogPath (Get-LogPath $directory 'ctest') -Activity 'ctest' | Out-Null
}

function Find-Ndk {
    if ($env:RECHIGROS_NDK -and (Test-Path -LiteralPath $env:RECHIGROS_NDK)) { return $env:RECHIGROS_NDK }
    if (-not $script:SdkRoot) { return '' }
    $ndkRoot = Join-Path $script:SdkRoot 'ndk'
    if (-not (Test-Path -LiteralPath $ndkRoot)) {
        $bundle = Join-Path $script:SdkRoot 'ndk-bundle'
        if (Test-Path -LiteralPath $bundle) { return $bundle }
        return ''
    }
    $versions = Get-ChildItem -LiteralPath $ndkRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'build\cmake\android.toolchain.cmake') } |
        Sort-Object { [version]($_.Name -replace '[^0-9.].*$', '') } -Descending
    if ($versions.Count -gt 0) { return $versions[0].FullName }
    return ''
}

function Build-Core {
    Write-Step "Core library $Configuration (Android cross-compile)"

    $ndk = Find-Ndk
    if (-not $ndk) { throw 'An Android NDK with its CMake toolchain was not found. Set RECHIGROS_NDK.' }
    Write-Info "NDK: $ndk"

    $directory = Get-BuildDirectory 'android-core'
    Reset-ForeignGenerator -Directory $directory -Generator 'Ninja'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $toolchain = Join-Path $ndk 'build\cmake\android.toolchain.cmake'
    $configure = @(
        '-S', $Root, '-B', $directory, '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DANDROID_ABI=x86_64',
        '-DANDROID_PLATFORM=android-24',
        '-DANDROID_STL=c++_shared',
        '-DRECHIGROS_BUILD_APP=OFF',
        '-DRECHIGROS_BUILD_TESTS=OFF'
    )
    if ($script:Ninja) { $configure += "-DCMAKE_MAKE_PROGRAM=$($script:Ninja)" }

    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $configure `
        -LogPath (Get-LogPath $directory 'configure') -Activity 'Configure' | Out-Null

    $build = @('--build', $directory, '--target', 'rechigros_core') + (Get-ParallelArgument '--parallel')
    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $build `
        -LogPath (Get-LogPath $directory 'build') -Activity 'Compile' | Out-Null
}

function Find-JavaHome {
    if ($env:RECHIGROS_JAVA_HOME -and (Test-Path -LiteralPath $env:RECHIGROS_JAVA_HOME)) { return $env:RECHIGROS_JAVA_HOME }
    if ($env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\java.exe'))) { return $env:JAVA_HOME }

    $candidates = @(
        'C:\Program Files\Android\Android Studio\jbr',
        'C:\Program Files\Java\jdk-17',
        'C:\Program Files\Java\jdk-21',
        'C:\Program Files\Eclipse Adoptium'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\java.exe')) { return $candidate }
    }
    $jdkRoot = 'C:\Program Files\Java'
    if (Test-Path -LiteralPath $jdkRoot) {
        $java = Get-ChildItem -LiteralPath $jdkRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\java.exe') } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($java) { return $java.FullName }
    }
    return ''
}

function Build-Android {
    Write-Step "Android $Configuration"

    Ensure-Sdl | Out-Null

    $projectDirectory = Join-Path $Root 'platform\android'
    $gradlew = Join-Path $projectDirectory 'gradlew.bat'
    if (-not (Test-Path -LiteralPath $gradlew)) { throw "The Gradle wrapper is missing: $gradlew" }

    if (-not $script:SdkRoot) { throw 'An Android SDK was not found. Set ANDROID_SDK_ROOT.' }
    $env:ANDROID_SDK_ROOT = $script:SdkRoot
    $env:ANDROID_HOME = $script:SdkRoot
    Write-Info "SDK: $script:SdkRoot"

    $localProperties = Join-Path $projectDirectory 'local.properties'
    if (-not (Test-Path -LiteralPath $localProperties)) {
        $escaped = $script:SdkRoot.Replace('\', '\\').Replace(':', '\:')
        Set-Content -LiteralPath $localProperties -Value "# Generated by scripts/build.ps1`nsdk.dir=$escaped" -Encoding ascii
        Write-Info "Wrote $localProperties"
    }

    $javaHome = Find-JavaHome
    if ($javaHome) {
        $env:JAVA_HOME = $javaHome
        Write-Info "JDK: $javaHome"
    }

    $arguments = @()
    $endpoint = Get-ProxyEndpoint
    if ($endpoint) {
        Write-Info "Proxy: $($endpoint.Host):$($endpoint.Port)"
        $arguments += "-Dhttp.proxyHost=$($endpoint.Host)"
        $arguments += "-Dhttp.proxyPort=$($endpoint.Port)"
        $arguments += "-Dhttps.proxyHost=$($endpoint.Host)"
        $arguments += "-Dhttps.proxyPort=$($endpoint.Port)"
    }
    $arguments += '-Dorg.gradle.jvmargs=-Xmx4g'
    $arguments += '--console=plain'
    $arguments += "--stacktrace"
    # The wrapper runs from wherever the caller launched the script, so the project
    # directory has to be named explicitly.
    $arguments += '-p'
    $arguments += $projectDirectory
    $arguments += ":app:assemble$Configuration"

    $logDirectory = Get-BuildDirectory 'android-logs'
    $result = Invoke-Tool -FilePath $gradlew -ArgumentList $arguments `
        -LogPath (Get-LogPath $logDirectory "gradle-$($Configuration.ToLowerInvariant())") -Activity 'Gradle'

    $apkDirectory = Join-Path $projectDirectory "app\build\outputs\apk\$($Configuration.ToLowerInvariant())"
    $apk = Get-ChildItem -LiteralPath $apkDirectory -Filter '*.apk' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($apk) { Write-Info "Package: $($apk.FullName) ($([math]::Round($apk.Length / 1MB, 1)) MB)" }

    if ($result.ExitCode -ne 0) {
        Write-Failure -Message "Gradle failed with exit code $($result.ExitCode)." -LogPath $result.LogPath -Tail $result.Tail
        throw "Android build failed with exit code $($result.ExitCode)."
    }
    if (-not $apk) { throw "Gradle reported success but no package was produced in $apkDirectory" }
}

function Build-Ios {
    Write-Step "iOS $Configuration"

    if (-not $IsMacOSHost) {
        throw 'Generating an Xcode project requires macOS. Run scripts/verify-ios-project.ps1 to check the packaging inputs on this host.'
    }

    $sdl = Ensure-Sdl
    $directory = Get-BuildDirectory 'ios'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $configure = @(
        '-S', $Root, '-B', $directory, '-G', 'Xcode',
        '-DCMAKE_SYSTEM_NAME=iOS',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0',
        '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO',
        '-DRECHIGROS_BUILD_TESTS=OFF',
        "-DRECHIGROS_SDL_SOURCE=$($sdl.Replace('\', '/'))"
    )
    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $configure `
        -LogPath (Get-LogPath $directory 'configure') -Activity 'Configure' | Out-Null

    $build = @('--build', $directory, '--config', $Configuration) + (Get-ParallelArgument '--parallel')
    Invoke-ToolChecked -FilePath $script:CMake -ArgumentList $build `
        -LogPath (Get-LogPath $directory 'build') -Activity 'Compile' | Out-Null
}

function Invoke-VerifyIos {
    Write-Step 'iOS packaging consistency'
    $verifier = Join-Path $ScriptDir 'verify-ios-project.ps1'
    if (-not (Test-Path -LiteralPath $verifier)) { throw "The verifier is missing: $verifier" }
    $shell = if (Get-Command 'pwsh' -CommandType Application -ErrorAction SilentlyContinue) { 'pwsh' } else { 'powershell' }
    $result = Invoke-Tool -FilePath $shell `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $verifier, '-Root', $Root) `
        -LogPath (Get-LogPath (Get-BuildDirectory 'ios-verify') 'verify') -Activity 'verify-ios-project'
    if ($result.ExitCode -ne 0) {
        Write-Failure -Message "iOS packaging verification failed with exit code $($result.ExitCode)." -LogPath $result.LogPath -Tail $result.Tail
        throw 'iOS packaging verification failed.'
    }
}

function Invoke-Clean {
    Write-Step 'Cleaning build output'
    $directory = Join-Path $Root 'build'
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Recurse -Force
        Write-Info "Removed $directory"
    }
    $androidBuild = Join-Path $Root 'platform\android\app\build'
    if (Test-Path -LiteralPath $androidBuild) {
        Remove-Item -LiteralPath $androidBuild -Recurse -Force
        Write-Info "Removed $androidBuild"
    }
    $androidCxx = Join-Path $Root 'platform\android\app\.cxx'
    if (Test-Path -LiteralPath $androidCxx) {
        Remove-Item -LiteralPath $androidCxx -Recurse -Force
        Write-Info "Removed $androidCxx"
    }
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

$IsWindowsHost = ($env:OS -eq 'Windows_NT')
$IsMacOSHost = $false
if (Test-Path variable:IsMacOS) { $IsMacOSHost = [bool]$IsMacOS }
elseif (-not $IsWindowsHost) { $IsMacOSHost = ((& uname -s 2>$null) -eq 'Darwin') }

$script:SdkRoot = Get-AndroidSdkRoot
$script:ProxyUrl = Resolve-Proxy

if ($env:HTTPS_PROXY -eq $null) { $env:HTTPS_PROXY = $script:ProxyUrl }
if ($env:HTTP_PROXY -eq $null) { $env:HTTP_PROXY = $script:ProxyUrl }

Write-Host "ReChigros build: target=$Target configuration=$Configuration" -ForegroundColor Cyan

if ($Target -eq 'clean') {
    Invoke-Clean
    exit 0
}

if ($Target -eq 'verify-ios') {
    Invoke-VerifyIos
    exit 0
}

if ($Target -eq 'verify-app') {
    Initialize-HostToolchain
    $script:CMake = Find-CMake
    $script:Ninja = Find-Ninja
    Build-Desktop
    $directory = Get-BuildDirectory ("desktop-{0}" -f $Configuration.ToLowerInvariant())
    $app = Join-Path $directory 'src\app\ReChigros.exe'
    if (-not (Test-Path $app)) { throw "application binary not found: $app" }
    Write-Step 'Application loop'
    & (Join-Path $PSScriptRoot 'verify-app-loop.ps1') -App $app `
        -Chart 'tests\fixtures\plain.json' -Work (Join-Path $directory 'app-loop')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # Every character the interface can draw must have a glyph, otherwise it renders as an
    # empty box on a screen nobody looked at.
    Write-Step 'Interface font coverage'
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
      & $python.Source (Join-Path $PSScriptRoot 'check-font-coverage.py')
      exit $LASTEXITCODE
    }
    Write-Warning 'python is not available; the font coverage check was skipped'
    exit 0
}

if ($Target -eq 'verify-render') {
    # The desktop build provides the offscreen renderer the matrix drives.
    Initialize-HostToolchain
    $script:CMake = Find-CMake
    $script:Ninja = Find-Ninja
    Build-Desktop
    $directory = Get-BuildDirectory ("desktop-{0}" -f $Configuration.ToLowerInvariant())
    $smoke = Join-Path $directory 'src\render\rechigros_render_smoke.exe'
    if (-not (Test-Path $smoke)) { throw "render smoke binary not found: $smoke" }
    Write-Step 'Render matrix'
    & (Join-Path $PSScriptRoot 'verify-render-matrix.ps1') -Smoke $smoke -Assets 'assets' `
        -Work (Join-Path $directory 'render-matrix')
    exit $LASTEXITCODE
}

$needsDesktop = ($Target -eq 'desktop') -or ($Target -eq 'all')
$needsCore = ($Target -eq 'core')
$needsNative = $needsDesktop -or $needsCore -or ($Target -eq 'ios')

if ($needsNative) {
    Write-Step 'Toolchain'
    if ($script:SdkRoot) { Write-Info "Android SDK:     $script:SdkRoot" } else { Write-Info 'Android SDK:     not found' }

    $script:CMake = Find-CMake
    $script:Ninja = Find-Ninja
    Write-Info "CMake:           $script:CMake"
    Write-Info "Ninja:           $(if ($script:Ninja) { $script:Ninja } else { 'generator default' })"

    if ($needsDesktop -or $needsCore) { Initialize-HostToolchain }
    else { Ensure-Sdl | Out-Null }
}

if ($needsDesktop) { Build-Desktop }
if ($needsCore) { Build-Core }
if (($Target -eq 'android') -or ($Target -eq 'all')) { Build-Android }
if (($Target -eq 'ios') -or ($Target -eq 'all' -and $IsMacOSHost)) { Build-Ios }

Write-Host ''
Write-Host "ReChigros build finished: target=$Target configuration=$Configuration" -ForegroundColor Green
