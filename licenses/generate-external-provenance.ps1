param([switch]$ResolveRemoteRefs, [int]$Limit = 0, [int]$Skip = 0, [string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
$gitExe = 'C:\Program Files\Git\cmd\git.exe'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\darling-source\src\external')).Path
$output = if ($OutputPath) { (Resolve-Path (Join-Path (Get-Location) $OutputPath) -ErrorAction SilentlyContinue).Path } else { Join-Path $PSScriptRoot 'EXTERNAL-COMPONENT-PROVENANCE.csv' }
if (!$output) { $output = Join-Path (Get-Location) $OutputPath }
$previous = @{}
if (Test-Path $output) {
    foreach ($row in (Import-Csv $output)) { $previous[$row.component] = $row.upstream_origin }
}
$moduleOrigins = @{}
$gitmodules = Join-Path (Join-Path $root '..\..') '.gitmodules'
if (Test-Path $gitmodules) {
    $modulePath = ''
    foreach ($line in Get-Content $gitmodules) {
        if ($line -match '^\s*path\s*=\s*src/external/(.+)$') { $modulePath = $Matches[1] }
        elseif ($modulePath -and $line -match '^\s*url\s*=\s*(.+)$') {
            $url = $Matches[1].Trim()
            if ($url -match '^\.\./(.+)$') { $url = 'https://github.com/darlinghq/' + $Matches[1] }
            $moduleOrigins[$modulePath] = $url
            $modulePath = ''
        }
    }
}
$manualEvidence = @{
    'libpthread_workqueue-0.8.2' = 'include/pthread_workqueue.h (header evidence; BSD-style terms)'
}
$directories = @(Get-ChildItem $root -Directory | Sort-Object Name)
if ($Skip -gt 0) { $directories = @($directories | Select-Object -Skip $Skip) }
if ($Limit -gt 0) { $directories = @($directories | Select-Object -First $Limit) }
$rows = foreach ($directory in $directories) {
    $remote = ''
    $commit = ''
    $gitStatus = 'absent'
    if (Test-Path (Join-Path $directory.FullName '.git')) {
        try {
            $commitCandidate = (& $gitExe -C $directory.FullName rev-parse --verify HEAD 2>$null | Out-String).Trim()
            if ($LASTEXITCODE -eq 0 -and $commitCandidate) {
                $gitStatus = 'present'
                $commit = $commitCandidate
                $remote = (& $gitExe -C $directory.FullName remote get-url origin 2>$null | Out-String).Trim()
            } else {
                $gitStatus = 'invalid-marker'
                if ($previous.ContainsKey($directory.Name)) { $remote = $previous[$directory.Name] }
                if (!$remote -and $moduleOrigins.ContainsKey($directory.Name)) { $remote = $moduleOrigins[$directory.Name] }
            }
        } catch {
            $gitStatus = 'invalid-marker'
            if ($previous.ContainsKey($directory.Name)) { $remote = $previous[$directory.Name] }
            if (!$remote -and $moduleOrigins.ContainsKey($directory.Name)) { $remote = $moduleOrigins[$directory.Name] }
        }
        if ($ResolveRemoteRefs -and $remote) {
            $remoteHead = (& $gitExe ls-remote --symref $remote HEAD 2>$null)
            if ($LASTEXITCODE -eq 0) {
                $headLine = @($remoteHead | Where-Object { $_ -match '^([0-9a-f]{40})\s+HEAD$' }) | Select-Object -First 1
                if ($headLine -and $headLine -match '^([0-9a-f]{40})\s+HEAD$') { $commit = $Matches[1] }
                $refLine = @($remoteHead | Where-Object { $_ -match '^ref:\s+refs/heads/([^\s]+)\s+HEAD$' }) | Select-Object -First 1
                if ($refLine -and $refLine -match '^ref:\s+refs/heads/([^\s]+)\s+HEAD$') { $gitStatus = "origin-only:$($Matches[1])" }
            }
        }
    }
    $licenseFiles = @(Get-ChildItem $directory.FullName -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'LICENSE|LICENCE|COPYING|NOTICE' } |
        Sort-Object FullName)
    [pscustomobject]@{
        component = $directory.Name
        git_metadata = $gitStatus
        upstream_origin = if ($remote) { $remote.Trim() } else { '' }
        head_commit = if ($commit) { $commit.Trim() } else { '' }
        license_file = if ($licenseFiles.Count -gt 0) {
            (($licenseFiles | ForEach-Object { $_.FullName.Substring($directory.FullName.Length + 1) }) -join ';')
        } elseif ($manualEvidence.ContainsKey($directory.Name)) { $manualEvidence[$directory.Name] } else { '' }
        license_status = if ($licenseFiles.Count -gt 0) { 'present' } elseif ($manualEvidence.ContainsKey($directory.Name)) { 'header-evidence' } else { 'review-required' }
    }
}
$rows | Export-Csv -LiteralPath $output -NoTypeInformation -Encoding UTF8
Write-Output "WROTE=$output COMPONENTS=$($rows.Count)"
