param([ValidateRange(6,120)][int]$Seconds=14)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$app=Join-Path $root 'build/host-win/sfg_desktop.exe'
if(-not(Test-Path -LiteralPath $app)){throw 'Build first: py -3 build.py --host-only'}
$running=Get-Process sfg_desktop -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $app}
if($running){throw 'Close the interactive preview before benchmark measurements.'}
foreach($case in @(@('fixed2x','lsfg'),@('variable-off','variable-off'),@('adaptive40','adaptive40'))){
    $name=$case[0]; $mode=$case[1]
    Write-Host "Measuring $name for $Seconds seconds..."
    $process=Start-Process -FilePath $app -WorkingDirectory $root -WindowStyle Normal `
        -ArgumentList "--mode=$mode","--bench=$name","--seconds=$Seconds" `
        -RedirectStandardOutput (Join-Path $root "build/perf-$name.log") `
        -RedirectStandardError (Join-Path $root "build/perf-$name-error.log") -PassThru
    $processHandle=$process.Handle
    $process.WaitForExit()
    $process.Refresh()
    if($process.ExitCode -ne 0){throw "Benchmark $name failed; see build/perf-$name-error.log"}
}
Write-Host 'Measurements complete. Generate the report: py -3 tools/report_performance.py'
