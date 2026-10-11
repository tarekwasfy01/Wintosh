<#
This file is part of the Darling Windows port.

Copyright (C) 2026 Darling Windows port contributors

Licensed under the GNU General Public License, version 3.
#>

param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture
)

$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Fixture) {
    Remove-Item -LiteralPath $Fixture -Force
}

& $Generator --emit-printf $Fixture
if ($LASTEXITCODE -ne 0) { throw "Mach-O printf fixture generation failed: $LASTEXITCODE" }

$output = & $Runner --startup-abi main $Fixture 2>&1
$exitCode = $LASTEXITCODE
$text = ($output -join "`n")
Write-Output $text
if ($exitCode -ne 0) { throw "Mach-O printf runner failed: $exitCode" }
if ($text -notmatch 'mach-o printf 7') {
    throw 'Mach-O printf output was not observed'
}

Write-Output 'MACHO_PRINTF_E2E=PASS'
