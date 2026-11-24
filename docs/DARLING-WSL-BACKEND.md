# WSL / emulated-Linux backend prototype

`tools/Invoke-DarlingWslBackend.ps1` is the first separate execution seam for
the architecture identified by Darwin_Computa. It does not replace the native
Windows adapter. It probes WSL and, when WSL is available, forwards a selected
Linux command to a selected distribution. The new `DarwinComputa` mode runs
Darwin_Computa's own Darling launcher inside that distribution.

## Contract

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Probe
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Run -Distribution <name> -DarlingCommand <path> -DarlingArgument <args>
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa -Distribution <name> -ComputaRoot /opt/Darwin_Computa -GuestMachO /usr/bin/sw_vers
```

For `DarwinComputa`, `-ComputaRoot` points to the Darwin_Computa checkout inside
the WSL distribution. The wrapper invokes
`<ComputaRoot>/tools/run_darling_app.sh <GuestMachO> [arguments...]`, which
starts the Boxedwine runtime and Darling rootfs. That demo helper sets
`DSERVER_INIT`, so it runs one app as the Darling container init and deliberately
bypasses the full `launchd` service bootstrap. This is only the first runtime
gate, not the complete Darling environment. This is the runtime chain:

`Windows/Wintosh -> wsl.exe -> WSL1 Linux process -> Boxedwine userspace CPU + Linux-kernel emulator -> Darling/darlingserver -> Mach-O`

WSL1 is the Linux-to-Windows syscall compatibility layer; it does not start a
virtual machine. Boxedwine is itself an ordinary process and supplies the
emulated Linux kernel in userspace. This route therefore avoids hardware
virtualization, but it is a separate runtime backend, not Wintosh's native
Mach-O loader. The Darwin_Computa checkout's current MSVC project does not
build its Darwin path; this route uses its Linux build inside WSL1. A `.app`
outside the Darling rootfs still needs a staging/mount bridge before it can be
addressed as a Darwin guest path.

## Full-Darling completion gates

The complete backend must pass these gates in order:

1. Build Darwin_Computa's x86-64 Linux target inside a WSL1 distribution with
   `BOXEDWINE_GUEST_X64` and `BOXEDWINE_DARWIN` enabled; preserve its GPLv2
   component boundary.
2. Stage the complete Darling prefix and its Linux dependency closure, then
   verify `mldr`, `dyld`, `libSystem`, frameworks and `darlingserver` together.
3. Boot the normal `launchd` path and fix the system-job bootstrap so it starts
   `launchctl`, `shellspawn` and the service processes. Darwin_Computa's own
   README identifies this as unfinished.
4. Add an app-bundle bridge that stages a Windows `.app`/Mach-O and its resources
   into the guest-visible Darwin prefix while preserving paths and arguments.
5. Verify multiple apps, process lifecycle and IPC, then Cocoa/AppKit windows
   and input/output through the Boxedwine SDL/graphics bridge.

Only after these pass should Wintosh label the backend full Darling runtime.
The present `DarwinComputa` launcher only addresses the first command-level
execution seam.

Exit `20` means WSL is unavailable or access was denied. Exit `21` means the
forwarded invocation failed. A successful probe prints
`DARLING_WSL_BACKEND=AVAILABLE`; this is only WSL availability, not proof that
Darling or a macOS Mach-O runs.

The next implementation stages/builds Darwin_Computa and its Darling rootfs in
the selected WSL1 distribution, invokes the wrapper above, captures its
syscall/server trace, and adds each missing Linux primitive to the backend
matrix. The current environment probe returned `Wsl/E_ACCESSDENIED`, so the
new launcher contract is implemented but an end-to-end WSL1 run is not verified.
