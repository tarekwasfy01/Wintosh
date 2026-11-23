<# Wintosh-original E2E check; GPL-3.0-only. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Python,
	[Parameter(Mandatory = $true)][string]$Runner,
	[Parameter(Mandatory = $true)][string]$RepositoryRoot,
	[Parameter(Mandatory = $true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
$source = Join-Path $RepositoryRoot 'wintosh-windows/tests/fixtures/i386/tlv_guest.c'
$object = Join-Path $WorkDirectory 'tlv_guest.o'
$image = Join-Path $WorkDirectory 'tlv_guest.macho'
$linker = Join-Path $RepositoryRoot 'tools/link_i386_macho.py'
& $Clang -target i386-apple-macosx10.13 -c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "i386 TLV fixture compilation failed: $LASTEXITCODE" }
& $Python $linker $object -o $image
if ($LASTEXITCODE -ne 0) { throw "i386 TLV fixture linking failed: $LASTEXITCODE" }
$output = & $Runner $image 2>&1
$code = $LASTEXITCODE
$text = ($output -join "`n")
if ($code -ne 42) { throw "i386 TLV E2E expected exit=42, got exit=$code output=$text" }
if ($text -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)') {
	throw "i386 TLV E2E reported a runner failure: $text"
}
Write-Output 'I386_TLV_E2E=PASS EXIT=42'
