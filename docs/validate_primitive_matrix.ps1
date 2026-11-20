param(
    [string]$Path = (Join-Path $PSScriptRoot '..\PRIMITIVE-MATRIX.csv')
)

Add-Type -AssemblyName Microsoft.VisualBasic
$parser = [Microsoft.VisualBasic.FileIO.TextFieldParser]::new([IO.Path]::GetFullPath($Path))
$parser.SetDelimiters(',')
$parser.HasFieldsEnclosedInQuotes = $true
$expected = $parser.ReadFields().Count
$records = 0
$bad = @()
try {
    while (-not $parser.EndOfData) {
        $fields = $parser.ReadFields()
        $records++
        if ($fields.Count -ne $expected) {
            $bad += [pscustomobject]@{ Record = $records; Fields = $fields.Count }
        }
    }
} finally { $parser.Close() }
"MATRIX=$([IO.Path]::GetFullPath($Path))"
"EXPECTED_FIELDS=$expected"
"DATA_ROWS=$records"
"MALFORMED_ROWS=$($bad.Count)"
$bad | Format-Table -AutoSize
if ($bad.Count -gt 0) { exit 1 }
