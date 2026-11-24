<# Original Wintosh ARM64 Darwin guest fixture; GPL-3.0-only. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Linker,
	[Parameter(Mandatory = $true)][string]$Runner,
	[Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$clangPath = (Get-Item -LiteralPath $Clang).FullName
if ([System.IO.Path]::GetFileNameWithoutExtension($clangPath) -eq 'clang-cl') {
	$clangPath = Join-Path (Split-Path -Parent $clangPath) 'clang.exe'
}
if (-not (Test-Path -LiteralPath $clangPath)) { throw "clang.exe not found: $clangPath" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source = Join-Path $PSScriptRoot 'fixtures/arm64-import/corefoundation_source.c'
$object = Join-Path $OutputDirectory 'arm64-corefoundation-source.o'
$image = Join-Path $OutputDirectory 'arm64-corefoundation-source.macho'
& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 CoreFoundation guest compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -e _main `
	-undefined dynamic_lookup -o $image $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 CoreFoundation Mach-O link failed: $LASTEXITCODE" }
$priorEager = [Environment]::GetEnvironmentVariable('WINTOSH_FORCE_EAGER_BIND', 'Process')
try {
	$env:WINTOSH_FORCE_EAGER_BIND = '1'
	$output = & $Runner --arch arm64 --startup-abi main $image 2>&1
	$exitCode = $LASTEXITCODE
	$text = $output -join "`n"
} finally {
	if ($null -eq $priorEager) {
		Remove-Item Env:WINTOSH_FORCE_EAGER_BIND -ErrorAction SilentlyContinue
	} else {
		$env:WINTOSH_FORCE_EAGER_BIND = $priorEager
	}
}
if ($exitCode -ne 42) {
	throw "ARM64 CoreFoundation source callback E2E failed: exit=$exitCode output=$text"
}
if ($text -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)|no ABI adapter|unsupported SVC') {
	throw "ARM64 CoreFoundation source callback fixture reported an execution failure: $text"
}
Write-Output 'WINTOSH_ARM64_COREF_FOUNDATION_SOURCE_CALLBACKS_E2E=PASS'
