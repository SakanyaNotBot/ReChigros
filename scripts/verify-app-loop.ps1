# Drives the real application loop in a hidden window and checks that the run reaches the
# states it should. This exercises the whole stack — audio, input, screens, renderer,
# replay — rather than the renderer alone.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\verify-app-loop.ps1 `
#          -App build\desktop-release\src\app\ReChigros.exe `
#          -Chart tests\fixtures\plain.json -Work build\testdata\app-loop

param(
  [Parameter(Mandatory = $true)][string]$App,
  [Parameter(Mandatory = $true)][string]$Chart,
  [Parameter(Mandatory = $true)][string]$Work
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Work | Out-Null

$failures = 0

function Invoke-Run([string]$Name, [string[]]$Extra, [string]$Expect) {
  $output = & $App $Chart @Extra 2>&1 | Out-String
  $exit = $LASTEXITCODE
  if ($exit -ne 0) {
    Write-Host "[FAIL] ${Name}: exit $exit"
    Write-Host $output.Trim()
    $script:failures++
    return
  }
  if ($output -notmatch 'BATCH-RUN PASS') {
    Write-Host "[FAIL] ${Name}: the run did not report BATCH-RUN PASS"
    Write-Host $output.Trim()
    $script:failures++
    return
  }
  if ($Expect -and $output -notmatch $Expect) {
    Write-Host "[FAIL] ${Name}: expected '$Expect' in the run summary"
    Write-Host $output.Trim()
    $script:failures++
    return
  }
  # The screen split is the most direct evidence that the run moved between screens.
  $screens = if ($output -match 'screens: ([^\r\n]+)') { $Matches[1].Trim() } else { 'none' }
  Write-Host ("[ok]   {0,-18} screens={1}" -f $Name, $screens)
}

# A short run stays in gameplay. The fixed step keeps the frame clock deterministic, so the
# run is still inside the opening wait when the frame limit is reached at a fast frame rate.
Invoke-Run 'gameplay' @('--frames', '120', '--fixed-delta') 'game: chartTime'

# A long run must reach the result screen with a real result.
Invoke-Run 'to-result' @('--frames', '900', '--expect-result') 'perfect=[1-9]|good=[1-9]|miss=[1-9]'

# The pause overlay must open, count down and resume. The fixed step is required here for
# the same reason as the gameplay case: without it the frames advance by real elapsed time,
# a short fixture chart finishes before the pause has been driven, and the run lands on the
# result screen while the pause assertions are still waiting.
Invoke-Run 'pause-cycle' @('--frames', '900', '--fixed-delta', '--expect-pause') 'pause: menu=1 countdown=1 resumed=1'

# A recorded replay must reproduce the run that produced it.
$replay = Join-Path $Work 'run.rpl'
Invoke-Run 'record' @('--frames', '900', '--record-replay', $replay) 'REPLAY-RECORDED PASS'
if (-not (Test-Path $replay)) {
  Write-Host '[FAIL] record: no replay file was written'
  $failures++
} else {
  Invoke-Run 'playback' @('--frames', '900', '--replay-playback', $replay) 'match=1'
}

# Each play mode must be applied and behave the way its rules require.
Invoke-Run 'mode-autoplay' @('--frames', '900', '--autoplay') `
  'mode: practice=0 challenge=0 mirror=0 autoplay=1[\s\S]*perfect=3 good=0 bad=0 miss=0'
Invoke-Run 'mode-challenge' @('--frames', '900', '--challenge') `
  'mode: practice=0 challenge=1 mirror=0 autoplay=0'
Invoke-Run 'mode-practice' @('--frames', '900', '--practice') `
  'mode: practice=1 challenge=0 mirror=0 autoplay=0'
# The practice window must reach the judgement engine, not just the menu.
Invoke-Run 'practice-window' @('--frames', '900', '--practice', '--practice-perfect', '40') 
  'practice: enabled=1 perfectMs=40 active=1'
# A harness that presses late must produce a different judgement than one that does not.
Invoke-Run 'mistimed-press' @('--frames', '900', '--mistime', '170') 'good=[1-9]'
# Mirroring only reflects input, so it must not change the rendered picture; the run has
# to complete with the mode applied and without a visual error.
Invoke-Run 'mode-mirror' @('--frames', '900', '--mirror') `
  'mode: practice=0 challenge=0 mirror=1 autoplay=0'

if ($failures -ne 0) {
  Write-Host "$failures application run(s) failed"
  exit 1
}
Write-Host 'Application loop check passed'
exit 0
