<# Original Wintosh end-to-end fixture; GPL-3.0-only. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Linker,
	[Parameter(Mandatory = $true)][string]$Objdump,
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
$source = Join-Path $PSScriptRoot 'fixtures/fixture-ladder/lazy-import.c'
$object = Join-Path $OutputDirectory 'lazy-import.o'
$image = Join-Path $OutputDirectory 'lazy-import.macho'
& $clangPath -target x86_64-apple-darwin -fno-stack-protector -c -x c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "Mach-O import fixture compile failed: $LASTEXITCODE" }
& $Linker -arch x86_64 -platform_version macos 10.15 14.0 -e _main `
	-undefined dynamic_lookup -o $image $object
if ($LASTEXITCODE -ne 0) { throw "Mach-O import fixture link failed: $LASTEXITCODE" }
$metadata = & $Objdump --macho --lazy-bind --section-headers $image 2>&1
if ($LASTEXITCODE -ne 0) { throw "Mach-O lazy metadata inspection failed: $LASTEXITCODE" }
$metadataText = $metadata -join "`n"
foreach ($required in @('__stub_helper', '__la_symbol_ptr', '_exit', '_puts')) {
	if ($metadataText -notmatch [regex]::Escape($required)) {
		throw "Linker output lacks expected lazy-import evidence: $required"
	}
}
$priorForce = [Environment]::GetEnvironmentVariable('WINTOSH_FORCE_EAGER_BIND', 'Process')
$priorTrace = [Environment]::GetEnvironmentVariable('WINTOSH_TRACE_LAZY_BIND', 'Process')
try {
	Remove-Item Env:WINTOSH_FORCE_EAGER_BIND -ErrorAction SilentlyContinue
	$env:WINTOSH_TRACE_LAZY_BIND = '1'
	$stderrPath = Join-Path $OutputDirectory 'lazy-import.stderr.txt'
	$stdoutPath = Join-Path $OutputDirectory 'lazy-import.stdout.txt'
	$process = Start-Process -FilePath $Runner -ArgumentList @('--startup-abi',
		'main', "`"$image`"") -Wait -PassThru -NoNewWindow `
		-RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
	$code = $process.ExitCode
	$text = (@(Get-Content -LiteralPath $stdoutPath) +
		@(Get-Content -LiteralPath $stderrPath)) -join "`n"
} finally {
	if ($null -eq $priorForce) { Remove-Item Env:WINTOSH_FORCE_EAGER_BIND -ErrorAction SilentlyContinue }
	else { $env:WINTOSH_FORCE_EAGER_BIND = $priorForce }
	if ($null -eq $priorTrace) { Remove-Item Env:WINTOSH_TRACE_LAZY_BIND -ErrorAction SilentlyContinue }
	else { $env:WINTOSH_TRACE_LAZY_BIND = $priorTrace }
}
if ($code -ne 42) { throw "Compiler-linked Mach-O lazy-import run expected 42, got $code; output=$text" }
if ($text -notmatch 'compiler-linked Mach-O import' -or
	$text -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)' -or
	([regex]::Matches($text, 'WINTOSH_LAZY_BIND\b').Count -ne 2)) {
	throw "Compiler-linked Mach-O lazy-import output was invalid: $text"
}
Write-Output 'MACHO_REAL_IMPORT_LAZY_E2E=PASS LAZY_IMPORTS=2 EXIT=42'
