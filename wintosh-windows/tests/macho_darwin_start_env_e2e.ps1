<# Wintosh-original E2E check; GPL-3.0-only. #>
param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture
)
$ErrorActionPreference = 'Stop'
& $Generator --emit-darwin-env $Fixture
if ($LASTEXITCODE -ne 0) { throw "Darwin env fixture generation failed: $LASTEXITCODE" }
$output = & $Runner --startup-abi darwin --env WINTOSH_DARWIN_START_ENV=present $Fixture 2>&1
$code = $LASTEXITCODE
$text = ($output -join "`n")
if ($code -ne 0) { throw "Darwin env E2E failed: exit=$code output=$text" }
if ($text -match 'DARLING_GUEST_EXCEPTION') {
    throw "Darwin env E2E entered the guest exception boundary: $text"
}
Write-Output 'DARWIN_START_ARGV_ENV_E2E=PASS EXIT=0'
