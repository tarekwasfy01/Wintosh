[CmdletBinding()]
param(
	[string]$Root,
	[string]$BuildRoot,
	[string]$SourceDirectory,
	[string]$DynarmicBuildDirectory,
	[string]$VcpkgInstalledDirectory,
	[string]$VcpkgRoot = $env:VCPKG_ROOT,
	[string]$Generator = 'Ninja',
	[switch]$Offline
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
	$Root = Split-Path -Parent $PSScriptRoot
}
$Root = (Resolve-Path -LiteralPath $Root).Path

if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
	$BuildRoot = Join-Path $Root 'build-dynarmic'
}
if (-not [System.IO.Path]::IsPathRooted($BuildRoot)) {
	$BuildRoot = Join-Path $Root $BuildRoot
}
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path

if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
	$SourceDirectory = Join-Path $BuildRoot 'source'
} elseif (-not [System.IO.Path]::IsPathRooted($SourceDirectory)) {
	$SourceDirectory = Join-Path $Root $SourceDirectory
}
if ([string]::IsNullOrWhiteSpace($DynarmicBuildDirectory)) {
	$DynarmicBuildDirectory = Join-Path $BuildRoot 'build'
} elseif (-not [System.IO.Path]::IsPathRooted($DynarmicBuildDirectory)) {
	$DynarmicBuildDirectory = Join-Path $Root $DynarmicBuildDirectory
}

$repository = 'https://github.com/yuzu-mirror/dynarmic.git'
$commit = '9d4582339990d4eae53f1dc7160686920fc2075c'
$vcpkgManifest = Join-Path $PSScriptRoot 'dynarmic'
if ([string]::IsNullOrWhiteSpace($VcpkgInstalledDirectory)) {
	$VcpkgInstalledDirectory = Join-Path $BuildRoot 'vcpkg_installed'
} elseif (-not [System.IO.Path]::IsPathRooted($VcpkgInstalledDirectory)) {
	$VcpkgInstalledDirectory = Join-Path $Root $VcpkgInstalledDirectory
}

if (-not $Offline -and [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
	$installedVcpkg = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
	if ($null -eq $installedVcpkg) {
		throw 'Set VCPKG_ROOT or install vcpkg.exe and make it available on PATH.'
	}
	$VcpkgRoot = Split-Path -Parent $installedVcpkg.Source
}
$toolchain = $null
if (-not $Offline) {
	$toolchain = Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'
	if (-not (Test-Path -LiteralPath $toolchain)) {
		throw "vcpkg toolchain file not found: $toolchain"
	}
}
if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
	throw 'git.exe is required.'
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
	throw 'cmake.exe is required.'
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	throw 'Run this script from a Visual Studio Developer PowerShell with cl.exe on PATH.'
}
if ($Generator -eq 'Ninja' -and -not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
	throw 'Ninja is required for the default generator. Install it or pass a Visual Studio generator.'
}

if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory '.git'))) {
	if (Test-Path -LiteralPath $SourceDirectory) {
		$entries = @(Get-ChildItem -LiteralPath $SourceDirectory -Force)
		if ($entries.Count -ne 0) {
			throw "Source directory is not empty and has no Git checkout: $SourceDirectory"
		}
	} else {
		$parent = Split-Path -Parent $SourceDirectory
		New-Item -ItemType Directory -Force -Path $parent | Out-Null
	}
	& git.exe clone --filter=blob:none --no-checkout $repository $SourceDirectory
	if ($LASTEXITCODE -ne 0) { throw "Dynarmic clone failed: $LASTEXITCODE" }
	& git.exe -C $SourceDirectory checkout --detach $commit
	if ($LASTEXITCODE -ne 0) { throw "Dynarmic checkout failed: $LASTEXITCODE" }
}

$actualCommit = (& git.exe -C $SourceDirectory rev-parse HEAD).Trim()
if ($actualCommit -ne $commit) {
	throw "Dynarmic source is at $actualCommit; expected pinned commit $commit."
}

# Dynarmic's pinned MSVC flags request external PDB generation (/Zi, /Zo).
# This environment's Ninja PDB manager rejects those PDBs; embed debug records
# in the objects instead. The transformation is limited to the two compiled
# CMake flag lists and is idempotent for an existing prepared checkout.
foreach ($relativePath in @('CMakeLists.txt', 'externals/mcl/CMakeLists.txt')) {
	$path = Join-Path $SourceDirectory $relativePath
	$content = [System.IO.File]::ReadAllText($path)
	$originalContent = $content
	if ($content -match '(?m)^[\t ]*/Zi[\t ]*\r?$') {
		$content = [System.Text.RegularExpressions.Regex]::Replace(
			$content, '(?m)^([\t ]*)/Zi([\t ]*\r?)$', '${1}/Z7${2}')
	}
	if ($content -match '(?m)^[\t ]*/Zo[\t ]*\r?$') {
		$content = [System.Text.RegularExpressions.Regex]::Replace(
			$content, '(?m)^[\t ]*/Zo[\t ]*\r?\n', '')
	}
	if ($content -match '(?m)^[\t ]*/Zi[\t ]*\r?$' -or
		$content -match '(?m)^[\t ]*/Zo[\t ]*\r?$') {
		throw "Could not normalize MSVC debug flags in $relativePath"
	}
	if ($content -ne $originalContent) {
		[System.IO.File]::WriteAllText($path, $content,
			[System.Text.UTF8Encoding]::new($false))
	}
}

$cmakeArguments = @(
	'-S', $SourceDirectory,
	'-B', $DynarmicBuildDirectory,
	'-G', $Generator,
	'-DCMAKE_BUILD_TYPE=Release',
	'-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
	'-DCMAKE_C_FLAGS_DEBUG=/Z7',
	'-DCMAKE_CXX_FLAGS_DEBUG=/Z7',
	'-DVCPKG_TARGET_TRIPLET=x64-windows',
	'-DDYNARMIC_FRONTENDS=A64',
	'-DDYNARMIC_TESTS=OFF',
	'-DDYNARMIC_USE_LLVM=OFF',
	'-DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF',
	'-DDYNARMIC_USE_BUNDLED_EXTERNALS=ON'
)
if ($Offline) {
	$boostIncludes = Join-Path $VcpkgInstalledDirectory 'x64-windows/include'
	if (-not (Test-Path -LiteralPath (Join-Path $boostIncludes 'boost/version.hpp'))) {
		throw "Offline Boost headers not found in installed package tree: $boostIncludes"
	}
	$cmakeArguments += @(
		'-DBoost_NO_BOOST_CMAKE=ON',
		"-DBoost_INCLUDE_DIR=$boostIncludes"
	)
} else {
	$cmakeArguments += @(
		"-DCMAKE_TOOLCHAIN_FILE=$toolchain",
		"-DVCPKG_MANIFEST_DIR=$vcpkgManifest",
		"-DVCPKG_INSTALLED_DIR=$VcpkgInstalledDirectory",
		'-DVCPKG_TARGET_TRIPLET=x64-windows'
	)
}
& cmake.exe @cmakeArguments
if ($LASTEXITCODE -ne 0) { throw "Dynarmic CMake configure failed: $LASTEXITCODE" }

& cmake.exe --build $DynarmicBuildDirectory --config Release --target dynarmic --parallel
if ($LASTEXITCODE -ne 0) { throw "Dynarmic build failed: $LASTEXITCODE" }

Write-Output "DYNARMIC_SOURCE=$SourceDirectory"
Write-Output "DYNARMIC_BUILD=$DynarmicBuildDirectory"
Write-Output "DYNARMIC_COMMIT=$commit"
Write-Output 'WINTOSH_CONFIGURE_ARGS:'
Write-Output "  -DWINTOSH_DYNARMIC_SOURCE_DIR=$SourceDirectory"
Write-Output "  -DWINTOSH_DYNARMIC_BUILD_DIR=$DynarmicBuildDirectory"
