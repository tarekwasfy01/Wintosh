[CmdletBinding()]
param(
    [ValidateSet('Probe','Run','DarwinComputa')]
    [string]$Mode = 'Probe',
    [string]$Distribution = '',
    [string]$ComputaRoot = '',
    [string]$GuestMachO = '/usr/bin/sw_vers',
    [string]$WindowsAppPath = '',
    [string]$GuestBundleExecutable = '',
    [string]$DarlingCommand = 'true',
    [string[]]$DarlingArgument = @(),
    [switch]$AllowWsl2ForDevelopment
)

$ErrorActionPreference = 'Stop'
$stagedAppRoot = ''
$stagedAppsRoot = ''

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
        if (-not $WindowsAppPath -and -not $GuestMachO.StartsWith('/')) {
            throw '-GuestMachO must be an absolute guest path such as /usr/bin/sw_vers.'
        }
        if ($WindowsAppPath) {
            $resolvedAppPath = (Resolve-Path -LiteralPath $WindowsAppPath).Path
            if (-not (Test-Path -LiteralPath $resolvedAppPath -PathType Container) -or
                -not $resolvedAppPath.EndsWith('.app', [StringComparison]::OrdinalIgnoreCase)) {
                throw '-WindowsAppPath must point to an existing .app directory.'
            }

            $linuxHome = (& wsl.exe --distribution $Distribution --exec sh -lc 'printf "%s" "$HOME"' 2>&1 | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or -not $linuxHome.StartsWith('/')) {
                throw "Could not determine the home directory in WSL.`n$linuxHome"
            }
            $linuxAppPath = (& wsl.exe --distribution $Distribution --exec wslpath -u $resolvedAppPath 2>&1 | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or -not $linuxAppPath.StartsWith('/')) {
                throw "Could not map the Windows app bundle into WSL.`n$linuxAppPath"
            }

            $bundleName = Split-Path -Leaf $resolvedAppPath
            if (-not $GuestBundleExecutable) {
                $infoPlistPath = Join-Path $resolvedAppPath 'Contents\Info.plist'
                if (Test-Path -LiteralPath $infoPlistPath -PathType Leaf) {
                    $linuxInfoPlist = "$linuxAppPath/Contents/Info.plist"
                    $plistReaderWindowsPath = Join-Path $PSScriptRoot 'read-bundle-executable.py'
                    $plistReaderPath = (& wsl.exe --distribution $Distribution --exec wslpath -u $plistReaderWindowsPath 2>&1 | Out-String).Trim()
                    if ($LASTEXITCODE -ne 0 -or -not $plistReaderPath.StartsWith('/')) {
                        throw "Could not map the Info.plist reader into WSL.`n$plistReaderPath"
                    }
                    $GuestBundleExecutable = (& wsl.exe --distribution $Distribution --exec python3 $plistReaderPath $linuxInfoPlist 2>&1 | Out-String).Trim()
                    if ($LASTEXITCODE -ne 0 -or -not $GuestBundleExecutable) {
                        throw "Could not read CFBundleExecutable from $infoPlistPath. Install python3 in WSL or pass -GuestBundleExecutable explicitly.`n$GuestBundleExecutable"
                    }
                } else {
                    $GuestBundleExecutable = [IO.Path]::GetFileNameWithoutExtension($bundleName)
                }
            }
            if ($GuestBundleExecutable.Contains('/') -or $GuestBundleExecutable.Contains([char]92)) {
                throw '-GuestBundleExecutable must be a filename, not a path.'
            }
            $guestBundleBinary = Join-Path $resolvedAppPath ("Contents\MacOS\" + $GuestBundleExecutable)
            if (-not (Test-Path -LiteralPath $guestBundleBinary -PathType Leaf)) {
                throw "Bundle executable was not found: $guestBundleBinary"
            }

            $stageToken = 'Wintosh-' + [guid]::NewGuid().ToString('N')
            $stagedAppsRoot = "$linuxHome/.local/share/Boxedwine/rootfs-darling/root/usr/libexec/darling/Applications"
            $stagedAppRoot = "$stagedAppsRoot/$stageToken"
            $stagedBundlePath = "$stagedAppRoot/$bundleName"
            & wsl.exe --distribution $Distribution --exec mkdir -p $stagedAppRoot
            if ($LASTEXITCODE -ne 0) {
                throw 'Could not create a temporary Darling Applications staging directory.'
            }
            & wsl.exe --distribution $Distribution --exec cp -a $linuxAppPath $stagedAppRoot
            if ($LASTEXITCODE -ne 0) {
                throw 'Could not copy the Windows .app bundle into the Darling rootfs.'
            }
            $stagedBinaryPath = "$stagedBundlePath/Contents/MacOS/$GuestBundleExecutable"
            & wsl.exe --distribution $Distribution --exec chmod +x $stagedBinaryPath
            if ($LASTEXITCODE -ne 0) {
                throw 'Could not mark the staged bundle executable as executable in WSL.'
            }
            & wsl.exe --distribution $Distribution --exec test -x $stagedBinaryPath
            if ($LASTEXITCODE -ne 0) {
                throw "Staged bundle executable is not runnable: $stagedBinaryPath"
            }
            $GuestMachO = "/Applications/$stageToken/$bundleName/Contents/MacOS/$GuestBundleExecutable"
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
        $helperWindowsPath = Join-Path $PSScriptRoot 'run-darwin-computa-once.sh'
        $helperPath = (& wsl.exe --distribution $Distribution --exec wslpath -u $helperWindowsPath 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $helperPath.StartsWith('/')) {
            throw "Could not map the WintOS Darling helper into the WSL filesystem.`n$helperPath"
        }
        $linuxHomeForRootfs = (& wsl.exe --distribution $Distribution --exec sh -lc 'printf "%s" "$HOME"' 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $linuxHomeForRootfs.StartsWith('/')) {
            throw "Could not determine the WSL home directory for Darling's rootfs.`n$linuxHomeForRootfs"
        }
        $materializerWindowsPath = Join-Path $PSScriptRoot 'materialize-darling-executable.py'
        $materializerPath = (& wsl.exe --distribution $Distribution --exec wslpath -u $materializerWindowsPath 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $materializerPath.StartsWith('/')) {
            throw "Could not map the Darling symlink materializer into WSL.`n$materializerPath"
        }
        $darlingZip = "$linuxHomeForRootfs/.local/share/Boxedwine/rootfs-darling/dist/darling.zip"
        $darlingOverlay = "$linuxHomeForRootfs/.local/share/Boxedwine/rootfs-darling/root"
        & wsl.exe --distribution $Distribution --exec python3 $materializerPath $darlingZip $darlingOverlay $GuestMachO
        if ($LASTEXITCODE -ne 0) {
            throw "Could not prepare the Darling guest executable path (exit code $LASTEXITCODE)."
        }
        $computaArguments = @('--distribution', $Distribution, '--exec', 'bash',
            $helperPath, $ComputaRoot, $GuestMachO)
        $computaOutput = & wsl.exe @computaArguments
        $computaStatus = $LASTEXITCODE
        if ($computaStatus -ne 0) {
            throw "Darwin_Computa supervisor failed with exit code $computaStatus.`n$($computaOutput -join [Environment]::NewLine)"
        }
        $guestExitLine = $computaOutput | Where-Object { $_ -match '^WINTOSH_DARWIN_GUEST_EXIT=(-?\d+)$' } | Select-Object -Last 1
        if (-not $guestExitLine) {
            throw "Darwin_Computa finished without reporting a guest exit status.`n$($computaOutput -join [Environment]::NewLine)"
        }
        $computaOutput | Where-Object { $_ -notmatch '^WINTOSH_DARWIN_GUEST_EXIT=-?\d+$' } | ForEach-Object { Write-Output $_ }
        $guestExitCode = [int]([regex]::Match($guestExitLine, '-?\d+').Value)
        exit $guestExitCode
    }

    $args = @()
    if ($Distribution) { $args += @('--distribution', $Distribution) }
    $args += @('--', $DarlingCommand)
    $args += $DarlingArgument
    Invoke-WslChecked -Arguments $args
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 21
} finally {
    if ($stagedAppRoot -and $stagedAppsRoot -and
        $stagedAppRoot.StartsWith($stagedAppsRoot.TrimEnd('/') + '/')) {
        & wsl.exe --distribution $Distribution --exec rm -rf -- $stagedAppRoot 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Could not remove temporary Darling staging directory: $stagedAppRoot"
        }
    }
}
