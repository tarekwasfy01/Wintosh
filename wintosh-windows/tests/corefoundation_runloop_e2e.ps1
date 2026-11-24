<# Original Wintosh Darwin-ABI guest fixture; GPL-3.0-or-later. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Linker,
	[Parameter(Mandatory = $true)][string]$Runner,
	[Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$clangPath = (Get-Item -LiteralPath $Clang).FullName
if ([System.IO.Path]::GetFileNameWithoutExtension($clangPath) -eq 'clang-cl') {
	$candidate = Join-Path (Split-Path $clangPath) 'clang.exe'
	if (Test-Path -LiteralPath $candidate) { $clangPath = $candidate }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source = Join-Path $PSScriptRoot 'fixtures\corefoundation-runloop\runloop.c'
$object = Join-Path $OutputDirectory 'corefoundation-runloop.o'
$fixture = Join-Path $OutputDirectory 'corefoundation-runloop.macho'
& $clangPath -target x86_64-apple-darwin -c -x c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "CoreFoundation run-loop guest compile failed: $LASTEXITCODE" }
& $Linker -arch x86_64 -platform_version macos 10.15 14.0 -e _main `
	-undefined dynamic_lookup -o $fixture $object
if ($LASTEXITCODE -ne 0) { throw "CoreFoundation run-loop Mach-O link failed: $LASTEXITCODE" }
$output = & $Runner --startup-abi main $fixture 2>&1
$code = $LASTEXITCODE
$text = $output -join "`n"
if ($code -ne 0 -or $text -notmatch 'COREFOUNDATION_GUEST_OBSERVER=PASS' -or
	$text -notmatch 'COREFOUNDATION_GUEST_TIMER=PASS' -or
	$text -notmatch 'COREFOUNDATION_GUEST_SOURCE=PASS') {
	throw "CoreFoundation run-loop E2E failed: exit=$code output=$text"
}
Write-Output 'WINTOSH_COREF_FOUNDATION_GUEST_RUNLOOP_E2E=PASS'
