<# Wintosh-original E2E check; GPL-3.0-only. #>
param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture,
    [string]$GeneratorMode = '--emit-darwin',
    [int]$ExpectedExit = 1
)
$ErrorActionPreference = 'Stop'
& $Generator $GeneratorMode $Fixture
if ($LASTEXITCODE -ne 0) { throw "DarwinStart fixture generation failed: $LASTEXITCODE" }
$output = & $Runner --startup-abi darwin $Fixture 2>&1
$code = $LASTEXITCODE
$text = ($output -join "`n")
if ($code -ne $ExpectedExit) { throw "DarwinStart E2E expected exit=$ExpectedExit, got exit=$code output=$text" }
if ($text -match 'DARLING_GUEST_EXCEPTION') {
    throw "DarwinStart E2E entered the guest exception boundary: $text"
}
Write-Output 'DARWIN_START_E2E=PASS EXIT=1'
