<##
Wintosh original end-to-end fixture.
Copyright (C) 2026 Wintosh contributors
SPDX-License-Identifier: GPL-3.0-or-later
##>
param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Fixture) { Remove-Item -LiteralPath $Fixture -Force }
& $Generator --emit-cxa-atexit $Fixture
if ($LASTEXITCODE -ne 0) { throw "cxa_atexit fixture generation failed: $LASTEXITCODE" }
$output = & $Runner --startup-abi main $Fixture 2>&1
$code = $LASTEXITCODE
$text = $output -join "`n"
if ($code -ne 0 -or $text -notmatch 'guest atexit callback') {
    throw "guest cxa_atexit E2E failed: exit=$code output=$text"
}
Write-Output 'WINTOSH_GUEST_CXA_ATEXIT_E2E=PASS'
