[CmdletBinding()]
param([string]$Root = (Join-Path $PSScriptRoot '..'))

$ErrorActionPreference = 'Stop'
$licenseDir = Join-Path $Root 'licenses'
$required = @(
    'LICENSE-darling-GPL-3.0.txt',
    'LICENSE-wsl-MIT.txt',
    'LICENSE-ipasim-MIT.txt',
    'LICENSE-sherlockelf-MIT.txt',
    'EXTERNAL-COMPONENT-PROVENANCE.csv',
    'SOURCE-LICENSE-INVENTORY.csv'
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $licenseDir $_)) })
foreach ($file in $required) {
    $path = Join-Path $licenseDir $file
    if (Test-Path -LiteralPath $path) { Write-Output ("PRESENT {0}" -f $file) }
}
if ($missing.Count -ne 0) {
    $missing | ForEach-Object { Write-Output ("MISSING {0}" -f $_) }
    exit 1
}
$provenance = Import-Csv (Join-Path $licenseDir 'EXTERNAL-COMPONENT-PROVENANCE.csv')
$inventory = Import-Csv (Join-Path $licenseDir 'SOURCE-LICENSE-INVENTORY.csv')
Write-Output ("PROVENANCE_ROWS={0}" -f @($provenance).Count)
Write-Output ("LICENSE_INVENTORY_ROWS={0}" -f @($inventory).Count)
Write-Output 'LICENSE_PREFLIGHT=PASS'
exit 0
