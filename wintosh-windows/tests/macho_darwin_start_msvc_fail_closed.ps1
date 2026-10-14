<# Original Wintosh verification script; GPL-3.0-only. #>
param(
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Fixture
)
$ErrorActionPreference = 'Stop'
& $Generator --emit-darwin $Fixture
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Fixture)) {
    throw 'could not create the Darwin-start fixture'
}
$ErrorActionPreference = 'Continue'
$output = & $Runner --startup-abi darwin $Fixture 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$text = $output -join "`n"
if ($code -eq 0 -or $text -notmatch 'real Mach-O execution requires a Clang build with System-V ABI support') {
    throw "MSVC Darwin-start path was not fail-closed: exit=$code output=$text"
}
Write-Output 'DARWIN_START_MSVC_FAIL_CLOSED=PASS'
