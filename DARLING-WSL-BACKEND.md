# WSL / emulated-Linux backend prototype

`tools/Invoke-DarlingWslBackend.ps1` is the first separate execution seam for
the architecture identified by Darwin_Computa. It does not replace the native
Windows adapter. It probes WSL and, when WSL is available, forwards a selected
Linux command to a selected distribution.

## Contract

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Probe
powershell -ExecutionPolicy Bypass -File .\tools\Invoke-DarlingWslBackend.ps1 -Mode Run -Distribution <name> -DarlingCommand <path> -DarlingArgument <args>
```

Exit `20` means WSL is unavailable or access was denied. Exit `21` means the
forwarded invocation failed. A successful probe prints
`DARLING_WSL_BACKEND=AVAILABLE`; this is only WSL availability, not proof that
Darling or a macOS Mach-O runs.

The next implementation stage is a distribution-specific Darling launcher:
stage the checked-out Darling build/rootfs inside WSL, invoke `mldr` or the
installed Darling command, capture its syscall/server trace, and add each
missing Linux primitive to the backend matrix. The current environment probe
returned `Wsl/E_ACCESSDENIED`, so no runtime Darling claim is made here.
