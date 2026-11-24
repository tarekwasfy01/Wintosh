[CmdletBinding()]
param(
    [string]$Distribution = 'Ubuntu',
    [string]$BackupPath = ''
)

$ErrorActionPreference = 'Stop'

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'This step requires an elevated PowerShell. Open PowerShell with Run as administrator, then run this script again.'
    }

    $feature = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux
    if ($feature.State -ne 'Enabled') {
        $result = Enable-WindowsOptionalFeature -Online `
            -FeatureName Microsoft-Windows-Subsystem-Linux -NoRestart
        if ($result.RestartNeeded) {
            Write-Warning 'Windows needs a restart to enable WSL1. Restart, then run this script again to convert the distribution.'
            exit 3010
        }
    }

    $listing = (& wsl.exe --list --verbose 2>&1 | Out-String) -replace "`0", ''
    if ($LASTEXITCODE -ne 0) {
        throw "WSL could not list distributions after enabling its Windows feature.`n$listing"
    }
    $pattern = '^\s*\*?\s*' + [regex]::Escape($Distribution) + '\s+\S+\s+(?<Version>[12])\s*$'
    $line = $listing -split '\r?\n' | Where-Object { $_ -match $pattern } | Select-Object -First 1
    if (-not $line) {
        throw "Distribution '$Distribution' was not found. Install it first, then rerun this script."
    }
    $version = [regex]::Match($line, $pattern).Groups['Version'].Value
    if ($version -eq '1') {
        Write-Output "WSL1_READY=$Distribution"
        exit 0
    }

    if ($BackupPath) {
        $resolvedBackup = [System.IO.Path]::GetFullPath($BackupPath)
        if (Test-Path -LiteralPath $resolvedBackup) {
            throw "Backup target already exists: $resolvedBackup"
        }
        $backupDirectory = [System.IO.Path]::GetDirectoryName($resolvedBackup)
        if (-not (Test-Path -LiteralPath $backupDirectory)) {
            New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
        }
        & wsl.exe --export $Distribution $resolvedBackup
        if ($LASTEXITCODE -ne 0) {
            throw "WSL export failed with exit code $LASTEXITCODE; distribution conversion was not started."
        }
        Write-Output "WSL_BACKUP=$resolvedBackup"
    }

    & wsl.exe --set-version $Distribution 1
    if ($LASTEXITCODE -ne 0) {
        throw "WSL conversion failed with exit code $LASTEXITCODE. The distribution was not verified as WSL1."
    }

    $listing = (& wsl.exe --list --verbose 2>&1 | Out-String) -replace "`0", ''
    $line = $listing -split '\r?\n' | Where-Object { $_ -match $pattern } | Select-Object -First 1
    if (-not $line -or [regex]::Match($line, $pattern).Groups['Version'].Value -ne '1') {
        throw "WSL reported a successful conversion, but '$Distribution' did not verify as WSL1.`n$listing"
    }

    Write-Output "WSL1_READY=$Distribution"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 21
}
