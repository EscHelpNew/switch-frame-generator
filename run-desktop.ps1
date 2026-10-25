param([switch]$Smoke,[ValidateSet('adaptive40','variable-off','lsfg','real30','native60')][string]$Mode)
$ErrorActionPreference = 'Stop'
$desktopApp = Join-Path $PSScriptRoot 'build/host-win/sfg_desktop.exe'
if (-not (Test-Path -LiteralPath $desktopApp)) {
    throw 'Build the desktop app first: py -3 build.py --host-only'
}
$arguments = @()
if ($Smoke) { $arguments += '--smoke' }
if ($Mode) { $arguments += "--mode=$Mode" }
$logName = if ($Smoke) { 'desktop-smoke' } else { 'desktop' }
$parameters = @{
    FilePath = $desktopApp
    WorkingDirectory = $PSScriptRoot
    WindowStyle = 'Normal'
    RedirectStandardOutput = (Join-Path $PSScriptRoot "build/$logName.log")
    RedirectStandardError = (Join-Path $PSScriptRoot "build/$logName-error.log")
    PassThru = $true
}
if ($arguments.Count) { $parameters.ArgumentList = $arguments }
$process = Start-Process @parameters
Write-Host "LSFG Desktop started, PID $($process.Id). Log: build/$logName.log"
if ($Smoke) {
    $process.WaitForExit()
    exit $process.ExitCode
}
