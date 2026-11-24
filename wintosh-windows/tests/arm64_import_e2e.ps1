<# Original Wintosh ARM64 guest import fixture; GPL-3.0-only. #>
param(
	[Parameter(Mandatory = $true)][string]$Clang,
	[Parameter(Mandatory = $true)][string]$Linker,
	[Parameter(Mandatory = $true)][string]$Runner,
	[Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$dependencyDirectory = Join-Path $OutputDirectory 'Provider'
New-Item -ItemType Directory -Force -Path $dependencyDirectory | Out-Null
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
$fileMmapPath = Join-Path $OutputDirectory 'arm64-file-mmap.bin'
$dynamicSource = Join-Path $PSScriptRoot 'fixtures/arm64-import/dynamic.c'
$dynamicObject = Join-Path $OutputDirectory 'arm64-dynamic-import.o'
$dependencySource = Join-Path $PSScriptRoot 'fixtures/arm64-import/dynamic_dependency.c'
$dependencyObject = Join-Path $OutputDirectory 'arm64-dynamic-dependency.o'
$dependencyImage = Join-Path $dependencyDirectory 'libarm64-dynamic-dependency.dylib'
$nestedDependencySource = Join-Path $PSScriptRoot 'fixtures/arm64-import/dynamic_nested_dependency.c'
$nestedDependencyObject = Join-Path $OutputDirectory 'arm64-dynamic-nested-dependency.o'
$nestedDependencyImage = Join-Path $OutputDirectory 'libarm64-dynamic-nested-dependency.dylib'
$pluginSource = Join-Path $PSScriptRoot 'fixtures/arm64-import/dynamic_plugin.c'
$pluginObject = Join-Path $OutputDirectory 'arm64-dynamic-plugin.o'
$pluginDirectory = Join-Path $dependencyDirectory 'Plugins'
New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
$pluginImage = Join-Path $pluginDirectory 'libarm64-dynamic-plugin.dylib'
$dynamicObjcSource = Join-Path $PSScriptRoot 'fixtures/arm64-import/dynamic_objc.m'
$dynamicObjcObject = Join-Path $OutputDirectory 'arm64-dynamic-objc-import.o'
$dynamicImage = Join-Path $OutputDirectory 'libarm64-dynamic-import.dylib'

& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $source -o $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 Mach-O import compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -e _main `
	-undefined dynamic_lookup -o $image $object
if ($LASTEXITCODE -ne 0) { throw "ARM64 Mach-O import link failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 '-fobjc-runtime=macosx-11.0' `
	-fno-objc-arc -fno-exceptions -fno-objc-exceptions -fblocks `
	-fno-stack-protector -O2 `
	-c -x objective-c $objcSource `
	-o $objcObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 Objective-C fixture compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -e _main `
	-rpath '@executable_path' `
	-undefined dynamic_lookup -o $objcImage $objcObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 Objective-C fixture link failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $dynamicSource -o $dynamicObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 dynamic image compile failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $dependencySource -o $dependencyObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 dependency image compile failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $nestedDependencySource -o $nestedDependencyObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 nested dependency compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -dylib `
	-install_name '@rpath/libarm64-dynamic-nested-dependency.dylib' `
	-undefined dynamic_lookup -o $nestedDependencyImage $nestedDependencyObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 nested dependency link failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 -fno-stack-protector -O0 `
	-c -x c $pluginSource -o $pluginObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 dynamic plugin compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -dylib `
	-install_name '@rpath/libarm64-dynamic-plugin.dylib' `
	-undefined dynamic_lookup -o $pluginImage $pluginObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 dynamic plugin link failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -dylib `
	-install_name '@rpath/libarm64-dynamic-dependency.dylib' -undefined dynamic_lookup `
	-o $dependencyImage $dependencyObject $nestedDependencyImage
if ($LASTEXITCODE -ne 0) { throw "ARM64 dependency image link failed: $LASTEXITCODE" }
& $clangPath -target arm64-apple-macosx11.0 '-fobjc-runtime=macosx-11.0' `
	-fno-objc-arc -fno-stack-protector -O0 -c -x objective-c $dynamicObjcSource `
	-o $dynamicObjcObject
if ($LASTEXITCODE -ne 0) { throw "ARM64 dynamic Objective-C compile failed: $LASTEXITCODE" }
& $Linker -arch arm64 -platform_version macos 11.0 14.0 -dylib `
	-install_name '@rpath/libarm64-dynamic-import.dylib' `
	-rpath '@loader_path' -rpath '@loader_path/Provider' `
	-rpath '@loader_path/Provider/Plugins' `
	-undefined dynamic_lookup -o $dynamicImage $dynamicObject $dynamicObjcObject `
	$dependencyImage
if ($LASTEXITCODE -ne 0) { throw "ARM64 dynamic image link failed: $LASTEXITCODE" }

$priorEager = [Environment]::GetEnvironmentVariable('WINTOSH_FORCE_EAGER_BIND', 'Process')
Remove-Item -LiteralPath $fileMmapPath -Force -ErrorAction SilentlyContinue
try {
	$env:WINTOSH_FORCE_EAGER_BIND = '1'
	$output = & $Runner --arch arm64 --startup-abi main $image 2>&1
	$exitCode = $LASTEXITCODE
	$text = $output -join "`n"
	$objcOutput = & $Runner --arch arm64 --startup-abi main $objcImage `
		$dynamicImage $fileMmapPath 2>&1
	$objcExitCode = $LASTEXITCODE
	$objcText = $objcOutput -join "`n"
} finally {
	if ($null -eq $priorEager) {
		Remove-Item Env:WINTOSH_FORCE_EAGER_BIND -ErrorAction SilentlyContinue
	} else {
		$env:WINTOSH_FORCE_EAGER_BIND = $priorEager
	}
	Remove-Item -LiteralPath $fileMmapPath -Force -ErrorAction SilentlyContinue
}
if ($exitCode -ne 42) {
	throw "ARM64 strlen import expected exit 42, got $exitCode; output=$text"
}
if ($text -match 'DARLING_(GUEST_EXCEPTION|RUNNER_ERROR)' -or
	$text -match 'no ABI adapter|unsupported SVC|unknown thunk') {
	throw "ARM64 import fixture reported an execution failure: $text"
}
if ($text -notmatch 'ARM64_SVC_IDENTITY_DESCRIPTOR_PROCESS_RLIMIT_IOCTL=PASS') {
	throw "ARM64 identity/process/descriptor/resource-limit/ioctl SVC fixture did not report success: $text"
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
	$objcText -notmatch 'ARM64_DYLIB_INIT=PASS' -or
	$objcText -notmatch 'ARM64_DEPENDENCY_INIT=PASS' -or
	$objcText -notmatch 'ARM64_NESTED_DEPENDENCY_INIT=PASS' -or
	$objcText -notmatch 'ARM64_PLUGIN_INIT=PASS' -or
	$objcText -notmatch 'ARM64_DYLIB_OBJC_LOAD=PASS' -or
	$objcText -notmatch 'ARM64_DYLIB_CATEGORY_LOAD=PASS' -or
	$objcText -notmatch 'ARM64_DYLIB_CXA=PASS' -or
	$objcText -notmatch 'ARM64_DYLIB_MOD_TERM=PASS' -or
	$objcText -notmatch 'ARM64_DEPENDENCY_CXA=PASS' -or
	$objcText -notmatch 'ARM64_DEPENDENCY_MOD_TERM=PASS' -or
	$objcText -notmatch 'ARM64_NESTED_DEPENDENCY_CXA=PASS' -or
	$objcText -notmatch 'ARM64_NESTED_DEPENDENCY_MOD_TERM=PASS' -or
	$objcText -notmatch 'ARM64_PLUGIN_CXA=PASS' -or
	$objcText -notmatch 'ARM64_PLUGIN_MOD_TERM=PASS' -or
	$objcText.IndexOf('ARM64_NESTED_DEPENDENCY_INIT=PASS') -gt
	$objcText.IndexOf('ARM64_DEPENDENCY_INIT=PASS') -or
	$objcText.IndexOf('ARM64_PLUGIN_INIT=PASS') -gt
	$objcText.IndexOf('ARM64_PLUGIN_CXA=PASS') -or
	$objcText.IndexOf('ARM64_PLUGIN_CXA=PASS') -gt
	$objcText.IndexOf('ARM64_PLUGIN_MOD_TERM=PASS') -or
	$objcText.IndexOf('ARM64_DEPENDENCY_INIT=PASS') -gt
	$objcText.IndexOf('ARM64_DYLIB_INIT=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_INIT=PASS') -gt
	$objcText.IndexOf('ARM64_DYLIB_OBJC_LOAD=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_OBJC_LOAD=PASS') -gt
	$objcText.IndexOf('ARM64_DYLIB_CATEGORY_LOAD=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_CATEGORY_LOAD=PASS') -gt
	$objcText.IndexOf('ARM64_DYLIB_CXA=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_CXA=PASS') -gt
	$objcText.IndexOf('ARM64_DYLIB_MOD_TERM=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_MOD_TERM=PASS') -gt
	$objcText.IndexOf('ARM64_DEPENDENCY_CXA=PASS') -or
	$objcText.IndexOf('ARM64_DEPENDENCY_CXA=PASS') -gt
	$objcText.IndexOf('ARM64_DEPENDENCY_MOD_TERM=PASS') -or
	$objcText.IndexOf('ARM64_DYLIB_MOD_TERM=PASS') -gt
	$objcText.IndexOf('ARM64_NESTED_DEPENDENCY_CXA=PASS') -or
	$objcText.IndexOf('ARM64_NESTED_DEPENDENCY_CXA=PASS') -gt
	$objcText.IndexOf('ARM64_NESTED_DEPENDENCY_MOD_TERM=PASS') -or
	$objcText.IndexOf('ARM64_DEPENDENCY_MOD_TERM=PASS') -gt
	$objcText.IndexOf('ARM64_CXA_ATEXIT=PASS') -or
	$objcText.IndexOf('ARM64_CXA_ATEXIT=PASS') -gt
	$objcText.IndexOf('ARM64_ATEXIT=PASS') -or
	$objcText.IndexOf('ARM64_ATEXIT=PASS') -gt
	$objcText.IndexOf('ARM64_TERMINATOR=PASS')) {
	throw "ARM64 atexit/terminator callbacks did not run: $objcText"
}
$onceMarkers = @(
	'ARM64_DYLIB_INIT=PASS',
	'ARM64_DEPENDENCY_INIT=PASS',
	'ARM64_NESTED_DEPENDENCY_INIT=PASS',
	'ARM64_DYLIB_OBJC_LOAD=PASS',
	'ARM64_DYLIB_CATEGORY_LOAD=PASS',
	'ARM64_DYLIB_CXA=PASS',
	'ARM64_DYLIB_MOD_TERM=PASS',
	'ARM64_DEPENDENCY_CXA=PASS',
	'ARM64_DEPENDENCY_MOD_TERM=PASS',
	'ARM64_NESTED_DEPENDENCY_CXA=PASS',
	'ARM64_NESTED_DEPENDENCY_MOD_TERM=PASS'
)
foreach ($marker in $onceMarkers) {
	if ([regex]::Matches($objcText, [regex]::Escape($marker)).Count -ne 1) {
		throw "ARM64 repeated dlopen changed marker count for ${marker}: $objcText"
	}
}
foreach ($marker in @(
	'ARM64_PLUGIN_INIT=PASS',
	'ARM64_PLUGIN_CXA=PASS',
	'ARM64_PLUGIN_MOD_TERM=PASS'
)) {
	if ([regex]::Matches($objcText, [regex]::Escape($marker)).Count -ne 3) {
		throw "ARM64 caller-relative plugin load count mismatch for ${marker}: $objcText"
	}
}
Write-Output (
	'ARM64_HOST_IMPORT_E2E=PASS IMPORT=_strlen EXIT=42 ' +
	'OBJC=objc_msgSend_scalar_pointer_stack_hfa_indirect_nested_heap_svc ' +
	'MEMORY=anonymous_mmap_mprotect_munmap_MAP_FIXED_overlap ' +
	'FILE=open_write_shared_mmap_persist_private_cow_lseek_read_close')
