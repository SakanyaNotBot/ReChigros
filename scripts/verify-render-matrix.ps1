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

Add-Type -AssemblyName System.Drawing

function Invoke-ProbeRender($Id, $Chart, $RenderArgs) {
  $path = Join-Path $Work "$Id.png"
  if (Test-Path $path) { Remove-Item $path -Force }
  $allArgs = @('--chart', $Chart, '--assets', $Assets, '--frames', '1', '--out', $path) + $RenderArgs
  & $Smoke $allArgs | Out-Null
  if (-not (Test-Path $path)) {
    Write-Host "[FAIL] ${Id}: a render produced no output"
    return $null
  }
  return $path
}

function Measure-Ink($Path, $Left, $Top, $Width, $Height, $Threshold = 180) {
  if (-not (Test-Path $Path)) { return $null }
  $image = [System.Drawing.Bitmap]::FromFile($Path)
  try {
    $minX = [int]::MaxValue
    $minY = [int]::MaxValue
    $maxX = -1
    $maxY = -1
    $sumX = 0.0
    $sumY = 0.0
    $count = 0
    $right = [Math]::Min($image.Width, $Left + $Width)
    $bottom = [Math]::Min($image.Height, $Top + $Height)
    for ($y = [Math]::Max(0, $Top); $y -lt $bottom; ++$y) {
      for ($x = [Math]::Max(0, $Left); $x -lt $right; ++$x) {
        $pixel = $image.GetPixel($x, $y)
        if ([Math]::Min($pixel.R, [Math]::Min($pixel.G, $pixel.B)) -lt $Threshold) { continue }
        $minX = [Math]::Min($minX, $x)
        $minY = [Math]::Min($minY, $y)
        $maxX = [Math]::Max($maxX, $x)
        $maxY = [Math]::Max($maxY, $y)
        $sumX += $x
        $sumY += $y
        ++$count
      }
    }
    if ($count -eq 0) { return $null }
    return @{ Left = $minX; Top = $minY; Right = $maxX; Bottom = $maxY;
      Width = $maxX - $minX + 1; Height = $maxY - $minY + 1;
      CenterX = $sumX / $count; CenterY = $sumY / $count; Count = $count }
  } finally {
    $image.Dispose()
  }
}

function Measure-NoteInk($Path, $Left, $Top, $Width, $Height) {
  if (-not (Test-Path $Path)) { return $null }
  $image = [System.Drawing.Bitmap]::FromFile($Path)
  try {
    $minX = [int]::MaxValue
    $minY = [int]::MaxValue
    $maxX = -1
    $maxY = -1
    $sumY = 0.0
    $count = 0
    $right = [Math]::Min($image.Width, $Left + $Width)
    $bottom = [Math]::Min($image.Height, $Top + $Height)
    for ($y = [Math]::Max(0, $Top); $y -lt $bottom; ++$y) {
      if ($y -ge 354 -and $y -le 365) { continue }
      for ($x = [Math]::Max(0, $Left); $x -lt $right; ++$x) {
        $pixel = $image.GetPixel($x, $y)
        if ([Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)) -le 90) { continue }
        $minX = [Math]::Min($minX, $x)
        $minY = [Math]::Min($minY, $y)
        $maxX = [Math]::Max($maxX, $x)
        $maxY = [Math]::Max($maxY, $y)
        $sumY += $y
        ++$count
      }
    }
    if ($count -eq 0) { return $null }
    return @{ Left = $minX; Top = $minY; Right = $maxX; Bottom = $maxY;
      CenterY = $sumY / $count; Count = $count }
  } finally {
    $image.Dispose()
  }
}

function Measure-DifferentialInk($OffPath, $OnPath, $Left, $Top, $Width, $Height, $Threshold = 25) {
  $off = [System.Drawing.Bitmap]::FromFile($OffPath)
  $on = [System.Drawing.Bitmap]::FromFile($OnPath)
  try {
    $minX = [int]::MaxValue
    $minY = [int]::MaxValue
    $maxX = -1
    $maxY = -1
    $sumX = 0.0
    $sumY = 0.0
    $count = 0
    $right = [Math]::Min($on.Width, $Left + $Width)
    $bottom = [Math]::Min($on.Height, $Top + $Height)
    for ($y = [Math]::Max(0, $Top); $y -lt $bottom; ++$y) {
      for ($x = [Math]::Max(0, $Left); $x -lt $right; ++$x) {
        $a = $off.GetPixel($x, $y)
        $b = $on.GetPixel($x, $y)
        $delta = [Math]::Max([Math]::Abs($a.R - $b.R),
          [Math]::Max([Math]::Abs($a.G - $b.G), [Math]::Abs($a.B - $b.B)))
        if ($delta -le $Threshold) { continue }
        $minX = [Math]::Min($minX, $x)
        $minY = [Math]::Min($minY, $y)
        $maxX = [Math]::Max($maxX, $x)
        $maxY = [Math]::Max($maxY, $y)
        $sumX += $x
        $sumY += $y
        ++$count
      }
    }
    if ($count -eq 0) { return $null }
    return @{ Left = $minX; Top = $minY; Right = $maxX; Bottom = $maxY;
      Width = $maxX - $minX + 1; Height = $maxY - $minY + 1;
      CenterX = $sumX / $count; CenterY = $sumY / $count; Count = $count }
  } finally {
    $off.Dispose()
    $on.Dispose()
  }
}

function Measure-RegionDifference($FirstPath, $SecondPath, $Left, $Top, $Width, $Height) {
  $first = [System.Drawing.Bitmap]::FromFile($FirstPath)
  $second = [System.Drawing.Bitmap]::FromFile($SecondPath)
  try {
    if ($first.Width -ne $second.Width -or $first.Height -ne $second.Height) { return 100.0 }
    $different = 0
    $count = 0
    $right = [Math]::Min($first.Width, $Left + $Width)
    $bottom = [Math]::Min($first.Height, $Top + $Height)
    for ($y = [Math]::Max(0, $Top); $y -lt $bottom; ++$y) {
      for ($x = [Math]::Max(0, $Left); $x -lt $right; ++$x) {
        $a = $first.GetPixel($x, $y)
        $b = $second.GetPixel($x, $y)
        $delta = [Math]::Max([Math]::Abs($a.R - $b.R),
          [Math]::Max([Math]::Abs($a.G - $b.G), [Math]::Abs($a.B - $b.B)))
        if ($delta -gt 3) { ++$different }
        ++$count
      }
    }
    if ($count -eq 0) { return 100.0 }
    return 100.0 * $different / $count
  } finally {
    $first.Dispose()
    $second.Dispose()
  }
}

function Measure-EffectCenter($BasePath, $ChangedPath) {
  $base = [System.Drawing.Bitmap]::FromFile($BasePath)
  $changed = [System.Drawing.Bitmap]::FromFile($ChangedPath)
  try {
    $sumX = 0.0
    $sumY = 0.0
    $count = 0
    for ($y = 140; $y -lt [Math]::Min(540, $changed.Height); ++$y) {
      for ($x = 450; $x -lt [Math]::Min(830, $changed.Width); ++$x) {
        $a = $base.GetPixel($x, $y)
        $b = $changed.GetPixel($x, $y)
        $delta = [Math]::Max([Math]::Abs($a.R - $b.R),
          [Math]::Max([Math]::Abs($a.G - $b.G), [Math]::Abs($a.B - $b.B)))
        $high = [Math]::Max($b.R, [Math]::Max($b.G, $b.B))
        $low = [Math]::Min($b.R, [Math]::Min($b.G, $b.B))
        if ($delta -le 25 -or $high -lt 100 -or $high - $low -lt 40) { continue }
        $sumX += $x
        $sumY += $y
        ++$count
      }
    }
    if ($count -eq 0) { return $null }
    return @{ X = $sumX / $count; Y = $sumY / $count; Count = $count }
  } finally {
    $base.Dispose()
    $changed.Dispose()
  }
}

function Assert-ScaledInk($Id, $BasePath, $ChangedPath, $BaseRoi, $ChangedRoi, $Threshold = 180) {
  $base = Measure-Ink $BasePath $BaseRoi[0] $BaseRoi[1] $BaseRoi[2] $BaseRoi[3] $Threshold
  $changed = Measure-Ink $ChangedPath $ChangedRoi[0] $ChangedRoi[1] $ChangedRoi[2] $ChangedRoi[3] $Threshold
  if ($null -eq $base -or $null -eq $changed) {
    Write-Host "[FAIL] ${Id}: HUD ink is missing"
    return 1
  }
  $scaleX = $changed.Width / $base.Width
  $scaleY = $changed.Height / $base.Height
  $expectedX = 640.0 + ($base.CenterX - 640.0) * 0.30
  $expectedY = 360.0 + ($base.CenterY - 360.0) * 0.30
  if ([Math]::Abs($scaleX - 0.30) -gt 0.22 -or [Math]::Abs($scaleY - 0.30) -gt 0.22 -or
      [Math]::Abs($changed.CenterX - $expectedX) -gt 8.0 -or
      [Math]::Abs($changed.CenterY - $expectedY) -gt 8.0) {
    Write-Host ("[FAIL] {0}: scale=({1:N3},{2:N3}), centre=({3:N1},{4:N1}), expected centre=({5:N1},{6:N1})" -f `
        $Id, $scaleX, $scaleY, $changed.CenterX, $changed.CenterY, $expectedX, $expectedY)
    return 1
  }
  Write-Host "[ok]   ${Id}: HUD ink follows the reveal transform"
  return 0
}

function Assert-HoldEffect($Id, $Chart, $HitTime, $ExpectedY) {
  $hitText = $HitTime.ToString('0.00', [Globalization.CultureInfo]::InvariantCulture)
  $common = @('--time', $hitText, '--dim', '1.0')
  $base = Invoke-ProbeRender "$Id-base" $Chart ($common + @('--no-hold-bursts'))
  $changed = Invoke-ProbeRender "$Id-change" $Chart ($common + @('--hold-hit-time', $hitText))
  if ($null -eq $base -or $null -eq $changed) { return 1 }
  $center = Measure-EffectCenter $base $changed
  if ($null -eq $center) {
    Write-Host "[FAIL] ${Id}: no hold-head effect was found"
    return 1
  }
  if ([Math]::Abs($center.X - 640.0) -gt 35.0 -or [Math]::Abs($center.Y - $ExpectedY) -gt 25.0) {
    Write-Host ("[FAIL] {0}: effect centre=({1:N1},{2:N1}), expected=(640,{3:N1})" -f `
        $Id, $center.X, $center.Y, $ExpectedY)
    return 1
  }
  Write-Host ("[ok]   {0,-20} effect centre=({1:N1},{2:N1})" -f $Id, $center.X, $center.Y)
  return 0
}

function Assert-RpeHoldFrame($Case) {
  $id = $Case.Id
  $live = Invoke-ProbeRender "$id-live" $Case.Chart `
    @('--time', '0.75', '--dim', '1.0', '--no-hold-bursts')
  $after = Invoke-ProbeRender "$id-after" $Case.Chart `
    @('--time', $Case.After, '--dim', '1.0', '--no-hold-bursts')
  if ($null -eq $live -or $null -eq $after) { return 1 }

  $all = Measure-NoteInk $live 560 20 160 640
  $head = Measure-NoteInk $live 560 $Case.Head[0] 160 ($Case.Head[1] - $Case.Head[0] + 1)
  $tail = Measure-NoteInk $live 560 $Case.Tail[0] 160 ($Case.Tail[1] - $Case.Tail[0] + 1)
  $afterInk = Measure-NoteInk $after 560 20 160 640
  if ($null -eq $all -or $null -eq $head -or $null -eq $tail) {
    Write-Host "[FAIL] ${id}: the head, body or tail cap has no ink"
    return 1
  }
  if ($null -ne $afterInk) {
    Write-Host ("[FAIL] {0}: {1} note pixels remain after end in rows {2}..{3}" -f `
        $id, $afterInk.Count, $afterInk.Top, $afterInk.Bottom)
    return 1
  }
  if ([Math]::Abs($all.Top - $Case.Top) -gt 2.0 -or
      [Math]::Abs($all.Bottom - $Case.Bottom) -gt 2.0) {
    Write-Host ("[FAIL] {0}: hold ink rows={1}..{2}, expected={3}..{4}" -f `
        $id, $all.Top, $all.Bottom, $Case.Top, $Case.Bottom)
    return 1
  }
  if ([Math]::Abs($head.Top - $Case.Head[0]) -gt 2.0 -or
      [Math]::Abs($head.Bottom - $Case.Head[1]) -gt 2.0 -or $head.Count -lt 500) {
    Write-Host ("[FAIL] {0}: head rows={1}..{2}, pixels={3}, expected rows={4}..{5}" -f `
        $id, $head.Top, $head.Bottom, $head.Count, $Case.Head[0], $Case.Head[1])
    return 1
  }
  if ([Math]::Abs($tail.Top - $Case.Tail[0]) -gt 2.0 -or
      [Math]::Abs($tail.Bottom - $Case.Tail[1]) -gt 2.0 -or $tail.Count -lt 500) {
    Write-Host ("[FAIL] {0}: tail-cap rows={1}..{2}, pixels={3}, expected rows={4}..{5}" -f `
        $id, $tail.Top, $tail.Bottom, $tail.Count, $Case.Tail[0], $Case.Tail[1])
    return 1
  }
  $wrongDirection = ($Case.TailSide -eq 'above' -and $tail.CenterY -ge $head.CenterY - 20.0) -or
    ($Case.TailSide -eq 'below' -and $tail.CenterY -le $head.CenterY + 20.0)
  if ($wrongDirection -or [Math]::Abs($head.CenterY - 360.0) -lt 60.0) {
    Write-Host ("[FAIL] {0}: head centre={1:N1}, tail centre={2:N1}, expected tail {3} and an unclamped head" -f `
        $id, $head.CenterY, $tail.CenterY, $Case.TailSide)
    return 1
  }
  Write-Host ("[ok]   {0,-24} head={1:N1}, tail={2:N1}, end cleared" -f `
      $id, $head.CenterY, $tail.CenterY)
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
$hudMatrix = Join-Path $Fixtures 'hud_matrix.json'
$revealClean = Join-Path $Fixtures 'reveal_clean.json'
$revealOcclusion = Join-Path $Fixtures 'reveal_occlusion.json'
$revealOutsideNote = Join-Path $Fixtures 'reveal_outside_note.json'
$hiddenLineZero = Join-Path $Fixtures 'hidden_line_zero.json'
$holdFxOfficial = Join-Path $Fixtures 'hold_fx_official.json'
$holdFxRpe = Join-Path $Fixtures 'hold_fx_rpe.json'

$rpeHoldCases = @(
  @{ Id = 'hold-rpe-negative-length'; Chart = Join-Path $Fixtures 'rpe_hold_negative_length.json';
     After = '1.05'; Top = 89; Bottom = 317; Head = @(89, 100); Tail = @(306, 317); TailSide = 'below' }
  @{ Id = 'hold-rpe-negative-flow'; Chart = Join-Path $Fixtures 'rpe_hold_negative_flow.json';
     After = '3.05'; Top = 494; Bottom = 614; Head = @(494, 505); Tail = @(603, 614); TailSide = 'below' }
  @{ Id = 'hold-rpe-cross-zero-ramp'; Chart = Join-Path $Fixtures 'rpe_hold_cross_zero_ramp.json';
     After = '4.05'; Top = 416; Bottom = 644; Head = @(416, 427); Tail = @(633, 644); TailSide = 'below' }
  @{ Id = 'hold-rpe-negative-note-speed'; Chart = Join-Path $Fixtures 'rpe_hold_negative_note_speed.json';
     After = '2.55'; Top = 449; Bottom = 569; Head = @(449, 460); Tail = @(558, 569); TailSide = 'below' }
  @{ Id = 'hold-rpe-normal'; Chart = Join-Path $Fixtures 'rpe_hold_normal.json';
     After = '2.55'; Top = 24; Bottom = 100; Head = @(89, 100); Tail = @(24, 36); TailSide = 'above' }
)

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
  @{ Id = 'reveal-occlusion'; BaseChart = $revealClean; ChangedChart = $revealOcclusion;
     Base = @('--time', '2.0', '--dim', '1.0', '--chart-reveal');
     Changed = @('--time', '2.0', '--dim', '1.0', '--chart-reveal'); Minimum = 0.02;
     Description = 'a scaled note draws over the reveal backdrop ring' }
  @{ Id = 'reveal-outside-note'; BaseChart = $revealClean; ChangedChart = $revealOutsideNote;
     Base = @('--time', '2.0', '--dim', '1.0', '--chart-reveal');
     Changed = @('--time', '2.0', '--dim', '1.0', '--chart-reveal'); Minimum = 0.02;
     Description = 'inverse viewport culling retains a note beyond the original stage' }
)

$failures = 0
foreach ($case in $cases) { $failures += Invoke-Case $case }

$hudNoFpsArgs = @('--time', '2.0', '--dim', '1.0', '--song', 'Matrix Song', '--level', 'EZ')
$hudArgs = $hudNoFpsArgs + @('--show-fps')
$hudBase = Invoke-ProbeRender 'hud-base' $hudMatrix $hudArgs
$hudReveal = Invoke-ProbeRender 'hud-reveal' $hudMatrix ($hudArgs + @('--chart-reveal'))
$hudBaseNoFps = Invoke-ProbeRender 'hud-base-nofps' $hudMatrix $hudNoFpsArgs
$hudRevealNoFps = Invoke-ProbeRender 'hud-reveal-nofps' $hudMatrix ($hudNoFpsArgs + @('--chart-reveal'))
if ($null -ne $hudBase -and $null -ne $hudReveal -and
    $null -ne $hudBaseNoFps -and $null -ne $hudRevealNoFps) {
  $hudProbes = @(
    @{ Id = 'hud-pause'; Base = @(0, 10, 100, 70); Changed = @(430, 245, 70, 45); Threshold = 180 },
    @{ Id = 'hud-progress'; Base = @(0, 0, 450, 15); Changed = @(430, 248, 170, 10); Threshold = 100 },
    @{ Id = 'hud-score'; Base = @(1050, 15, 225, 65); Changed = @(740, 245, 120, 45); Threshold = 180 },
    @{ Id = 'hud-combo'; Base = @(560, 10, 160, 85); Changed = @(585, 245, 110, 45); Threshold = 180 },
    @{ Id = 'hud-song'; Base = @(50, 650, 300, 65); Changed = @(450, 425, 130, 55); Threshold = 180 },
    @{ Id = 'hud-level'; Base = @(1100, 650, 175, 65); Changed = @(760, 425, 100, 55); Threshold = 180 }
  )
  foreach ($probe in $hudProbes) {
    $failures += Assert-ScaledInk $probe.Id $hudBase $hudReveal $probe.Base $probe.Changed $probe.Threshold
  }

  $fpsBase = Measure-DifferentialInk $hudBaseNoFps $hudBase 0 670 58 45
  $fpsReveal = Measure-DifferentialInk $hudRevealNoFps $hudReveal 0 670 58 45
  if ($null -eq $fpsBase -or $null -eq $fpsReveal) {
    Write-Host '[FAIL] hud-fps: FPS ink is missing'
    ++$failures
  } elseif ([Math]::Abs($fpsBase.CenterX - $fpsReveal.CenterX) -gt 1.0 -or
      [Math]::Abs($fpsBase.CenterY - $fpsReveal.CenterY) -gt 1.0 -or
      $fpsBase.Width -ne $fpsReveal.Width -or $fpsBase.Height -ne $fpsReveal.Height) {
    Write-Host '[FAIL] hud-fps: FPS moved or changed size during reveal'
    ++$failures
  } else {
    Write-Host '[ok]   hud-fps: FPS stays fixed during reveal'
  }
} else {
  ++$failures
}

$wideArgs = @('--width', '1600', '--height', '720') + $hudArgs
$wideBase = Invoke-ProbeRender 'side-base' $hudMatrix $wideArgs
$wideReveal = Invoke-ProbeRender 'side-reveal' $hudMatrix ($wideArgs + @('--chart-reveal'))
if ($null -eq $wideBase -or $null -eq $wideReveal) {
  ++$failures
} else {
  $sideDifference = Measure-RegionDifference $wideBase $wideReveal 0 0 140 720
  if ($sideDifference -gt 0.1) {
    Write-Host ("[FAIL] reveal-sidebars: {0:N2}% of side-column pixels changed" -f $sideDifference)
    ++$failures
  } else {
    Write-Host '[ok]   reveal-sidebars: fixed side columns are unchanged'
  }
}

$hiddenArgs = @('--time', '2.0', '--dim', '1.0')
$hiddenBase = Invoke-ProbeRender 'hidden-line-base' $hiddenLineZero $hiddenArgs
$hiddenReveal = Invoke-ProbeRender 'hidden-line-reveal' $hiddenLineZero ($hiddenArgs + @('--chart-reveal'))
if ($null -eq $hiddenBase -or $null -eq $hiddenReveal) {
  ++$failures
} else {
  $lineDifference = Measure-RegionDifference $hiddenBase $hiddenReveal 400 330 480 60
  if ($lineDifference -lt 5.0) {
    Write-Host ("[FAIL] hidden-line-floor: only {0:N2}% of the central line region changed" -f $lineDifference)
    ++$failures
  } else {
    Write-Host '[ok]   hidden-line-floor: zero-alpha line receives the reveal floor'
  }
}

$failures += Assert-HoldEffect 'hold-official-early' $holdFxOfficial 0.90 244.8
$failures += Assert-HoldEffect 'hold-official-exact' $holdFxOfficial 1.00 288.0
$failures += Assert-HoldEffect 'hold-official-late' $holdFxOfficial 1.05 288.0
$failures += Assert-HoldEffect 'hold-rpe-early' $holdFxRpe 0.90 201.6
$failures += Assert-HoldEffect 'hold-rpe-exact' $holdFxRpe 1.00 288.0
$failures += Assert-HoldEffect 'hold-rpe-late' $holdFxRpe 1.05 288.0
foreach ($case in $rpeHoldCases) { $failures += Assert-RpeHoldFrame $case }

if ($failures -ne 0) {
  Write-Host "$failures render case(s) did not reach the picture"
  exit 1
}
Write-Host "Render matrix passed: $($cases.Count) comparison cases and directed probes verified"
exit 0
