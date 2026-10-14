<# Original Wintosh verification script; GPL-3.0-only. #>
param(
    [Parameter(Mandatory = $true)][string]$Clang,
    [Parameter(Mandatory = $true)][string]$Linker,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$FixtureName,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [int]$ExpectedExit = 0
)
$ErrorActionPreference = 'Stop'
$build = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot 'build-fixtures.ps1') -Clang $Clang `
    -Linker $Linker -OutputDirectory $OutputDirectory 2>&1
if ($LASTEXITCODE -ne 0) { throw ($build -join "`n") }
$fixture = Join-Path $OutputDirectory ($FixtureName + '.macho')
$oldLocation = Get-Location
try {
    Set-Location $OutputDirectory
    $previousErrorAction = $ErrorActionPreference
    # The crash fixture deliberately writes its diagnostic on stderr while
    # returning the expected non-zero Darwin-compatible status.
    $ErrorActionPreference = 'Continue'
    $output = & $Runner --startup-abi main $fixture 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
} finally { Set-Location $oldLocation }
if ($code -ne $ExpectedExit) {
    throw "$FixtureName Mach-O E2E failed: exit=$code output=$($output -join "`n")"
}
Write-Output ("FIXTURE_{0}_E2E=PASS EXIT={1}" -f $FixtureName.ToUpperInvariant(), $code)
