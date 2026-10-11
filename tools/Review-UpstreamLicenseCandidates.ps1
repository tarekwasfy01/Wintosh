param([string]$Directory = (Join-Path $PSScriptRoot '..\licenses\upstream-candidates'))
$ErrorActionPreference = 'Stop'
$rows = foreach ($file in Get-ChildItem -LiteralPath $Directory -File) {
    if ($file.Name -in @('README.md','CANDIDATE-INVENTORY.csv')) { continue }
    $text = Get-Content -LiteralPath $file.FullName -Raw
    $families = @()
    if ($text -match '(?i)apache') { $families += 'Apache' }
    if ($text -match '(?i)gnu general public license|\bGPL\b') { $families += 'GPL' }
    if ($text -match '(?i)lesser general public license|\bLGPL\b') { $families += 'LGPL' }
    if ($text -match '(?i)\bMIT\b') { $families += 'MIT' }
    if ($text -match '(?i)\bBSD\b') { $families += 'BSD' }
    if ($text -match '(?i)\bMPL\b|mozilla public license') { $families += 'MPL' }
    if ($text -match '(?i)copyright|notice') { $families += 'copyright/notice' }
    [pscustomobject]@{
        file=$file.Name
        bytes=$file.Length
        detected_license=($families -join ';')
        multiple_license_markers=([int]($families.Count -gt 1))
        sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        review_status='manual-review-required'
    }
}
$output = Join-Path $Directory 'CANDIDATE-REVIEW.csv'
$rows | Export-Csv -LiteralPath $output -NoTypeInformation -Encoding UTF8
"CANDIDATE_FILES=$(@($rows).Count)"
"MANUAL_REVIEW_REQUIRED=$(@($rows | Where-Object review_status -eq 'manual-review-required').Count)"
"MULTIPLE_MARKER_FILES=$(@($rows | Where-Object multiple_license_markers -eq 1).Count)"
