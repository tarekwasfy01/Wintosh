[CmdletBinding()]
param(
    [ValidateSet('Probe','Run','DarwinComputa')]
    [string]$Mode = 'Probe',
    [string]$Distribution = '',
    [string]$ComputaRoot = '',
    [string]$GuestMachO = '/usr/bin/sw_vers',
    [string]$DarlingCommand = 'true',
    [string[]]$DarlingArgument = @(),
    [switch]$AllowWsl2ForDevelopment
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
        $distributionList = (& wsl.exe --list --verbose 2>&1 | Out-String) -replace "`0", ''
        if ($LASTEXITCODE -ne 0) {
            throw "Could not list WSL distributions.`n$distributionList"
        }
        $distributionPattern = '^\s*\*?\s*' + [regex]::Escape($Distribution) + '\s+\S+\s+(?<Version>[12])\s*$'
        $distributionLine = $distributionList -split '\r?\n' |
            Where-Object { $_ -match $distributionPattern } |
            Select-Object -First 1
        if (-not $distributionLine) {
            throw "Could not determine the WSL version for distribution '$Distribution'."
        }
        $wslVersion = [regex]::Match($distributionLine, $distributionPattern).Groups['Version'].Value
        if ($wslVersion -ne '1' -and -not $AllowWsl2ForDevelopment) {
            throw "Distribution '$Distribution' is WSL $wslVersion. This runtime mode requires WSL1 to avoid WSL2 virtualization. Use -AllowWsl2ForDevelopment only for compatibility testing."
        }
        if ($wslVersion -eq '2' -and $AllowWsl2ForDevelopment) {
            Write-Warning 'Running under WSL2 for development; this does not satisfy the no-virtualization WSL1 target.'
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
