[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Distribution,
    [Parameter(Mandatory)]
    [string]$ComputaRoot,
    [switch]$AllowWsl2ForDevelopment
)

$ErrorActionPreference = 'Stop'

function Invoke-WslChecked {
    param([string[]]$Arguments)
    & wsl.exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed with exit code $LASTEXITCODE."
    }
}

try {
    if (-not $ComputaRoot.StartsWith('/')) {
        throw '-ComputaRoot must be an absolute Linux path inside the selected WSL distribution.'
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
        throw "Distribution '$Distribution' is WSL $wslVersion. Building inside WSL1 is required for the no-virtualization target; use -AllowWsl2ForDevelopment only for development testing."
    }
    if ($wslVersion -eq '2' -and $AllowWsl2ForDevelopment) {
        Write-Warning 'Building under WSL2 for development; this does not meet the no-virtualization target.'
    }

    $linuxProject = $ComputaRoot.TrimEnd('/') + '/project/linux'
    $runtime = $linuxProject + '/Build/Release/boxedwine'
    $preflight = @(
        '--distribution', $Distribution, '--exec', 'sh', '-lc',
        'command -v make >/dev/null && command -v g++ >/dev/null && pkg-config --exists sdl2 openssl libcurl minizip glu'
    )
    & wsl.exe @preflight
    if ($LASTEXITCODE -ne 0) {
        throw 'Build prerequisites are missing in the distribution. Install build-essential, libsdl2-dev, libssl-dev, libcurl4-openssl-dev, libminizip-dev, libgl-dev and libglu1-mesa-dev.'
    }

    $flags = '-DASMJIT_NO_AARCH64 -DBOXEDWINE_64 -DBOXEDWINE_JIT -DBOXEDWINE_JIT_X64 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_MEM_CACHE -DBOXEDWINE_HOST_EXCEPTIONS -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_DARWIN=1'
    $makeFlags = 'EXTRA_CPP_FLAGS=' + $flags
    Invoke-WslChecked -Arguments @('--distribution', $Distribution, '--exec', 'make', '-C', $linuxProject, 'release', $makeFlags)

    Invoke-WslChecked -Arguments @('--distribution', $Distribution, '--exec', $runtime, '--x64-selftest')
    Invoke-WslChecked -Arguments @('--distribution', $Distribution, '--exec', $runtime, '--darwin-selftest')
    Write-Output "DARWIN_COMPUTA_BUILD=PASS"
    Write-Output "DARWIN_COMPUTA_RUNTIME=$runtime"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 21
}
