param([Parameter(ValueFromRemainingArguments = $true)][string[]]$BuildArguments)
$ErrorActionPreference = 'Stop'
$python = $env:SFG_PYTHON
if (-not $python) {
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        & $launcher.Source -3 (Join-Path $PSScriptRoot 'build.py') @BuildArguments
        exit $LASTEXITCODE
    }
    $python = (Get-Command python.exe -ErrorAction Stop).Source
}
& $python (Join-Path $PSScriptRoot 'build.py') @BuildArguments
exit $LASTEXITCODE
