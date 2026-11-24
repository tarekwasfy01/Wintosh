[CmdletBinding()]
param(
    [ValidateSet('Probe','Run','DarwinComputa')]
    [string]$Mode = 'Probe',
    [string]$Distribution = '',
    [string]$ComputaRoot = '',
    [string]$GuestMachO = '/usr/bin/sw_vers',
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

    if ($Mode -eq 'DarwinComputa') {
        if (-not $Distribution) {
            throw 'DarwinComputa mode requires -Distribution.'
        }
        if (-not $ComputaRoot.StartsWith('/')) {
            throw '-ComputaRoot must be an absolute Linux path inside the selected WSL distribution.'
        }
        if (-not $GuestMachO.StartsWith('/')) {
            throw '-GuestMachO must be an absolute guest path such as /usr/bin/sw_vers.'
        }
        if ($DarlingArgument.Count -ne 0) {
            throw 'Darwin_Computa currently forwards only the executable path; upstream DSERVER_INIT does not yet forward argv.'
        }
        $runtime = $ComputaRoot.TrimEnd('/') + '/project/linux/Build/Release/boxedwine'
        $computaArguments = @('--distribution', $Distribution, '--exec', 'env',
            "DSERVER_INIT=$GuestMachO", $runtime, '--darwin-run', '/usr/bin/darlingserver')
        Invoke-WslChecked -Arguments $computaArguments
        exit 0
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
