# Renders a scene twice with one difference and checks that the difference reaches the
# picture. This is the regression net for "the renderer silently ignores an input": every
# case pairs a base render with a changed one and requires a minimum share of pixels to
# change.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\verify-render-matrix.ps1 `
#          -Smoke build\probe\src\render\rechigros_render_smoke.exe -Assets assets `
#          -Work build\testdata\matrix

param(
  [Parameter(Mandatory = $true)][string]$Smoke,
  [Parameter(Mandatory = $true)][string]$Assets,
  [Parameter(Mandatory = $true)][string]$Work,
  [string]$Fixtures = 'tests\fixtures'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Work | Out-Null

function Invoke-Case($Case) {
  $id = $Case.Id
  $basePath = Join-Path $Work "$id-base.png"
  $changePath = Join-Path $Work "$id-change.png"

  $baseArgs = @('--chart', $Case.BaseChart, '--assets', $Assets, '--frames', '1', '--out', $basePath)
  foreach ($item in $Case.Base) { $baseArgs += $item }
  & $Smoke $baseArgs | Out-Null

  $changeArgs = @('--chart', $Case.ChangedChart, '--assets', $Assets, '--frames', '1',
    '--out', $changePath, '--compare', $basePath)
  foreach ($item in $Case.Changed) { $changeArgs += $item }
  $line = (& $Smoke $changeArgs 2>&1 | Select-String 'Compare') -join ''

  if (-not (Test-Path $basePath) -or -not (Test-Path $changePath)) {
    Write-Host "[FAIL] ${id}: a render produced no output"
    return 1
  }
  if ($line -notmatch '([0-9.]+)%') {
    Write-Host "[FAIL] ${id}: no comparison was produced"
    return 1
  }
  $percent = [double]$Matches[1]
  if ($percent -lt $Case.Minimum) {
    Write-Host ("[FAIL] {0}: only {1:N2}% of pixels changed (need {2:N2}%) - {3}" -f `
        $id, $percent, $Case.Minimum, $Case.Description)
    return 1
  }
  Write-Host ("[ok]   {0,-20} {1,6:N2}%  {2}" -f $id, $percent, $Case.Description)
  return 0
}

$plain = Join-Path $Fixtures 'plain.json'
$redzone = Join-Path $Fixtures 'redzone.json'
$redzoneShader = Join-Path $Fixtures 'redzone_shader_pack\chart.json'
$bloom = Join-Path $Fixtures 'bloom_pack\chart.json'
$vignette = Join-Path $Fixtures 'vignette_pack\chart.json'
$gif = Join-Path $Fixtures 'gif_pack\chart.json'
$singleNote = Join-Path $Fixtures 'single_note.json'
$hold = Join-Path $Fixtures 'hold.json'
$holdSkin = (Resolve-Path (Join-Path $Fixtures 'hold_skin')).Path
$skin = (Resolve-Path (Join-Path $Fixtures 'skin')).Path
$skinYml = (Resolve-Path (Join-Path $Fixtures 'skin_yml')).Path
$rotated = Join-Path $Fixtures 'rotated.json'
$rotatedLevel = Join-Path $Fixtures 'rotated_level.json'
$moreBets = Join-Path $Fixtures 'morebets.json'
$speedChange = Join-Path $Fixtures 'speed_change.json'
$speedConstant = Join-Path $Fixtures 'speed_constant.json'
$fade = Join-Path $Fixtures 'fade.json'
$fadeConstant = Join-Path $Fixtures 'fade_constant.json'
$sideAbove = Join-Path $Fixtures 'side_above.json'
$sideBelow = Join-Path $Fixtures 'side_below.json'

# Each case names the base and changed chart explicitly, so a case can compare two
# different packs as well as two settings on one chart.
$cases = @(
  @{ Id = 'input-dim'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0', '--dim', '0.8'); Minimum = 5.0;
     Description = 'background dim reaches the picture' }
  @{ Id = 'input-scroll'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0', '--scroll', '2.5'); Minimum = 0.2;
     Description = 'scroll speed moves the notes' }
  @{ Id = 'input-pause'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0');
     Changed = @('--time', '1.0', '--pause', '--pause-elapsed', '0.3'); Minimum = 5.0;
     Description = 'the pause backdrop is drawn' }
  @{ Id = 'input-key-scale'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0', '--key-scale', '1.5'); Minimum = 0.3;
     Description = 'the key scale resizes the notes' }
  @{ Id = 'bad-effect'; BaseChart = $singleNote; ChangedChart = $singleNote;
     Base = @('--time', '0.55');
     Changed = @('--time', '0.55', '--force-bad'); Minimum = 1.0;
     Description = 'a bad judgement leaves a fading copy of the note' }
  @{ Id = 'hold-sheet'; BaseChart = $hold; ChangedChart = $hold;
     Base = @('--time', '1.2');
     Changed = @('--time', '1.2', '--skin', $holdSkin); Minimum = 2.0;
     Description = 'the hold sheet slicing and body behaviour follow the pack' }
  @{ Id = 'line-fade'; BaseChart = $fadeConstant; ChangedChart = $fade;
     Base = @('--time', '2.4'); Changed = @('--time', '2.4'); Minimum = 0.2;
     Description = 'a disappear event fades the judge line' }
  @{ Id = 'note-side'; BaseChart = $sideAbove; ChangedChart = $sideBelow;
     Base = @('--time', '1.2'); Changed = @('--time', '1.2'); Minimum = 0.2;
     Description = 'a note under the line is drawn flipped' }
  @{ Id = 'speed-change'; BaseChart = $speedConstant; ChangedChart = $speedChange;
     Base = @('--time', '2.2'); Changed = @('--time', '2.2'); Minimum = 0.3;
     Description = 'a second speed segment moves the notes further' }
  @{ Id = 'line-rotation'; BaseChart = $rotatedLevel; ChangedChart = $rotated;
     Base = @('--time', '1.2'); Changed = @('--time', '1.2'); Minimum = 1.5;
     Description = 'a rotated play field draws differently from a level one' }
  @{ Id = 'note-highlight'; BaseChart = $plain; ChangedChart = $moreBets;
     Base = @('--time', '0.9'); Changed = @('--time', '0.9'); Minimum = 0.2;
     Description = 'the highlight note variant draws with its own sheet' }
  @{ Id = 'skin-overrides'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0', '--skin', $skin); Minimum = 0.5;
     Description = 'a skin overrides the built-in materials' }
  @{ Id = 'skin-info-yml'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0', '--skin', $skinYml); Minimum = 1.0;
     Description = 'an info.yml skin configures materials and replaces note artwork' }
  @{ Id = 'effect-bloom'; BaseChart = $plain; ChangedChart = $bloom;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0'); Minimum = 3.0;
     Description = 'a bloom storyboard pass runs' }
  @{ Id = 'effect-vignette'; BaseChart = $plain; ChangedChart = $vignette;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0'); Minimum = 1.0;
     Description = 'a vignette storyboard pass runs' }
  @{ Id = 'red-zones'; BaseChart = $plain; ChangedChart = $redzone;
     Base = @('--time', '1.0'); Changed = @('--time', '1.0'); Minimum = 0.5;
     Description = 'red zones are drawn' }
  @{ Id = 'red-zone-post-effects'; BaseChart = $redzone; ChangedChart = $redzoneShader;
     Base = @('--time', '5.0'); Changed = @('--time', '5.0'); Minimum = 25.0;
     Description = 'stage and global shader passes process the composited red zones' }
  @{ Id = 'gif-line'; BaseChart = $gif; ChangedChart = $gif;
     Base = @('--time', '0.1'); Changed = @('--time', '0.6'); Minimum = 0.5;
     Description = 'a GIF line texture advances frames' }
  @{ Id = 'level-ds-grid'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '115.0');
     Changed = @('--time', '115.0', '--song', 'desultorysignals'); Minimum = 3.0;
     Description = 'the grid level effect draws' }
  # Song names are matched case- and punctuation-insensitively, so an ASCII spelling is
  # enough. Non-ASCII names are covered by the level-effect unit tests instead, which avoids
  # depending on how this script's own encoding is read.
  @{ Id = 'level-message'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '140.0');
     Changed = @('--time', '140.0', '--song', 'message'); Minimum = 3.0;
     Description = 'the kaleidoscope level effect draws' }
  # Display settings that are persisted by the settings screen must reach the picture.
  @{ Id = 'setting-judge-line-width'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '4.0');
     Changed = @('--time', '4.0', '--judge-line-width', '2.0'); Minimum = 0.2;
     Description = 'the judge-line-width setting scales plain lines' }
  @{ Id = 'setting-multi-press'; BaseChart = $moreBets; ChangedChart = $moreBets;
     Base = @('--time', '0.0');
     Changed = @('--time', '0.0', '--no-multi-press'); Minimum = 0.2;
     Description = 'disabling multi-press highlight falls back to the normal sheet' }
  @{ Id = 'setting-low-res'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0');
     Changed = @('--time', '1.0', '--low-res'); Minimum = 0.2;
     Description = 'the low-resolution setting renders through the half-res target' }
  @{ Id = 'setting-antialias'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '1.0', '--low-res');
     Changed = @('--time', '1.0', '--low-res', '--antialias'); Minimum = 0.05;
     Description = 'the antialias setting resolves the low-res frame through FXAA' }
  @{ Id = 'setting-chart-reveal'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '0.9');
     Changed = @('--time', '0.9', '--chart-reveal'); Minimum = 1.0;
     Description = 'the chart-reveal setting draws the revealing play field' }
  @{ Id = 'setting-story-mode'; BaseChart = $plain; ChangedChart = $plain;
     Base = @('--time', '115.0', '--song', 'desultorysignals');
     Changed = @('--time', '115.0', '--song', 'desultorysignals', '--no-story-mode');
     Minimum = 1.0;
     Description = 'disabling story mode gates the non-Message level effect' }
  @{ Id = 'setting-chart-shader'; BaseChart = $bloom; ChangedChart = $bloom;
     Base = @('--time', '1.0');
     Changed = @('--time', '1.0', '--no-chart-shader'); Minimum = 1.0;
     Description = 'disabling the chart shader skips the storyboard pass' }
)

$failures = 0
foreach ($case in $cases) { $failures += Invoke-Case $case }

if ($failures -ne 0) {
  Write-Host "$failures render case(s) did not reach the picture"
  exit 1
}
Write-Host "Render matrix passed: $($cases.Count) cases verified"
exit 0
