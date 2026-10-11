param(
    [string]$Inventory = (Join-Path $PSScriptRoot '..\licenses\EXTERNAL-COMPONENT-PROVENANCE.csv'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\licenses\upstream-candidates')
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$headers = @{ 'User-Agent' = 'Wintosh-license-inventory/1.0' }
$names = @('LICENSE', 'LICENSE.txt', 'LICENSE.md', 'COPYING', 'COPYING.txt', 'NOTICE', 'NOTICE.txt')
$rows = Import-Csv -LiteralPath $Inventory
$results = @()

foreach ($row in $rows) {
    if ($row.license_status -ne 'review-required' -or $row.upstream_origin -notmatch '^https://github\.com/([^/]+)/([^/]+?)(?:\.git)?$') { continue }
    $owner = $Matches[1]
    $repo = $Matches[2]
    $found = $false
    try {
        $api = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/license" -Headers $headers -TimeoutSec 10
        if ($api.download_url) {
            $response = Invoke-WebRequest -Uri $api.download_url -Headers $headers -UseBasicParsing -TimeoutSec 10
            if ($response.StatusCode -eq 200 -and -not [string]::IsNullOrWhiteSpace($response.Content)) {
                $safe = ($row.component -replace '[^A-Za-z0-9_.-]', '_')
                $name = [System.IO.Path]::GetFileName($api.path)
                $destination = Join-Path $OutputDirectory "$safe-$name"
                [System.IO.File]::WriteAllText($destination, $response.Content, [System.Text.Encoding]::UTF8)
                $results += [pscustomobject]@{ component=$row.component; status='candidate-found'; license_file=$destination; source_url=$api.download_url; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash }
                $found = $true
            }
        }
    } catch { }
    if (-not $found) { $results += [pscustomobject]@{ component=$row.component; status='not-found'; license_file=''; source_url=''; sha256='' } }
}
$results | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'CANDIDATE-INVENTORY.csv') -NoTypeInformation -Encoding UTF8
"CANDIDATES_FOUND=$(@($results | Where-Object status -eq 'candidate-found').Count)"
"NOT_FOUND=$(@($results | Where-Object status -eq 'not-found').Count)"
