<#
Scans a Darling/Mach source tree for MIG definition files and emits a small,
provenance-friendly inventory. It does not copy or transform upstream code.
Wintosh original tooling: MIT; upstream files retain their own licenses.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$OutputCsv
)

$root = (Resolve-Path -LiteralPath $SourceRoot).Path
$rows = foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File -Include *.defs,*.def,*.mig) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -ErrorAction Stop
    $subsystems = [regex]::Matches($text, '(?im)^\s*subsystem\s+([A-Za-z_][A-Za-z0-9_]*)') |
        ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
    $routines = [regex]::Matches($text, '(?im)^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*;') |
        ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
    $relative = $file.FullName.Substring($root.Length) -replace '^[\\/]+', ''
    [pscustomobject]@{
        RelativePath = $relative
        Extension = $file.Extension.ToLowerInvariant()
        Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        Subsystems = ($subsystems -join ';')
        RoutineCount = @($routines).Count
        Routines = ($routines -join ';')
        LicenseReview = 'REVIEW_UPSTREAM_HEADER'
    }
}

$parent = Split-Path -Parent $OutputCsv
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
@($rows) | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding UTF8
Write-Output ("MIG_DEFINITION_FILES={0}" -f @($rows).Count)
