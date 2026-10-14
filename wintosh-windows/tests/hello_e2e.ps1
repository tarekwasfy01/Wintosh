<#
Wintosh original test fixture.
Copyright (C) 2026 Wintosh contributors
Licensed under the GNU General Public License, version 3.
#>

param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Clang,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Fixture
)

$ErrorActionPreference = 'Stop'
$sourceCheck = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot 'fixtures\hello\build-hello.ps1') `
    -Clang $Clang -Source $Source 2>&1
if ($LASTEXITCODE -ne 0) { throw ($sourceCheck -join "`n") }

if (Test-Path -LiteralPath $Fixture) { Remove-Item -LiteralPath $Fixture -Force }
& $Generator --emit-hello $Fixture
if ($LASTEXITCODE -ne 0) { throw "hello Mach-O fixture generation failed: $LASTEXITCODE" }

$withoutArgument = & $Runner --startup-abi main $Fixture 2>&1
$withoutCode = $LASTEXITCODE
$withoutText = $withoutArgument -join "`n"
if ($withoutCode -ne 0 -or $withoutText -notmatch 'hello from guest') {
    throw "hello no-argument run failed: exit=$withoutCode output=$withoutText"
}

$withArgument = & $Runner --startup-abi main $Fixture extra 2>&1
$withCode = $LASTEXITCODE
if ($withCode -ne 7) {
    throw "hello argument run failed: expected exit 7, got $withCode"
}

Write-Output 'WINTOSH_HELLO_E2E=PASS'
