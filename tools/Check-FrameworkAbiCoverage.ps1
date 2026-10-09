[CmdletBinding()]
param(
    [string]$Root = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
$frameworks = @(
    @('darling_windows_corefoundation.h', 'darling_windows_corefoundation.cpp'),
    @('darling_windows_foundation.h', 'darling_windows_foundation.cpp'),
    @('darling_windows_coregraphics.h', 'darling_windows_coregraphics.cpp')
)

$failed = $false
foreach ($pair in $frameworks) {
    $headerPath = Join-Path $Root ('wintosh-windows/frameworks/' + $pair[0])
    $sourcePath = Join-Path $Root ('wintosh-windows/frameworks/' + $pair[1])
    $header = Get-Content -LiteralPath $headerPath -Raw
    $source = Get-Content -LiteralPath $sourcePath -Raw
    $names = [regex]::Matches($header, 'darling_windows_[A-Za-z0-9_]+\s*\(') |
        ForEach-Object { $_.Value.TrimEnd('(').Trim() } |
        Sort-Object -Unique
    $missing = @($names | Where-Object {
        $source -notmatch [regex]::Escape($_ + '(')
    })
    Write-Output ("{0}: declared={1} missing={2}" -f $pair[0], $names.Count, $missing.Count)
    foreach ($name in $missing) {
        Write-Output ("  MISSING {0}" -f $name)
    }
    if ($missing.Count -ne 0) { $failed = $true }
}

if ($failed) { exit 1 }
exit 0
