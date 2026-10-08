# Wintosh current status

Status date: 2026-10-08

## Evidence summary

| Area | Current state | Evidence / limitation |
|---|---|---|
| Native Windows source | present | `darling-source/src/native/windows` |
| x64 Foundation type encoding | verified | `darling_windows_foundation_smoke.exe` reports `FOUNDATION_TYPE_ENCODING_ABI=PASS` |
| Win32 Foundation type encoding | verified | same smoke gate on Win32 |
| Low-level primitive families | verified in selected batches | tracked in `PRIMITIVE-MATRIX.csv`; not equivalent to full Darwin semantics |
| Full generated `ALL_BUILD` | not currently confirmed | previous environment invocation hung without active compiler output and was stopped |
| WSL runtime backend | unavailable in the current host evidence | probe reported `Wsl/E_ACCESSDENIED` |
| Real Darling server / arbitrary Mach-O app | not proven | no local end-to-end application run |
| Darling component licenses | incomplete review | 150 external components; 147 remain review-required |
| Corresponding source completeness | exception remains | Heimdal filename cannot be materialized on NTFS |

## What this means

Wintosh is a documented native Windows compatibility experiment with real
verified primitive adapters, not yet a complete Darling port. The current
implementation can demonstrate selected ABI-shaped behaviors and smoke tests;
it cannot yet claim that normal macOS applications, full Foundation, AppKit,
or the Darling userland execute on Windows.

## Next gates

1. Make a clean reproducible x64 and Win32 `ALL_BUILD` run.
2. Preserve smoke output as release evidence.
3. Resolve or exclude the 147 external license-review cases.
4. Resolve the NTFS-incompatible Heimdal source checkout or document an
   alternate corresponding-source bundle.
5. Implement Foundation/CoreFoundation families incrementally.
6. Prove a guest/runtime boundary with a real Darling server or a documented
   WSL/emulated-Linux backend.
