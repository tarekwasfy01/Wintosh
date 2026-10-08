[CmdletBinding()]
param(
    [ValidateSet('Probe','Run')]
    [string]$Mode = 'Probe',
    [string]$Distribution = '',
    [string]$DarlingCommand = 'true',
    [string[]]$DarlingArgument = @()
)

$ErrorActionPreference = 'Stop'

function Invoke-WslChecked {
    param([string[]]$Arguments)
    & wsl.exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "WSL invocation failed with exit code $LASTEXITCODE."
    }
}

try {
    $status = & wsl.exe --status 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        [Console]::Error.WriteLine("WSL is unavailable or access was denied.`n$status")
        exit 20
    }

    if ($Mode -eq 'Probe') {
        Write-Output 'DARLING_WSL_BACKEND=AVAILABLE'
        & wsl.exe -l -v
        exit $LASTEXITCODE
    }

    $args = @()
    if ($Distribution) { $args += @('--distribution', $Distribution) }
    $args += @('--', $DarlingCommand)
    $args += $DarlingArgument
    Invoke-WslChecked -Arguments $args
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 21
}
