<#
This file is part of the Wintosh original test fixture.
Copyright (C) 2026 Wintosh contributors
Licensed under the GNU General Public License, version 3.
#>

param(
    [Parameter(Mandatory = $true)][string]$Clang,
    [Parameter(Mandatory = $true)][string]$Source
)

$ErrorActionPreference = 'Stop'
if ((Split-Path -Leaf $Clang) -match 'clang-cl') {
    $clangExe = Join-Path (Split-Path -Parent $Clang) 'clang.exe'
    if (-not (Test-Path -LiteralPath $clangExe)) { throw "clang.exe not found beside $Clang" }
    & $clangExe -target x86_64-apple-darwin -fsyntax-only -x c $Source
} else {
    & $Clang -target x86_64-apple-darwin -fsyntax-only -x c $Source
}
if ($LASTEXITCODE -ne 0) {
    throw "hello.c syntax check failed: $LASTEXITCODE"
}
Write-Output 'HELLO_SOURCE=PASS'
