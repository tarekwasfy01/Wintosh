# WSL / emulated-Linux backend prototype

`tools/Invoke-DarlingWslBackend.ps1` connects the separate Darwin_Computa runtime to WintOS. It leaves the native Windows Mach-O adapter intact and can probe WSL, forward an ordinary Linux command, or start the Darling backend.

## Contract

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Probe
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Run -Distribution <name> -DarlingCommand <path> -DarlingArgument <args>
powershell -ExecutionPolicy Bypass -File .\tools\Build-DarwinComputaWslBackend.ps1 -Distribution <name> -ComputaRoot /opt/Darwin_Computa
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa -Distribution <name> -ComputaRoot /opt/Darwin_Computa -GuestMachO /usr/bin/sw_vers
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa -Distribution <name> -ComputaRoot /opt/Darwin_Computa -WindowsAppPath 'C:\Apps\Example.app' -GuestBundleExecutable Example
# WSL2 is permitted only for development and compatibility tests:
powershell -ExecutionPolicy Bypass -File .\tools\Build-DarwinComputaWslBackend.ps1 -Distribution <name> -ComputaRoot /opt/Darwin_Computa -AllowWsl2ForDevelopment
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa -Distribution <name> -ComputaRoot /opt/Darwin_Computa -AllowWsl2ForDevelopment
# Run from an elevated PowerShell to enable WSL1 and convert Ubuntu:
powershell -ExecutionPolicy Bypass -File .\tools\Enable-DarlingWsl1.ps1 -Distribution Ubuntu -BackupPath D:\Backups\Ubuntu-before-WSL1.tar
```

The WSL1 preparation script must be run as Administrator. It enables the
Windows optional feature without forcing an immediate reboot; if Windows
requests a restart, restart and rerun the script. `-BackupPath` is optional;
when supplied, the script exports the distribution before conversion.

The build script checks the selected distribution version and build packages,
sets the x86-64 guest and Darwin defines, builds the Linux target, then runs
both upstream self-tests. Install these packages in the distribution first:
`build-essential`, `libsdl2-dev`, `libssl-dev`, `libcurl4-openssl-dev`,
`libminizip-dev`, `libgl-dev` and `libglu1-mesa-dev`. GCC 15 may require a
downstream compatibility fix for a packed ELF field in `loader64.cpp`; the
script reports the compiler failure and does not patch the external checkout.

In `DarwinComputa` mode, `-ComputaRoot` points to the Darwin_Computa checkout inside WSL. The wrapper checks the selected distribution's WSL version and requires **WSL1** by default; WSL2 needs the explicit `-AllowWsl2ForDevelopment` switch. It calls `tools/run-darwin-computa-once.sh`, which invokes `project/linux/Build/Release/boxedwine --darwin-run /usr/bin/darlingserver` with `DSERVER_INIT=<GuestMachO>`. This is the project's direct-init demo path; it bypasses full `launchd` service bootstrap. `DSERVER_INIT` currently forwards only the executable path, not its argv.

Before launch, the wrapper checks whether the requested system executable is a
symlink in `darling.zip` or in the staged writable overlay. If so, it copies the resolved Mach-O to the same path
in the writable rootfs overlay. This works around the current direct-init
symlink-to-executable failure while preserving the archive and original
Darling package. This also handles a bundle's `Contents/MacOS` executable
symlink when its target is another regular file in the staged bundle.

`Windows/WintOS -> wsl.exe -> WSL Linux process -> Boxedwine CPU + Linux kernel emulator -> Darling/darlingserver -> Mach-O`

With `-WindowsAppPath` and `-GuestBundleExecutable`, the PowerShell wrapper
copies a Windows `.app` directory into a unique temporary location inside the
Darling rootfs, checks the executable path, runs it, and removes the staged
copy afterward. It reads `CFBundleExecutable` from XML or binary `Contents/Info.plist`
with Python's `plistlib`; without a plist, it falls back to the bundle name.
`-GuestBundleExecutable` can override discovery. A fixture with a relative
`Contents/MacOS` symlink to a Mach-O under `Contents/Resources` was staged and
launched successfully. This validates that specific symlink and path flow; it
does not establish compatibility with arbitrary real app bundles.

Darwin_Computa's direct-init mode leaves the `darlingserver` process alive after
the guest command exits. `tools/run-darwin-computa-once.sh` supervises that
one-shot path, waits for the guest `mldr` exit record, returns its exit status,
and force-stops only the Boxedwine process it started. Guest stdout/stderr is
emitted without the emulator's log prefix, and the PowerShell launcher returns
the guest's exit code. It also fails early if `mldr` exits before the requested
guest starts. This is a bounded one-command lifecycle, not a graceful service
shutdown, persistent service, or launchd replacement.

WSL1 is a Windows-provided Linux-to-Windows syscall compatibility layer, not a library embedded in WintOS. Boxedwine is an ordinary process that emulates the Linux kernel in userspace. WintOS can orchestrate these existing components, but cannot turn WSL1 itself into an in-process WintOS module. The Darwin_Computa checkout's MSVC project does not build its Darwin path, so this backend uses its Linux target inside WSL. A Windows `.app` still needs a staging bridge to the Darwin guest filesystem.

## WSL source and driver boundary

Microsoft publishes the WSL repository under MIT, including the command-line
tools, service, Linux-side init/daemons and Plan 9 file server. Microsoft still
identifies `Lxcore.sys` (the WSL1 kernel driver) as closed source; `P9rdr.sys`
and `p9np.dll` are also Windows-image components, not downloadable WSL source
packages. The public WSL repository therefore cannot supply a buildable WSL1
syscall layer for embedding in WintOS. On Windows, WSL1 must use the driver's
Windows optional feature; a separately downloaded driver is not an available
substitute. Sources: [Microsoft's WSL component and source status](https://learn.microsoft.com/en-us/windows/wsl/opensource), [microsoft/WSL repository and MIT license](https://github.com/microsoft/WSL).

## Full-Darling completion gates

1. **Complete:** Build Darwin_Computa's x86-64 Linux target inside the deployment WSL1 distribution with `BOXEDWINE_GUEST_X64=1` and `BOXEDWINE_DARWIN=1`; preserve the GPLv2 component boundary.
2. **Partial:** Stage the Darling rootfs and dependency closure; `mldr` has now run a real Mach-O CLI tool, but the full `launchd`/framework closure is not verified.
3. Boot normal `launchd` and complete its system-job bootstrap for `launchctl`, `shellspawn` and service processes.
4. **Partial:** Stage a Windows `.app` directory into the Darling rootfs, discover its executable, and materialize an executable symlink. Validate resource/framework symlinks on real bundles and add argv forwarding.
5. Verify multiple apps, lifecycle and IPC, then Cocoa/AppKit windows and input/output through the SDL/graphics bridge.

Do not label this a full Darling runtime until those gates pass. Exit `20` means WSL is unavailable/access denied; exit `21` means the invocation failed. A successful WSL probe does not prove a Mach-O app runs.

## Local build evidence

On 2026-11-25, Ubuntu was converted to and verified as **WSL1**. The Darwin_Computa checkout was copied from the Windows workspace into `/home/tarek/Darwin_Computa` so compilation runs on the WSL Linux filesystem. Its x86-64 Linux target was rebuilt with `BOXEDWINE_GUEST_X64=1` and `BOXEDWINE_DARWIN=1`; `--x64-selftest` passed 234/234 and `--darwin-selftest` passed 12/12. GCC 15 required a one-line packed-field value copy in this disposable WSL checkout; the Windows source checkout was not modified.

The official Darling package zips were present in the local rootfs. In WSL1, the real `/usr/bin/sw_vers` Mach-O loaded through `mldr`, ran under `darlingserver`, and exited with status 0 after printing `ProductName: macOS`, `ProductVersion: 11.7.4`, and `BuildVersion: Darling`. This is the first verified Mach-O command-line program through the Darwin_Computa/Boxedwine/Darling path on WSL1. The direct-init service stays alive after the guest exits; the new WintOS supervisor detects the guest exit, returns its status and force-stops its own Boxedwine process. A graceful shutdown protocol is still missing. Logs also include unsupported shared-memfd behavior, missing `xdg-user-dir`, and Linux syscall 166. This does not demonstrate a GUI app or complete Darling compatibility.

The WSL1 build and supervised `sw_vers` run are now verified. Follow-up commands showed `/usr/bin/uname` returning `Darwin`, `/bin/date` returning a formatted timestamp, and `/usr/bin/id` returning `uid=0 gid=0`. `/usr/bin/defaults` reached argument validation and returned its expected status 1 when invoked without required arguments. `/usr/bin/false` returned guest exit code 1 through the PowerShell launcher, confirming nonzero status forwarding. A Windows `.app`-shaped directory containing the `sw_vers` Mach-O at `Contents/MacOS/WintoshProbe` was staged into a temporary Darling rootfs path and launched successfully, including binary Info.plist executable discovery and a bundle path with spaces; the wrapper removed its temporary copy afterward. This validates path translation and file staging, not real bundle resources or a GUI application. `/usr/bin/whoami` is a symlink to `id` in the Darling package; it returned `uid=0` after the launcher materialized that Mach-O in the writable rootfs overlay. The direct symlink path previously failed with “Exec format error”. Remaining work is to implement graceful Darling service shutdown, materialize or resolve bundle-internal symlinks, complete prefix/bootstrap requirements, support argv, then verify launchd, GUI windows, input/output, and multiple applications. Do not label this a full Darling runtime until those gates pass.
