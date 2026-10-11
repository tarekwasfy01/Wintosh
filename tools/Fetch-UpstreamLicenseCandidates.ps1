param(
    [string]$Inventory = (Join-Path $PSScriptRoot '..\licenses\EXTERNAL-COMPONENT-PROVENANCE.csv'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\licenses\upstream-candidates')
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$headers = @{ 'User-Agent' = 'Wintosh-license-inventory/1.0' }
$names = @('LICENSE', 'LICENSE.txt', 'LICENSE.md', 'COPYING', 'COPYING.txt', 'NOTICE', 'NOTICE.txt', 'COPYRIGHT', 'COPYRIGHT.txt', 'LICENCE', 'LICENCE.txt', 'LICENCE.md')
$rows = Import-Csv -LiteralPath $Inventory
$results = @()

foreach ($row in $rows) {
    if ($row.license_status -ne 'review-required' -or $row.upstream_origin -notmatch '^https://github\.com/([^/]+)/([^/]+?)(?:\.git)?$') { continue }
    $owner = $Matches[1]
    $repo = $Matches[2]
    $found = $false
    $safe = ($row.component -replace '[^A-Za-z0-9_.-]', '_')
    $commit = if ($row.head_commit) { $row.head_commit } else { 'HEAD' }
    # Prefer the exact commit recorded in the provenance inventory. This keeps
    # the candidate text tied to the source revision that was actually used.
    foreach ($name in $names) {
        if ($found) { break }
        $uri = "https://raw.githubusercontent.com/$owner/$repo/$commit/$name"
        try {
            $response = Invoke-WebRequest -Uri $uri -Headers $headers -UseBasicParsing -TimeoutSec 8
            if ($response.StatusCode -eq 200 -and -not [string]::IsNullOrWhiteSpace($response.Content)) {
                $destination = Join-Path $OutputDirectory "$safe-$name"
                [System.IO.File]::WriteAllText($destination, $response.Content, [System.Text.Encoding]::UTF8)
                $results += [pscustomobject]@{ component=$row.component; status='candidate-found'; license_file=$destination; source_url=$uri; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash }
                $found = $true
            }
        } catch { }
    }
    try {
        if ($found) { continue }
        $api = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/license" -Headers $headers -TimeoutSec 10
        if ($api.download_url) {
            $response = Invoke-WebRequest -Uri $api.download_url -Headers $headers -UseBasicParsing -TimeoutSec 10
            if ($response.StatusCode -eq 200 -and -not [string]::IsNullOrWhiteSpace($response.Content)) {
                $name = [System.IO.Path]::GetFileName($api.path)
                $destination = Join-Path $OutputDirectory "$safe-$name"
                [System.IO.File]::WriteAllText($destination, $response.Content, [System.Text.Encoding]::UTF8)
                $results += [pscustomobject]@{ component=$row.component; status='candidate-found'; license_file=$destination; source_url=$api.download_url; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash }
                $found = $true
            }
        }
    } catch { }
    if (-not $found) {
        foreach ($branch in @('main', 'master')) {
            if ($found) { break }
            $uri = "https://raw.githubusercontent.com/$owner/$repo/$branch/LICENSE"
            try {
                $response = Invoke-WebRequest -Uri $uri -Headers $headers -UseBasicParsing -TimeoutSec 5
                if ($response.StatusCode -eq 200 -and -not [string]::IsNullOrWhiteSpace($response.Content)) {
                    $destination = Join-Path $OutputDirectory "$safe-LICENSE"
                    [System.IO.File]::WriteAllText($destination, $response.Content, [System.Text.Encoding]::UTF8)
                    $results += [pscustomobject]@{ component=$row.component; status='candidate-found'; license_file=$destination; source_url=$uri; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash }
                    $found = $true
                }
            } catch { }
        }
    }
    if (-not $found) { $results += [pscustomobject]@{ component=$row.component; status='not-found'; license_file=''; source_url=''; sha256='' } }
}
# Keep historical candidate files in the inventory even if a later network run
# is incomplete. The current run wins for a component when it found a file.
$inventoryPath = Join-Path $OutputDirectory 'CANDIDATE-INVENTORY.csv'
$historical = @()
if (Test-Path -LiteralPath $inventoryPath) { $historical = @(Import-Csv -LiteralPath $inventoryPath) }
$merged = @($historical + $results) | Group-Object component | ForEach-Object {
    $group = $_.Group
    $best = $group | Where-Object status -eq 'candidate-found' | Select-Object -Last 1
    if (-not $best) { $best = $group | Select-Object -Last 1 }
    $best
}
$merged | Sort-Object component | Export-Csv -LiteralPath $inventoryPath -NoTypeInformation -Encoding UTF8
"CANDIDATES_FOUND=$(@($results | Where-Object status -eq 'candidate-found').Count)"
"NOT_FOUND=$(@($results | Where-Object status -eq 'not-found').Count)"
"INVENTORY_COMPONENTS=$(@($merged).Count)"
