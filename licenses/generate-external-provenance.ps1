$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\darling-source\src\external')).Path
$output = Join-Path $PSScriptRoot 'EXTERNAL-COMPONENT-PROVENANCE.csv'
$manualEvidence = @{
    'libpthread_workqueue-0.8.2' = 'include/pthread_workqueue.h (header evidence; BSD-style terms)'
}
$rows = foreach ($directory in Get-ChildItem $root -Directory | Sort-Object Name) {
    $remote = ''
    $commit = ''
    $gitStatus = 'absent'
    if (Test-Path (Join-Path $directory.FullName '.git')) {
        $gitStatus = 'present'
        try {
            $remote = (& git -C $directory.FullName remote get-url origin 2>$null | Out-String).Trim()
            $commit = (& git -C $directory.FullName rev-parse HEAD 2>$null | Out-String).Trim()
        } catch {
            # A .git marker can be stale or point at an unavailable worktree.
            # Preserve the row and record the missing metadata conservatively.
            $remote = ''
            $commit = ''
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
