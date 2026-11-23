<# Original Wintosh ARM64 guest import fixture; GPL-3.0-only. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Linker,
	[Parameter(Mandatory = $true)][string]$Runner,
	[Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$clangPath = (Get-Item -LiteralPath $Clang).FullName
if ([System.IO.Path]::GetFileNameWithoutExtension($clangPath) -eq 'clang-cl') {
	$clangPath = Join-Path (Split-Path -Parent $clangPath) 'clang.exe'
}
if (-not (Test-Path -LiteralPath $clangPath)) { throw "clang.exe not found: $clangPath" }
$source = Join-Path $PSScriptRoot 'fixtures/arm64-import/main.c'
$object = Join-Path $OutputDirectory 'arm64-import.o'
$image = Join-Path $OutputDirectory 'arm64-import.macho'
$objcSource = Join-Path $PSScriptRoot 'fixtures/arm64-import/objc.m'
$objcObject = Join-Path $OutputDirectory 'arm64-objc-import.o'
$objcImage = Join-Path $OutputDirectory 'arm64-objc-import.macho'

& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 Mach-O import compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -e _main `
	-undefined dynamic_lookup -o $image $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 Mach-O import link failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 '-fobjc-runtime=macosx-11.0' `
	-fno-objc-arc -fno-stack-protector -O2 -c -x objective-c $objcSource `
	-o $objcObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 Objective-C fixture compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -e _main `
	-undefined dynamic_lookup -o $objcImage $objcObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 Objective-C fixture link failed: $LASTEXITCODE" }

$priorEager = [Environment]::GetEnvironmentVariable('WINTOSH_FORCE_EAGER_BIND', 'Process')
try {
	$env:WINTOSH_FORCE_EAGER_BIND = '1'
	$output = & $Runner --arch arm64 --startup-abi main $image 2>&1
	$exitCode = $LASTEXITCODE
	$text = $output -join "`n"
	$objcOutput = & $Runner --arch arm64 --startup-abi main $objcImage 2>&1
	$objcExitCode = $LASTEXITCODE
	$objcText = $objcOutput -join "`n"
} finally {
	if ($null -eq $priorEager) {
		Remove-Item Env:WINTOSH_FORCE_EAGER_BIND -ErrorAction SilentlyContinue
	} else {
		$env:WINTOSH_FORCE_EAGER_BIND = $priorEager
	}
}
if ($exitCode -ne 42) {
	throw "ARM64 strlen import expected exit 42, got $exitCode; output=$text"
}
if ($text -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)' -or
	$text -match 'no ABI adapter|unsupported SVC|unknown thunk') {
	throw "ARM64 import fixture reported an execution failure: $text"
}
if ($objcExitCode -ne 42) {
	throw "ARM64 Objective-C guest callback expected exit 42, got $objcExitCode; output=$objcText"
}
if ($objcText -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)' -or
	$objcText -match 'no ABI adapter|unsupported SVC|unknown thunk') {
	throw "ARM64 Objective-C fixture reported an execution failure: $objcText"
}
if ($objcText -notmatch 'ARM64_CXA_ATEXIT=PASS' -or
	$objcText -notmatch 'ARM64_ATEXIT=PASS' -or
	$objcText -notmatch 'ARM64_TERMINATOR=PASS' -or
	$objcText.IndexOf('ARM64_CXA_ATEXIT=PASS') -gt
	$objcText.IndexOf('ARM64_ATEXIT=PASS') -or
	$objcText.IndexOf('ARM64_ATEXIT=PASS') -gt
	$objcText.IndexOf('ARM64_TERMINATOR=PASS')) {
	throw "ARM64 atexit/terminator callbacks did not run: $objcText"
}
Write-Output 'ARM64_HOST_IMPORT_E2E=PASS IMPORT=_strlen EXIT=42 OBJC=objc_msgSend_scalar_pointer_stack_hfa_indirect_nested_heap_svc'
