<# Original Wintosh verification script; GPL-3.0-only. #>
param(
    [Parameter(Mandatory = $true)][string]$Clang,
    [Parameter(Mandatory = $true)][string]$Linker,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$build = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot 'build-fixtures.ps1') -Clang $Clang `
    -Linker $Linker -OutputDirectory $OutputDirectory 2>&1
if ($LASTEXITCODE -ne 0) { throw ($build -join "`n") }
$fixture = Join-Path $OutputDirectory 'argv-env.macho'
$env:WINTOSH_FIXTURE = 'present'
$output = & $Runner --startup-abi main $fixture 2>&1
$code = $LASTEXITCODE
$env:WINTOSH_FIXTURE = $null
$text = $output -join "`n"
if ($code -ne 0 -or $text -notmatch 'fixture argv-env') {
    throw "argv-env Mach-O E2E failed: exit=$code output=$text"
}
Write-Output 'FIXTURE_ARGV_ENV_E2E=PASS'
