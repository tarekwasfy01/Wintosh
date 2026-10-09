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
$sourceFiles = @(Get-ChildItem (Join-Path $Root 'wintosh-windows') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.h', '.c', '.hpp' })
$missingNotices = @()
foreach ($sourceFile in $sourceFiles) {
    $head = (Get-Content -LiteralPath $sourceFile.FullName -TotalCount 14) -join "`n"
    if ($head -notmatch '(?i)GPL|license|copyright') {
        $missingNotices += $sourceFile.FullName
    }
}
Write-Output ("PROVENANCE_ROWS={0}" -f @($provenance).Count)
Write-Output ("LICENSE_INVENTORY_ROWS={0}" -f @($inventory).Count)
$statusCounts = @{}
foreach ($row in $provenance) {
    $status = [string]$row.license_status
    if (-not $statusCounts.ContainsKey($status)) { $statusCounts[$status] = 0 }
    $statusCounts[$status]++
}
foreach ($status in @('present', 'header-evidence', 'review-required')) {
    $count = if ($statusCounts.ContainsKey($status)) { $statusCounts[$status] } else { 0 }
    Write-Output ("STATUS_{0}={1}" -f $status.ToUpper().Replace('-', '_'), $count)
}
if (@($provenance).Count -ne 150 -or
    $statusCounts['present'] -ne 2 -or
    $statusCounts['header-evidence'] -ne 1 -or
    $statusCounts['review-required'] -ne 147) {
    Write-Output 'PROVENANCE_EXPECTATIONS=FAIL'
    exit 1
}
Write-Output ("PORT_SOURCE_FILES={0}" -f $sourceFiles.Count)
Write-Output ("PORT_SOURCE_FILES_WITHOUT_HEADER_NOTICE={0}" -f $missingNotices.Count)
foreach ($file in $missingNotices) { Write-Output ("MISSING_HEADER_NOTICE {0}" -f $file) }
if ($missingNotices.Count -ne 0) { exit 1 }
Write-Output 'LICENSE_PREFLIGHT=PASS'
exit 0
