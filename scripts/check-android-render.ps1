#requires -Version 5.1
<#
    Device check for the gameplay picture.

    On the device the play field is drawn with a different graphics stack than on the
    desktop, and a shader that only compiles for desktop GLSL makes every play-field draw
    a silent no-op: the frame is black where the notes, judge lines, background and HUD
    should be. The unit tests cannot see this, and the interface overlay still renders, so
    the failure survives a normal build check. This script closes that gap:

      1. installs the APK on the connected device,
      2. starts a chart directly on the game screen with a frame limit and a frame
         capture, using the argument passthrough of the launcher activity,
      3. takes a system screenshot while the chart runs,
      4. pulls the in-app frame capture and measures it (non-black coverage, colour
         variety, mean channel values),
      5. fails when a shader failed to build, when a render pass set a GL error, or when
         the captured frame is flat black.

    Requirements
      - adb on this machine (discovered from ANDROID_SDK_ROOT, D:\Studio-SDK or PATH),
      - one device or emulator connected,
      - a built APK (default: the release package of this repository).

    The chart directory is copied to the application's external files directory, which
    the application can read without a permission request.

    Usage
      powershell -ExecutionPolicy Bypass -File scripts/check-android-render.ps1
      powershell -ExecutionPolicy Bypass -File scripts/check-android-render.ps1 `
          -Serial 127.0.0.1:5555 -Apk platform\android\app\build\outputs\apk\release\app-release.apk `
          -MinUniqueColors 300

    Exit codes
      0  the play field rendered on the device
      1  the check ran and failed
      2  setup error (adb, device, APK or image tooling missing)
#>
[CmdletBinding()]
param(
    # Device serial as printed by "adb devices"; the first connected device is used when empty.
    [string]$Serial = '',
    # APK to install. The default is the release package this repository builds.
    [string]$Apk = '',
    [string]$Package = 'com.wuying.chigros',
    # Local directory holding the chart to play; its chart file is passed to the launcher.
    [string]$ChartDirectory = '',
    # Where the screenshot and the pulled frame capture are stored.
    [string]$OutDirectory = '',
    # Frame limit for the run; the capture waits for the opening to finish, so this must
    # outlast the intro. Emulators often present faster than 60 frames per second.
    [int]$Frames = 600,
    # Minimum number of distinct colours in the captured frame. A flat black frame has one;
    # a rendered play field has hundreds. This is a floor, not a quality measure.
    [int]$MinUniqueColors = 64,
    # Minimum fraction of sampled pixels whose channels exceed the dark threshold. A frame
    # holding only interface text over black stays far below this.
    [double]$MinBrightFraction = 0.05,
    [int]$TimeoutSeconds = 120,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Root = Split-Path -Parent $PSScriptRoot
if (-not $Apk) {
    $Apk = Join-Path $Root 'platform\android\app\build\outputs\apk\release\app-release.apk'
}
if (-not $ChartDirectory) { $ChartDirectory = Join-Path $Root 'assets\sample' }
if (-not $OutDirectory) { $OutDirectory = Join-Path $Root 'build\android-render-check' }

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Fail {
    param([string]$Message)
    Write-Host ''
    Write-Host "ANDROID-RENDER FAIL: $Message" -ForegroundColor Red
    exit 1
}

function Find-Adb {
    $command = Get-Command adb -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) { return $command.Source }
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($sdk in @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, 'D:\Studio-SDK')) {
        if (-not $sdk) { continue }
        $candidates.Add((Join-Path $sdk 'platform-tools\adb.exe'))
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return ''
}

function Invoke-Adb {
    # Runs one adb command and returns its output; throws when the command fails.
    param([string[]]$Arguments)
    $output = & $script:adb -s $script:serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($Arguments -join ' ') failed: $($output -join ' ')"
    }
    return $output
}

function Get-FrameStatistics {
    # Decodes a PNG through the platform drawing library and returns colour statistics
    # computed from a grid sample, which is enough to tell a rendered play field from a
    # flat clear colour.
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    } catch {
        return $null
    }
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        $rect = New-Object System.Drawing.Rectangle 0, 0, $width, $height
        $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = $data.Stride
        try {
            $bytes = New-Object byte[] ($stride * $height)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        } finally {
            $bitmap.UnlockBits($data)
        }
    } finally {
        $bitmap.Dispose()
    }

    $colors = New-Object 'System.Collections.Generic.HashSet[int]'
    [long]$sumRed = 0
    [long]$sumGreen = 0
    [long]$sumBlue = 0
    [long]$samples = 0
    [long]$bright = 0
    for ($row = 0; $row -lt $height; $row += 3) {
        $rowOffset = $row * $stride
        for ($column = 0; $column -lt $width; $column += 3) {
            $offset = $rowOffset + $column * 4
            $blue = $bytes[$offset]
            $green = $bytes[$offset + 1]
            $red = $bytes[$offset + 2]
            [void]$colors.Add((($red -shr 2) -shl 12) -bor (($green -shr 2) -shl 6) -bor ($blue -shr 2))
            $sumBlue += $blue
            $sumGreen += $green
            $sumRed += $red
            ++$samples
            if (($red + $green + $blue) -gt 24) { ++$bright }
        }
    }
    return [pscustomobject]@{
        Width = $width
        Height = $height
        UniqueColors = $colors.Count
        MeanRed = if ($samples -gt 0) { [int]($sumRed / $samples) } else { 0 }
        MeanGreen = if ($samples -gt 0) { [int]($sumGreen / $samples) } else { 0 }
        MeanBlue = if ($samples -gt 0) { [int]($sumBlue / $samples) } else { 0 }
        BrightFraction = if ($samples -gt 0) { [double]$bright / [double]$samples } else { 0.0 }
    }
}

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

$script:adb = Find-Adb
if (-not $script:adb) {
    Write-Host 'ANDROID-RENDER SKIP: adb not found; set ANDROID_SDK_ROOT or add platform-tools to PATH' -ForegroundColor Yellow
    exit 2
}
if (-not (Test-Path -LiteralPath $Apk -PathType Leaf)) {
    Write-Host "ANDROID-RENDER SKIP: no APK at $Apk (build the android target first)" -ForegroundColor Yellow
    exit 2
}
$chartFile = Get-ChildItem -LiteralPath $ChartDirectory -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in '.json', '.pec' } |
    Sort-Object Name |
    Select-Object -First 1
if (-not $chartFile) {
    Write-Host "ANDROID-RENDER SKIP: no chart file in $ChartDirectory" -ForegroundColor Yellow
    exit 2
}

$devices = & $script:adb devices 2>&1 | Where-Object { $_ -match '^\S+\s+device$' }
if ($LASTEXITCODE -ne 0 -or -not $devices) {
    Write-Host 'ANDROID-RENDER SKIP: no device or emulator connected' -ForegroundColor Yellow
    exit 2
}
if (-not $Serial) {
    $Serial = ($devices | Select-Object -First 1) -split '\s+' | Select-Object -First 1
}
$script:serial = $Serial
Write-Step "Device $($script:serial)"

New-Item -ItemType Directory -Force -Path $OutDirectory | Out-Null
$OutDirectory = (Resolve-Path -LiteralPath $OutDirectory).Path
$screenshotPath = Join-Path $OutDirectory 'device_screenshot.png'
$capturePath = Join-Path $OutDirectory 'device_frame_capture.png'
foreach ($stale in @($screenshotPath, $capturePath)) {
    if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force }
}

# ---------------------------------------------------------------------------
# Install, seed and stage the chart
# ---------------------------------------------------------------------------

if (-not $SkipInstall) {
    Write-Step 'Installing the package'
    $installOutput = & $script:adb -s $script:serial install -r $Apk 2>&1
    if ($LASTEXITCODE -ne 0 -or ($installOutput -join ' ') -notmatch 'Success') {
        Fail "install failed: $($installOutput -join ' ')"
    }
}

# The application writes its frame capture inside its own files directory. Reading that
# directory from the host needs root; the emulators this project targets provide it. A
# non-rooted device falls back to the application's external files directory.
$rooted = $false
$identity = (& $script:adb -s $script:serial shell id 2>&1) -join ' '
if ($identity -notmatch 'uid=0') {
    Write-Info 'Requesting root access so the capture directory can be read'
    & $script:adb -s $script:serial root 2>&1 | Out-Null
    Start-Sleep -Milliseconds 1500
    for ($attempt = 0; $attempt -lt 12; ++$attempt) {
        $identity = (& $script:adb -s $script:serial shell id 2>&1) -join ' '
        if ($identity -match 'uid=0') { break }
        Start-Sleep -Milliseconds 700
    }
}
$rooted = $identity -match 'uid=0'

if ($rooted) {
    $internalRoot = "/data/data/$Package/files"
    # The first run copies the bundled example out of the package into the application's
    # own storage, which gives the check a chart that needs no import step or permission.
    Write-Step 'Seeding the application storage with the bundled example'
    & $script:adb -s $script:serial shell am force-stop $Package 2>&1 | Out-Null
    & $script:adb -s $script:serial shell am start -n "$Package/.MainActivity" 2>&1 | Out-Null
    Start-Sleep -Seconds 4
    & $script:adb -s $script:serial shell am force-stop $Package 2>&1 | Out-Null
    $remoteChart = "$internalRoot/asset-cache/sample/$($chartFile.Name)"
    $listed = & $script:adb -s $script:serial shell ls $remoteChart 2>&1
    if ($LASTEXITCODE -ne 0 -or ($listed -join ' ') -match 'No such file') {
        # The bundled materialisation did not happen: copy the chart directory instead.
        Write-Info 'Bundled example not found; copying the chart directory'
        & $script:adb -s $script:serial shell mkdir -p "$internalRoot/check/sample" 2>&1 | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $ChartDirectory -File) {
            & $script:adb -s $script:serial push $file.FullName "$internalRoot/check/sample/" 2>&1 |
                Out-Null
        }
        $remoteChart = "$internalRoot/check/sample/$($chartFile.Name)"
    }
    $remoteCapture = "$internalRoot/check/frame.png"
    Write-Info "Chart: $remoteChart"
} else {
    $remoteRoot = "/sdcard/Android/data/$Package/files/check"
    $remoteChart = "$remoteRoot/$($chartFile.Name)"
    $remoteCapture = "$remoteRoot/frame.png"
    Write-Step "Staging the chart at $remoteRoot"
    & $script:adb -s $script:serial shell mkdir -p $remoteRoot 2>&1 | Out-Null
    $pushOutput = & $script:adb -s $script:serial push $ChartDirectory $remoteRoot 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ('ANDROID-RENDER SKIP: the device is not rooted and the application external ' +
            'files directory is not writable from the host; run the check on an emulator with ' +
            'root, or copy the chart to the device manually') -ForegroundColor Yellow
        Write-Info ($pushOutput -join ' ')
        exit 2
    }
    $remoteChart = "$remoteRoot/$(Split-Path -Leaf $ChartDirectory)/$($chartFile.Name)"
}

# ---------------------------------------------------------------------------
# Run the chart with a frame capture
# ---------------------------------------------------------------------------

Write-Step "Starting the chart ($Frames frames, capture requested)"
& $script:adb -s $script:serial logcat -c 2>&1 | Out-Null
& $script:adb -s $script:serial shell am force-stop $Package 2>&1 | Out-Null
$arguments = "--frames $Frames --autoplay --gl-debug --capture-frame $remoteCapture $remoteChart"
# A capture left over from an earlier run must not be mistaken for this run's output.
& $script:adb -s $script:serial shell rm -f $remoteCapture 2>&1 | Out-Null
# The launcher reads one space-separated argument string, so the quotes have to survive
# the shell: the whole command is handed to the device shell as a single string.
$launchCommand = "am start -n $Package/.MainActivity --es args `"$arguments`""
& $script:adb -s $script:serial shell $launchCommand 2>&1 | Out-Null

# Startup texture decoding can outlast a fixed delay. Wait for the application's first-frame
# report, then allow that frame to reach the compositor before taking the system screenshot.
$firstFrameDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
$firstFrameReported = $false
while ((Get-Date) -lt $firstFrameDeadline) {
    $startupLog = & $script:adb -s $script:serial logcat -d 2>&1
    if ($startupLog -match 'launch: total to first frame') {
        $firstFrameReported = $true
        break
    }
    Start-Sleep -Milliseconds 250
}
if (-not $firstFrameReported) {
    Fail "the application did not report its first frame within $TimeoutSeconds seconds"
}
Start-Sleep -Milliseconds 500
& $script:adb -s $script:serial shell screencap -p /sdcard/rechigros-check.png 2>&1 | Out-Null
& $script:adb -s $script:serial pull /sdcard/rechigros-check.png $screenshotPath 2>&1 | Out-Null
& $script:adb -s $script:serial shell rm -f /sdcard/rechigros-check.png 2>&1 | Out-Null

# Wait for the frame capture to appear; the run exits once the frame limit is reached.
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    $listed = & $script:adb -s $script:serial shell ls $remoteCapture 2>&1
    if ($LASTEXITCODE -eq 0 -and ($listed -join ' ') -notmatch 'No such file') { break }
    Start-Sleep -Milliseconds 1000
}
$pullOutput = & $script:adb -s $script:serial pull $remoteCapture $capturePath 2>&1
$logcat = & $script:adb -s $script:serial logcat -d 2>&1
& $script:adb -s $script:serial shell am force-stop $Package 2>&1 | Out-Null
if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
    Write-Info ("capture pull failed: " + ($pullOutput -join ' '))
}

# ---------------------------------------------------------------------------
# Verdict
# ---------------------------------------------------------------------------

$appLines = $logcat | Where-Object { $_ -match '\[gl\]|\[render\]|CAPTURE' }
Write-Step 'Diagnostics reported by the run'
$reported = $appLines | Where-Object { $_ -match '\[gl\]|\[render\]|CAPTURE' }
if ($reported) {
    $reported | Select-Object -First 24 | ForEach-Object { Write-Info $_.Trim() }
} else {
    Write-Info '(no application diagnostics were found in the log)'
}

$problems = New-Object System.Collections.Generic.List[string]
# The launcher activity is the only way to reach the game screen without tapping, so the
# diagnostic switch arriving in the log also proves the argument passthrough works.
if (-not ($appLines | Where-Object { $_ -match '\[gl\] version=' })) {
    $problems.Add('the run did not receive the launch arguments: no graphics driver report ' +
        'was logged (expected --gl-debug to reach the application)')
}
$initFailed = $appLines | Where-Object { $_ -match 'renderer initialization failed' }
if ($initFailed) {
    foreach ($line in $initFailed) { $problems.Add("renderer did not initialise: $($line.Trim())") }
} elseif (-not ($appLines | Where-Object { $_ -match 'renderer initialized' })) {
    $problems.Add('the scene renderer never reported initialisation; nothing can be drawn')
}
$shaderFailures = $appLines | Where-Object { $_ -match 'shader failed' }
if ($shaderFailures) {
    foreach ($line in $shaderFailures) { $problems.Add("shader did not build: $($line.Trim())") }
}
$glErrors = $appLines | Where-Object { $_ -match '\[gl\] stage=' }
if ($glErrors) {
    foreach ($line in $glErrors) { $problems.Add("render pass set a GL error: $($line.Trim())") }
}

$statistics = Get-FrameStatistics -Path $capturePath
if ($null -eq $statistics) {
    $problems.Add("no frame capture was written (expected $remoteCapture)")
} else {
    Write-Step 'Captured frame'
    Write-Info ("$($statistics.Width)x$($statistics.Height) uniqueColors=" +
        "$($statistics.UniqueColors) meanRGB=$($statistics.MeanRed)," +
        "$($statistics.MeanGreen),$($statistics.MeanBlue) brightFraction=" +
        $statistics.BrightFraction.ToString('P1'))
    if ($statistics.UniqueColors -lt $MinUniqueColors) {
        $problems.Add("the captured frame has only $($statistics.UniqueColors) distinct colours " +
            "(minimum $MinUniqueColors): the play field did not render")
    }
    if ($statistics.BrightFraction -lt $MinBrightFraction) {
        $problems.Add(("only {0:P1} of the sampled pixels are lit (minimum {1:P1}): the play field " +
            "is black") -f $statistics.BrightFraction, $MinBrightFraction)
    }
}

# The system screenshot proves what the compositor put on the display, not only what the
# application read back from its own framebuffer.
if (Test-Path -LiteralPath $screenshotPath -PathType Leaf) {
    $screenshotStatistics = Get-FrameStatistics -Path $screenshotPath
    if ($null -ne $screenshotStatistics) {
        Write-Step 'System screenshot'
        Write-Info ("$($screenshotStatistics.Width)x$($screenshotStatistics.Height) " +
            "uniqueColors=$($screenshotStatistics.UniqueColors) " +
            "meanRGB=$($screenshotStatistics.MeanRed),$($screenshotStatistics.MeanGreen)," +
            "$($screenshotStatistics.MeanBlue)")
        if ($screenshotStatistics.UniqueColors -lt 32) {
            $problems.Add('the system screenshot is nearly flat: the frame was not presented')
        }
    }
}

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host "ANDROID-RENDER FAIL ($($problems.Count) problem(s))" -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Yellow }
    Write-Host "  artifacts: $OutDirectory"
    exit 1
}

Write-Host ''
Write-Host ("ANDROID-RENDER PASS: play field rendered " +
    "(uniqueColors={0}, brightFraction={1:P1})" -f $statistics.UniqueColors,
    $statistics.BrightFraction) -ForegroundColor Green
Write-Host "  frame capture: $capturePath"
Write-Host "  screenshot:    $screenshotPath"
exit 0
