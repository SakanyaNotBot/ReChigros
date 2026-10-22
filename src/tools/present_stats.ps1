# Summarises device present-path runs for the round-3 quiet A/B.
#
# Every run is a `--frame-stats` CSV (columns index,interval_ms,cpu_ms,screen,now_sec,
# gpu_ms,swap_ms,wall_sec; the old five-column format still works) plus, optionally, a
# `dumpsys SurfaceFlinger --latency` dump of the app surface captured during the same run.
# The first CSV given is the baseline for the delta columns; labels come from the file names.
#
# Usage:
#   pwsh -File src\tools\present_stats.ps1 `
#     -Csv perf-final\r3\quiet\old_r2.csv,perf-final\r3\quiet\after_skip.csv,perf-final\r3\quiet\after_noskip.csv `
#     -Sf  perf-final\r3\quiet\old_r2_sf.txt,perf-final\r3\quiet\after_skip_sf.txt,perf-final\r3\quiet\after_noskip_sf.txt

param(
  [Parameter(Mandatory = $true)][string[]]$Csv,
  [string[]]$Sf = @(),
  [int]$Warmup = 5
)

$ErrorActionPreference = 'Stop'

# `pwsh -File script.ps1 -Csv a,b,c` passes the list as one comma-joined string, so normalise
# both list parameters to clean path arrays here.
$Csv = @($Csv | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne '' })
$Sf = @($Sf | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne '' })

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return 0.0 }
  $sorted = $Values | Sort-Object
  $index = [int][math]::Floor($Fraction * ($sorted.Count - 1))
  return [double]$sorted[$index]
}

function Get-SeriesStats([double[]]$Values) {
  if ($Values.Count -eq 0) { return $null }
  $sorted = $Values | Sort-Object
  return [pscustomobject]@{
    Count = $Values.Count
    Mean = ($Values | Measure-Object -Average).Average
    P50 = [double]$sorted[[int][math]::Floor(0.50 * ($sorted.Count - 1))]
    P95 = [double]$sorted[[int][math]::Floor(0.95 * ($sorted.Count - 1))]
    P99 = [double]$sorted[[int][math]::Floor(0.99 * ($sorted.Count - 1))]
    Max = [double]$sorted[$sorted.Count - 1]
  }
}

function Read-FrameCsv([string]$Path) {
  $rows = Import-Csv $Path
  if ($rows.Count -le $Warmup) { throw "$Path has too few rows" }
  $rows = $rows | Select-Object -Skip $Warmup
  $interval = @($rows | ForEach-Object { [double]$_.interval_ms })
  $cpu = @($rows | ForEach-Object { [double]$_.cpu_ms })
  $swap = @($rows | ForEach-Object {
      if ($_.PSObject.Properties.Match('swap_ms').Count -gt 0) { [double]$_.swap_ms } else { 0.0 }
    })
  $gpu = @($rows | ForEach-Object {
      if ($_.PSObject.Properties.Match('gpu_ms').Count -gt 0) { [double]$_.gpu_ms } else { 0.0 }
    })
  [pscustomobject]@{
    Path = $Path
    Interval = Get-SeriesStats $interval
    Cpu = Get-SeriesStats $cpu
    Swap = Get-SeriesStats $swap
    Gpu = Get-SeriesStats $gpu
    GpuAvailable = (($gpu | Where-Object { $_ -gt 0 }).Count -gt 0)
    Over16 = ($interval | Where-Object { $_ -gt 16.7 }).Count
    Over33 = ($interval | Where-Object { $_ -gt 33.3 }).Count
    Histogram = ($interval | ForEach-Object { [math]::Min(9, [int]($PSItem / 2)) } |
        Group-Object | Sort-Object { [int]$_.Name } |
        ForEach-Object { "{0}-{1}ms:{2}" -f ([int]$_.Name * 2), ([int]$_.Name * 2 + 2), $_.Count }) -join ' '
  }
}

function Read-SurfaceFlinger([string]$Path) {
  $lines = Get-Content $Path | Where-Object { $_ -match "\d+\t\d+\t\d+" }
  $timestamps = New-Object System.Collections.Generic.List[double]
  foreach ($line in $lines) {
    $parts = $line -split '\s+'
    if ([double]$parts[1] -gt 0.0) { $timestamps.Add([double]$parts[1]) }
  }
  $deltas = New-Object System.Collections.Generic.List[double]
  for ($index = 1; $index -lt $timestamps.Count; ++$index) {
    $delta = ($timestamps[$index] - $timestamps[$index - 1]) / 1.0e6
    if ($delta -gt 0.0 -and $delta -lt 200.0) { $deltas.Add($delta) }
  }
  $stats = Get-SeriesStats @($deltas.ToArray())
  if ($env:RECHIGROS_PRESENT_DEBUG) {
    Write-Host "[debug] $Path lines=$($lines.Count) stamps=$($timestamps.Count) deltas=$($deltas.Count)"
  }
  $histogram = ($deltas | ForEach-Object { [math]::Round($PSItem, 1) } |
      Group-Object | Sort-Object { [double]$_.Name } |
      Select-Object -First 8 |
      ForEach-Object { "{0}ms:{1}" -f $_.Name, $_.Count }) -join ' '
  # The emulator panel runs at 240 Hz, so every present interval should be a multiple of
  # 4.167 ms; the share of single-tick presents is the cleanest "keeps up" indicator.
  $singleTick = ($deltas | Where-Object { $_ -lt 6.25 }).Count
  [pscustomobject]@{
    Stats = $stats
    SingleTickShare = if ($deltas.Count -gt 0) { 100.0 * $singleTick / $deltas.Count } else { 0.0 }
    Histogram = $histogram
  }
}

$runResults = @()
foreach ($path in $Csv) {
  $runResults += Read-FrameCsv $path
}

$baseline = $runResults[0]
Write-Host ''
Write-Host 'Frame stats (warm-up skipped)'
Write-Host ''
Write-Host ('| run | interval mean | p50 | p95 | p99 | >16.7 | >33 | cpu mean | swap mean | gpu mean |')
Write-Host ('| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($run in $runResults) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($run.Path)
  $gpuText = if ($run.GpuAvailable) { '{0:N2}' -f $run.Gpu.Mean } else { 'n/a' }
  Write-Host ('| {0} | {1:N2} | {2:N2} | {3:N2} | {4:N2} | {5} | {6} | {7:N2} | {8:N2} | {9} |' -f `
      $name, $run.Interval.Mean, $run.Interval.P50, $run.Interval.P95, $run.Interval.P99,
      $run.Over16, $run.Over33, $run.Cpu.Mean, $run.Swap.Mean, $gpuText)
}
Write-Host ''
Write-Host 'Deltas vs the first CSV (negative is better):'
Write-Host ''
Write-Host ('| run | d interval | d cpu | d swap |')
Write-Host ('| --- | --- | --- | --- |')
foreach ($run in $runResults) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($run.Path)
  Write-Host ('| {0} | {1:+0.00;-0.00} | {2:+0.00;-0.00} | {3:+0.00;-0.00} |' -f `
      $name, ($run.Interval.Mean - $baseline.Interval.Mean),
      ($run.Cpu.Mean - $baseline.Cpu.Mean), ($run.Swap.Mean - $baseline.Swap.Mean))
}
Write-Host ''
Write-Host 'Interval histograms (2 ms bins):'
foreach ($run in $runResults) {
  Write-Host ('  {0}: {1}' -f [System.IO.Path]::GetFileNameWithoutExtension($run.Path), $run.Histogram)
}

if ($Sf.Count -gt 0) {
  Write-Host ''
  Write-Host 'SurfaceFlinger present cadence (240 Hz ticks, deltas of the present column):'
  Write-Host ''
  Write-Host ('| run | mean | p50 | p95 | p99 | max | single-tick share | histogram |')
  Write-Host ('| --- | --- | --- | --- | --- | --- | --- | --- |')
  foreach ($presentPath in @($Sf)) {
    $present = Read-SurfaceFlinger $presentPath
    if ($null -eq $present -or $null -eq $present.Stats) {
      Write-Host ("[warn] no present deltas parsed from $presentPath")
      continue
    }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($presentPath)
    Write-Host ('| {0} | {1:N2} | {2:N2} | {3:N2} | {4:N2} | {5:N2} | {6:N1}% | {7} |' -f `
        $name, $present.Stats.Mean, $present.Stats.P50, $present.Stats.P95, $present.Stats.P99,
        $present.Stats.Max, $present.SingleTickShare, $present.Histogram)
  }
}
Write-Host ''
