#requires -Version 5.1
<#
    Static shader-language check for the device build.

    The device build compiles every renderer shader as GLSL ES 3.00. A shader that is
    only valid desktop GLSL is accepted by the desktop build and then fails to compile on
    the device, where each draw that uses it becomes a silent no-op: the picture is
    missing while the logic and the interface keep working. That failure mode is easy to
    see on a device only after a build-install-run cycle, so this check catches it on the
    host: it normalises the shader sources exactly the way the renderer does, for the ES
    language, and compiles the result with a standalone GLSL compiler.

    Sources checked
      - inline raw-string shader bodies in src/render/**/*.cpp
      - every .glsl file under assets/prpr_shaders and assets/official_shaders

    Tools
      The normaliser probe is built from this repository's own ShaderProgram.cpp with the
      local MSVC toolchain. Validation uses glslangValidator, which ships with the Android
      emulator and with any Vulkan SDK. If either tool is missing the check prints a skip
      line and exits 0, so it is safe to call on a machine that only builds the app.

    Usage
      powershell -ExecutionPolicy Bypass -File scripts/check-gles-shaders.ps1
      powershell -ExecutionPolicy Bypass -File scripts/check-gles-shaders.ps1 `
          -GlslangValidator "C:\VulkanSDK\1.3.290.0\Bin\glslangValidator.exe"

    Exit codes
      0  every shader compiles for GLSL ES 3.00, or the check was skipped
      1  at least one shader failed to compile for GLSL ES 3.00
      2  setup error (probe did not build, no sources found)
#>
[CmdletBinding()]
param(
    # Repository root; the default is the parent of this script's directory.
    [string]$Root = '',
    # Scratch directory for extracted and normalised sources. Defaults to build\gles-shader-check.
    [string]$ScratchDirectory = '',
    # Explicit glslangValidator path; discovered automatically when left empty.
    [string]$GlslangValidator = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not $Root) { $Root = Split-Path -Parent $PSScriptRoot }
$Root = (Resolve-Path -LiteralPath $Root).Path
if (-not $ScratchDirectory) { $ScratchDirectory = Join-Path $Root 'build\gles-shader-check' }
$ScratchDirectory = [System.IO.Path]::GetFullPath($ScratchDirectory)

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Find-FirstExisting {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if (-not $candidate) { continue }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
        # Wildcard candidates cover versioned install roots.
        if ($candidate -match '[\*\?]') {
            $found = Get-ChildItem -Path $candidate -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    return ''
}

function Find-GlslangValidator {
    param([string]$Explicit)
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit -PathType Leaf)) {
            throw "glslangValidator not found at $Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    $command = Get-Command glslangValidator -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) { return $command.Source }
    $roots = @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, $env:VULKAN_SDK)
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($root in $roots) {
        if (-not $root) { continue }
        $candidates.Add((Join-Path $root 'emulator\lib64\vulkan\glslangValidator.exe'))
        $candidates.Add((Join-Path $root 'Bin\glslangValidator.exe'))
    }
    $candidates.Add('D:\Studio-SDK\emulator\lib64\vulkan\glslangValidator.exe')
    $candidates.Add('C:\VulkanSDK\*\Bin\glslangValidator.exe')
    $candidates.Add('C:\Program Files\VulkanSDK\*\Bin\glslangValidator.exe')
    return Find-FirstExisting -Candidates $candidates
}

function Find-VcVars {
    $candidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\*\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2019\*\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat'
    )
    return Find-FirstExisting -Candidates $candidates
}

function Get-RawStringBlocks {
    param([string]$Text)
    # C++ raw strings: R"( ... )", R"tag( ... )tag". Only the shader bodies matter here.
    $pattern = 'R"(?<delimiter>[A-Za-z0-9_]*)\((?<body>.*?)\)\k<delimiter>"'
    $results = New-Object System.Collections.Generic.List[string]
    foreach ($match in [regex]::Matches($Text, $pattern,
            [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $results.Add($match.Groups['body'].Value)
    }
    return $results
}

$validator = Find-GlslangValidator -Explicit $GlslangValidator
if (-not $validator) {
    Write-Host 'SHADER-CHECK SKIP: glslangValidator not found; install a Vulkan SDK or pass -GlslangValidator'
    exit 0
}
Write-Step "GLSL ES validator: $validator"

$vcvars = Find-VcVars
if (-not $vcvars) {
    Write-Host 'SHADER-CHECK SKIP: no Visual C++ environment found to build the normaliser probe'
    exit 0
}
$gladInclude = Join-Path $Root 'third_party\cache\glad\include'
$gladSource = Join-Path $Root 'third_party\cache\glad\src\glad.c'
if (-not (Test-Path -LiteralPath $gladInclude) -or -not (Test-Path -LiteralPath $gladSource)) {
    Write-Host 'SHADER-CHECK SKIP: the glad dependency is not downloaded yet (run a desktop build first)'
    exit 0
}

# ---------------------------------------------------------------------------
# Extract every shader body into the scratch tree and record a work list.
# ---------------------------------------------------------------------------

if (Test-Path -LiteralPath $ScratchDirectory) {
    Remove-Item -LiteralPath $ScratchDirectory -Recurse -Force
}
$rawDirectory = Join-Path $ScratchDirectory 'raw'
New-Item -ItemType Directory -Force -Path $rawDirectory | Out-Null

$work = New-Object System.Collections.Generic.List[object]
$renderSourceDirectory = Join-Path $Root 'src\render'
$inlineSources = Get-ChildItem -LiteralPath $renderSourceDirectory -Recurse -Filter '*.cpp' |
    Sort-Object FullName
$inlineCount = 0
foreach ($source in $inlineSources) {
    $text = Get-Content -Raw -LiteralPath $source.FullName
    if ($text -notmatch 'R"') { continue }
    foreach ($block in Get-RawStringBlocks -Text $text) {
        if ($block.Trim().Length -eq 0) { continue }
        # A body that writes gl_Position is a vertex stage; everything else is a fragment
        # stage, which is the stage whose language rules the device build kept failing on.
        $fragment = -not ($block -match 'gl_Position')
        $extension = if ($fragment) { 'frag' } else { 'vert' }
        $stem = '{0}_{1}' -f $source.BaseName, $inlineCount
        $input = Join-Path $rawDirectory ($stem + '.src')
        $output = Join-Path $rawDirectory ($stem + '.' + $extension)
        Set-Content -LiteralPath $input -Value $block -Encoding ascii
        $work.Add([pscustomobject]@{
            Stage = if ($fragment) { 'fragment' } else { 'vertex' }
            Input = $input
            Output = $output
            Name = $stem
            Origin = $source.FullName.Substring($Root.Length + 1)
        })
        ++$inlineCount
    }
}

foreach ($relativeDirectory in @('assets\prpr_shaders', 'assets\official_shaders')) {
    $directory = Join-Path $Root $relativeDirectory
    if (-not (Test-Path -LiteralPath $directory)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $directory -Filter '*.glsl' | Sort-Object Name) {
        $input = Join-Path $rawDirectory ($file.BaseName + '.glsl.src')
        $output = Join-Path $rawDirectory ($file.BaseName + '.frag')
        Copy-Item -LiteralPath $file.FullName -Destination $input
        $work.Add([pscustomobject]@{
            Stage = 'fragment'
            Input = $input
            Output = $output
            Name = $file.BaseName
            Origin = $file.FullName.Substring($Root.Length + 1)
        })
    }
}

if ($work.Count -eq 0) {
    Write-Host 'SHADER-CHECK FAIL: no shader sources were found'
    exit 2
}

# The sprite batch shader draws the whole play field, so its presence in the inventory is
# required: an extraction bug must not turn this check into a silent pass.
$spriteBatch = $work | Where-Object { $_.Name -like 'SpriteBatch_*' -and $_.Stage -eq 'fragment' }
if (-not $spriteBatch) {
    Write-Host 'SHADER-CHECK FAIL: the sprite batch fragment shader was not found in the render sources'
    exit 2
}

# ---------------------------------------------------------------------------
# Build the normaliser probe against the renderer's own shader preparation code.
# ---------------------------------------------------------------------------

Write-Step 'Building the shader normaliser probe'
$probeSource = Join-Path $ScratchDirectory 'shader_probe.cpp'
@'
#include "rechigros/render/gl/ShaderProgram.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readText(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream manifest(argv[1]);
  if (!manifest) return 2;
  std::string line;
  int written = 0;
  while (std::getline(manifest, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto first = line.find('\t');
    const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos) continue;
    const bool fragment = line.substr(0, first) == "fragment";
    const std::string input = line.substr(first + 1, second - first - 1);
    const std::string output = line.substr(second + 1);
    const std::string source = readText(input);
    if (source.empty()) {
      std::fprintf(stderr, "empty shader source: %s\n", input.c_str());
      continue;
    }
    const std::string prepared =
        rechigros::render::gl::prepareShaderSourceForLanguage(fragment, true, source);
    std::ofstream out(output, std::ios::binary);
    out << prepared;
    ++written;
  }
  std::fprintf(stdout, "normalised %d shader sources\n", written);
  return written == 0 ? 3 : 0;
}
'@ | Set-Content -LiteralPath $probeSource -Encoding ascii

$manifestPath = Join-Path $ScratchDirectory 'manifest.txt'
$manifestLines = $work | ForEach-Object { "$($_.Stage)`t$($_.Input)`t$($_.Output)" }
$manifestLines | Set-Content -LiteralPath $manifestPath -Encoding ascii

$probeExecutable = Join-Path $ScratchDirectory 'shader_probe.exe'
$includePath = Join-Path $Root 'include'
$shaderProgramSource = Join-Path $Root 'src\render\gl\ShaderProgram.cpp'
$compileCommand = 'call "{0}" >nul 2>&1 && cd /d "{1}" && cl /nologo /std:c++20 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /I "{2}" /I "{3}" "{4}" "{5}" "{6}" /Fe:"{7}"' -f `
    $vcvars, $ScratchDirectory, $includePath, $gladInclude, $probeSource, $shaderProgramSource,
    $gladSource, $probeExecutable
$compileOutput = & cmd.exe /c $compileCommand 2>&1
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $probeExecutable)) {
    Write-Host 'SHADER-CHECK FAIL: the normaliser probe did not build'
    $compileOutput | Select-Object -Last 10 | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
    exit 2
}

$probeOutput = & $probeExecutable $manifestPath 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host 'SHADER-CHECK FAIL: the normaliser probe did not produce output'
    $probeOutput | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
    exit 2
}

# ---------------------------------------------------------------------------
# Compile every normalised source for GLSL ES 3.00.
# ---------------------------------------------------------------------------

Write-Step "Compiling $($work.Count) shaders for GLSL ES 3.00"
$failures = New-Object System.Collections.Generic.List[object]
foreach ($item in $work) {
    if (-not (Test-Path -LiteralPath $item.Output)) {
        $failures.Add([pscustomobject]@{ Name = $item.Name; Log = 'normalised source missing' })
        continue
    }
    $log = & $validator $item.Output 2>&1
    $status = $LASTEXITCODE
    # The validator prints the file name on success too; only the exit status decides.
    if ($status -ne 0) {
        $failures.Add([pscustomobject]@{ Name = $item.Name; Log = ($log -join "`n") })
    }
}

if ($failures.Count -gt 0) {
    Write-Host ''
    Write-Host "SHADER-CHECK FAIL: $($failures.Count) of $($work.Count) shaders do not compile for GLSL ES 3.00" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host "  $($failure.Name)" -ForegroundColor Yellow
        ($failure.Log -split "`n" | Select-Object -First 8) | ForEach-Object { Write-Host "    $_" }
    }
    exit 1
}

Write-Host ''
Write-Host "SHADER-CHECK PASS ($($work.Count)/$($work.Count) shaders compile for GLSL ES 3.00)" -ForegroundColor Green
exit 0
