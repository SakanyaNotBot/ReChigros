#requires -Version 5.1
<#
    Static consistency check for the iOS packaging inputs.

    Generating and linking an Xcode project requires macOS, so this script
    validates everything that can be validated on any host: the presence and
    well-formedness of the bundle metadata, the asset catalog contract between
    Contents.json and the icon files, and the CMake wiring that consumes them.
    It never claims that a project was compiled.
#>
[CmdletBinding()]
param(
    [string]$Root = '',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if (-not $Root) { $Root = Split-Path -Parent $PSScriptRoot }
$IosDirectory = Join-Path $Root 'platform\ios'

$script:Failures = New-Object 'System.Collections.Generic.List[string]'
$script:Warnings = New-Object 'System.Collections.Generic.List[string]'
$script:Checks = 0

function Format-CheckLine {
    param([string]$Label, [string]$Name, [string]$Detail)
    if ($Detail) { return "  [$Label] $Name  $Detail" }
    return "  [$Label] $Name"
}

function Write-Check {
    param([string]$Name, [bool]$Passed, [string]$Detail = '')

    $script:Checks++
    if ($Passed) {
        if (-not $Quiet) { Write-Host (Format-CheckLine 'ok' $Name $Detail) -ForegroundColor Green }
    }
    else {
        Write-Host (Format-CheckLine 'FAIL' $Name $Detail) -ForegroundColor Red
        $script:Failures.Add($Name)
    }
}

function Write-WarningCheck {
    param([string]$Name, [string]$Detail = '')
    if (-not $Quiet) { Write-Host (Format-CheckLine 'warn' $Name $Detail) -ForegroundColor Yellow }
    $script:Warnings.Add($Name)
}

# Reads the pixel width of a PNG straight from the IHDR chunk.
function Get-PngDimension {
    param([string]$Path)

    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
    }
    catch { return -1 }

    if ($bytes.Length -lt 24) { return -1 }
    if (-not ($bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50 -and $bytes[2] -eq 0x4E -and $bytes[3] -eq 0x47)) { return -1 }

    # IHDR width occupies bytes 16..19 in network byte order. The operands are
    # widened first because shifting a byte truncates to byte width.
    $w = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    return $w
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { return $null }
}

Write-Host 'iOS packaging consistency check' -ForegroundColor Cyan
Write-Host "  root: $Root"
Write-Host ''

# ---------------------------------------------------------------------------
# Bundle metadata
# ---------------------------------------------------------------------------

Write-Host 'Bundle metadata' -ForegroundColor Cyan

$plistPath = Join-Path $IosDirectory 'Info.plist'
Write-Check 'Info.plist exists' (Test-Path -LiteralPath $plistPath) $plistPath

if (Test-Path -LiteralPath $plistPath) {
    $plistText = Get-Content -LiteralPath $plistPath -Raw
    $plist = $null
    try { $plist = [xml]$plistText } catch { }

    Write-Check 'Info.plist is well-formed XML' ($null -ne $plist)

    $requiredKeys = @(
        'CFBundleDisplayName',
        'CFBundleIdentifier',
        'CFBundleExecutable',
        'CFBundleName',
        'CFBundlePackageType',
        'CFBundleShortVersionString',
        'CFBundleVersion',
        'LSRequiresIPhoneOS',
        'UILaunchStoryboardName',
        'UISupportedInterfaceOrientations'
    )
    foreach ($key in $requiredKeys) {
        Write-Check "Info.plist contains $key" ($plistText -match "<key>$([regex]::Escape($key))</key>")
    }

    $launchKey = [regex]::Match($plistText, '<key>UILaunchStoryboardName</key>\s*<string>([^<]+)</string>')
    if ($launchKey.Success) {
        $storyboard = Join-Path $IosDirectory "$($launchKey.Groups[1].Value).storyboard"
        Write-Check 'UILaunchStoryboardName resolves to a storyboard' (Test-Path -LiteralPath $storyboard) $storyboard
    }

    if ($plistText -match '<key>UISupportedInterfaceOrientations</key>') {
        $landscape = ($plistText -match 'UIInterfaceOrientationLandscapeLeft') -and
                     ($plistText -match 'UIInterfaceOrientationLandscapeRight')
        Write-Check 'Info.plist supports landscape' $landscape
    }
}

$storyboardPath = Join-Path $IosDirectory 'LaunchScreen.storyboard'
Write-Check 'LaunchScreen.storyboard exists' (Test-Path -LiteralPath $storyboardPath) $storyboardPath
if (Test-Path -LiteralPath $storyboardPath) {
    $storyboard = $null
    try { $storyboard = [xml](Get-Content -LiteralPath $storyboardPath -Raw) } catch { }
    Write-Check 'LaunchScreen.storyboard is well-formed XML' ($null -ne $storyboard)
    if ($storyboard) {
        $isLaunch = $null -ne $storyboard.SelectSingleNode("//*[@launchScreen='YES']")
        Write-Check 'LaunchScreen.storyboard is marked as a launch screen' $isLaunch
    }
}

# ---------------------------------------------------------------------------
# Asset catalog
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'Asset catalog' -ForegroundColor Cyan

$catalogDirectory = Join-Path $IosDirectory 'Assets.xcassets'
$catalogContents = Join-Path $catalogDirectory 'Contents.json'
Write-Check 'Assets.xcassets exists' (Test-Path -LiteralPath $catalogDirectory) $catalogDirectory
Write-Check 'Assets.xcassets/Contents.json is valid JSON' ($null -ne (Read-JsonFile -Path $catalogContents))

$appIconDirectory = Join-Path $catalogDirectory 'AppIcon.appiconset'
$appIconContents = Join-Path $appIconDirectory 'Contents.json'
$appIconJson = Read-JsonFile -Path $appIconContents
Write-Check 'AppIcon.appiconset/Contents.json is valid JSON' ($null -ne $appIconJson)

if ($appIconJson) {
    $entries = @($appIconJson.images)
    Write-Check 'AppIcon.appiconset declares images' ($entries.Count -gt 0) "($($entries.Count) entries)"

    $hasMarketing = @($entries | Where-Object { $_.idiom -eq 'ios-marketing' }).Count -gt 0
    Write-Check 'AppIcon.appiconset declares a marketing icon' $hasMarketing

    foreach ($entry in $entries) {
        if (-not $entry.filename) { continue }
        $iconPath = Join-Path $appIconDirectory $entry.filename
        if (-not (Test-Path -LiteralPath $iconPath)) {
            Write-Check "icon file present: $($entry.filename)" $false
            continue
        }

        # 'size' is in points and 'scale' is a multiplier such as 2x. The two
        # matches are captured separately because $Matches is overwritten by
        # every subsequent -match.
        $expected = $null
        $sizeMatch = [regex]::Match([string]$entry.size, '^([0-9.]+)x([0-9.]+)$')
        $scaleMatch = [regex]::Match([string]$entry.scale, '^([0-9.]+)x$')
        if ($sizeMatch.Success -and $scaleMatch.Success) {
            $points = [double]$sizeMatch.Groups[1].Value
            $multiplier = [double]$scaleMatch.Groups[1].Value
            $expected = [int][math]::Round($points * $multiplier)
        }
        if ($null -ne $expected) {
            $actual = Get-PngDimension -Path $iconPath
            if ($actual -gt 0) {
                Write-Check "icon size matches spec: $($entry.filename)" ($actual -eq $expected) "(expected ${expected}px, found ${actual}px)"
            }
            else {
                Write-WarningCheck "icon size could not be read: $($entry.filename)"
            }
        }
        else {
            Write-Check "icon file present: $($entry.filename)" $true
        }
    }
}

# ---------------------------------------------------------------------------
# CMake wiring
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'CMake wiring' -ForegroundColor Cyan

$appCMake = Join-Path $Root 'src\app\CMakeLists.txt'
Write-Check 'src/app/CMakeLists.txt exists' (Test-Path -LiteralPath $appCMake) $appCMake

if (Test-Path -LiteralPath $appCMake) {
    $cmakeText = Get-Content -LiteralPath $appCMake -Raw

    Write-Check 'src/app/CMakeLists.txt handles IOS' ($cmakeText -match '\bIOS\b')
    Write-Check 'Bundle metadata is wired to the target' ($cmakeText -match 'MACOSX_BUNDLE_INFO_PLIST')
    Write-Check 'Asset catalog name is wired to the target' ($cmakeText -match 'ASSETCATALOG_COMPILER_APPICON_NAME')

    $referenced = @([regex]::Matches($cmakeText, 'platform/ios/([A-Za-z0-9_.]+)') | ForEach-Object { $_.Groups[1].Value })
    foreach ($item in ($referenced | Sort-Object -Unique)) {
        Write-Check "referenced from CMake: $item" (Test-Path -LiteralPath (Join-Path $IosDirectory $item))
    }
}

$rootCMake = Join-Path $Root 'CMakeLists.txt'
$buildScript = Join-Path $Root 'scripts\build.ps1'
if ((Test-Path -LiteralPath $buildScript)) {
    $scriptText = Get-Content -LiteralPath $buildScript -Raw
    Write-Check 'build.ps1 has an iOS branch' ($scriptText -match "'-G',\s*'Xcode'")
    Write-Check 'build.ps1 passes CMAKE_SYSTEM_NAME=iOS' ($scriptText -match 'CMAKE_SYSTEM_NAME=iOS')
    Write-Check 'build.ps1 guards the iOS build to macOS' ($scriptText -match 'requires macOS')
}

# ---------------------------------------------------------------------------
# Identifier consistency
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'Identifier consistency' -ForegroundColor Cyan

# The bundle identifier is declared in three places that must agree, otherwise
# the signed bundle and the metadata disagree at install time.
$plistIdentifier = $null
if (Test-Path -LiteralPath $plistPath) {
    $match = [regex]::Match((Get-Content -LiteralPath $plistPath -Raw),
        '<key>CFBundleIdentifier</key>\s*<string>([^<]+)</string>')
    if ($match.Success) { $plistIdentifier = $match.Groups[1].Value.Trim() }
}
Write-Check 'Info.plist declares a bundle identifier' ([bool]$plistIdentifier) $plistIdentifier

$cmakeBundleIdentifier = $null
if (Test-Path -LiteralPath $appCMake) {
    $match = [regex]::Match((Get-Content -LiteralPath $appCMake -Raw),
        'XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER\s+"?([A-Za-z0-9._-]+)"?')
    if ($match.Success) { $cmakeBundleIdentifier = $match.Groups[1].Value.Trim() }
}
Write-Check 'CMake sets XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER' ([bool]$cmakeBundleIdentifier) $cmakeBundleIdentifier

if ($plistIdentifier -and $cmakeBundleIdentifier) {
    Write-Check 'Bundle identifier agrees between Info.plist and CMake' `
        ($plistIdentifier -eq $cmakeBundleIdentifier) `
        "plist=$plistIdentifier cmake=$cmakeBundleIdentifier"
}

# The Android application id should stay aligned with the Apple bundle id so the
# two packages are recognisably the same product.
$androidGradle = Join-Path $Root 'platform\android\app\build.gradle'
if (Test-Path -LiteralPath $androidGradle) {
    $match = [regex]::Match((Get-Content -LiteralPath $androidGradle -Raw),
        "applicationId\s+'([A-Za-z0-9._-]+)'")
    if ($match.Success) {
        $androidIdentifier = $match.Groups[1].Value.Trim()
        if ($plistIdentifier) {
            Write-Check 'Apple and Android identifiers agree' ($androidIdentifier -eq $plistIdentifier) `
                "android=$androidIdentifier apple=$plistIdentifier"
        }
    }
}

# The launch storyboard and the asset catalog are both copied into the bundle as
# resources; xcassets must not be listed as a plain resource file.
if (Test-Path -LiteralPath $appCMake) {
    $cmakeText = Get-Content -LiteralPath $appCMake -Raw
    $hasCatalogResource = $cmakeText -match 'Assets\.xcassets"\s+PROPERTIES\s+MACOSX_PACKAGE_LOCATION'
    Write-Check 'Asset catalog is copied into the bundle resources' $hasCatalogResource
    $hasStoryboardResource = $cmakeText -match 'LaunchScreen\.storyboard"\s+PROPERTIES\s+MACOSX_PACKAGE_LOCATION'
    Write-Check 'Launch storyboard is copied into the bundle resources' $hasStoryboardResource
}

# ---------------------------------------------------------------------------
# Host expectations
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'Unverifiable on this host' -ForegroundColor Cyan
Write-Host '  - Xcode project generation (requires macOS with Xcode)' -ForegroundColor DarkGray
Write-Host '  - Compiling and linking the application bundle' -ForegroundColor DarkGray
Write-Host '  - actool compilation of the asset catalog' -ForegroundColor DarkGray
Write-Host '  - Code signing and provisioning' -ForegroundColor DarkGray

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

Write-Host ''
if ($script:Failures.Count -eq 0) {
    Write-Host ("iOS packaging check passed: {0} checks, {1} warnings." -f $script:Checks, $script:Warnings.Count) -ForegroundColor Green
    exit 0
}

Write-Host ("iOS packaging check failed: {0} of {1} checks failed." -f $script:Failures.Count, $script:Checks) -ForegroundColor Red
foreach ($failure in $script:Failures) { Write-Host "  - $failure" -ForegroundColor Red }
exit 1
