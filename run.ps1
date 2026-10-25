param(
    [string]$Address,
    [ValidateSet('real30','lsfg','native60')][string]$Mode = 'real30',
    [ValidateRange(0,1000000)][int]$Frames = 0,
    [switch]$ReuseLogServer,
    [string]$MonitorConfig = (Join-Path $PSScriptRoot '../switch-monitor/data/config.json')
)
$ErrorActionPreference = 'Stop'
if (-not $Address) {
    if (-not (Test-Path -LiteralPath $MonitorConfig)) { throw 'Specify -Address <Switch IPv4>.' }
    $Address = (Get-Content -LiteralPath $MonitorConfig -Raw | ConvertFrom-Json).Ip
}
$parsedAddress = $null
if (-not [System.Net.IPAddress]::TryParse($Address, [ref]$parsedAddress) -or
    $parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'Address must be a literal Switch IPv4 address.'
}
$sdk = $env:DEVKITPRO
if (-not $sdk -or $sdk -eq '/opt/devkitpro') {
    $msys = $env:SFG_MSYS_ROOT
    if (-not $msys) { $msys = 'C:/msys64' }
    $sdk = Join-Path $msys 'opt/devkitpro'
}
$nxlink = Join-Path $sdk 'tools/bin/nxlink.exe'
$nro = Join-Path $PSScriptRoot 'build/switch-frame-generator.nro'
if (-not (Test-Path -LiteralPath $nro)) { throw 'Build the NRO first: py -3 build.py' }
if (-not (Test-Path -LiteralPath $nxlink)) { throw "nxlink not found: $nxlink" }
$nroArguments = "--mode=$Mode --frames=$Frames"
Write-Host "Sending Switch Frame Generator to $Address, mode=$Mode, frames=$Frames. Enable hbmenu netloader with Y."
$nxArguments = @('-r', '3', '-a', $Address, '-p', 'switch-frame-generator.nro', '--args', $nroArguments, $nro)
if (-not $ReuseLogServer) { $nxArguments = @('-s') + $nxArguments }
else { Write-Host 'Using the existing nxlink log receiver on TCP 28771; logs are written by that receiver.' }
& $nxlink @nxArguments
exit $LASTEXITCODE
