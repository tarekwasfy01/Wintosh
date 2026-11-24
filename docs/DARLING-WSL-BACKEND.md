# WSL / emulated-Linux backend prototype

`tools/Invoke-DarlingWslBackend.ps1` connects the separate Darwin_Computa runtime to WintOS. It leaves the native Windows Mach-O adapter intact and can probe WSL, forward an ordinary Linux command, or start the Darling backend.

## Contract

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Probe
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Run -Distribution <name> -DarlingCommand <path> -DarlingArgument <args>
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa -Distribution <name> -ComputaRoot /opt/Darwin_Computa -GuestMachO /usr/bin/sw_vers
```

In `DarwinComputa` mode, `-ComputaRoot` points to a Linux build of the Darwin_Computa checkout inside WSL. The wrapper runs `project/linux/Build/Release/boxedwine --darwin-run /usr/bin/darlingserver` with `DSERVER_INIT=<GuestMachO>`. This is the project's direct-init demo path; it bypasses full `launchd` service bootstrap. Upstream currently forwards only the executable path, not its argv.

`Windows/WintOS -> wsl.exe -> WSL Linux process -> Boxedwine CPU + Linux kernel emulator -> Darling/darlingserver -> Mach-O`

WSL1 is the Linux-to-Windows syscall compatibility layer and does not start a virtual machine. Boxedwine is an ordinary process that emulates the Linux kernel in userspace. The Darwin_Computa checkout's MSVC project does not build its Darwin path, so this backend uses its Linux target inside WSL. A Windows `.app` still needs a staging bridge to the Darwin guest filesystem.

## Full-Darling completion gates

1. Build Darwin_Computa's x86-64 Linux target inside the deployment WSL1 distribution with `BOXEDWINE_GUEST_X64=1` and `BOXEDWINE_DARWIN=1`; preserve the GPLv2 component boundary. The current build evidence is from WSL2.
2. Stage the complete Darling prefix and Linux dependency closure; verify `mldr`, `dyld`, `libSystem`, frameworks and `darlingserver` together.
3. Boot normal `launchd` and complete its system-job bootstrap for `launchctl`, `shellspawn` and service processes.
4. Stage app bundles and resources into guest-visible paths while preserving arguments.
5. Verify multiple apps, lifecycle and IPC, then Cocoa/AppKit windows and input/output through the SDL/graphics bridge.

Do not label this a full Darling runtime until those gates pass. Exit `20` means WSL is unavailable/access denied; exit `21` means the invocation failed. A successful WSL probe does not prove a Mach-O app runs.

## Local build evidence

In this environment Ubuntu runs as WSL2. Darwin_Computa's Linux target was built with the x86-64 and Darwin defines. Its `--x64-selftest` passed 234/234; `--darwin-selftest` passed 12/12. GCC 15 required a one-line packed-field value copy in a disposable build copy; the upstream checkout was not modified.

The official Darling 2026-06-08 Debian packages were extracted into a local, untracked rootfs. The real `/usr/bin/sw_vers` Mach-O loaded through `mldr`, checked in with `darlingserver`, and exercised Mach RPC. It did not finish startup. Traces show missing Linux runtime files including `libstdc++.so.6` and `/bin/sh`, missing writable prefix paths, and an unimplemented Linux syscall. A successful Mach-O app run is not yet verified.

The Windows host reports that its current configuration does not support WSL1. The backend has only been built and exercised under WSL2 so far; WSL1 is still required to verify the requested no-VM deployment. The next engineering steps are to close the Linux rootfs dependencies, address the first blocking syscall from a focused trace, then verify a clean CLI run before working on launchd, app bundles and Cocoa GUI.
