param(
    [string]$Path = (Join-Path $PSScriptRoot '..\PRIMITIVE-MATRIX.csv')
)

$header = (Get-Content -LiteralPath $Path -TotalCount 1)
$expected = ($header -split ',').Count
$lines = Get-Content -LiteralPath $Path | Select-Object -Skip 1
$bad = @()
$number = 2
foreach ($line in $lines) {
    $count = ($line -split ',').Count
    if ($count -ne $expected) {
        $bad += [pscustomobject]@{ Line = $number; Fields = $count }
    }
    $number++
}

"MATRIX=$([IO.Path]::GetFullPath($Path))"
"EXPECTED_FIELDS=$expected"
"DATA_ROWS=$($lines.Count)"
"MALFORMED_ROWS=$($bad.Count)"
$bad | Format-Table -AutoSize
if ($bad.Count -gt 0) { exit 1 }
