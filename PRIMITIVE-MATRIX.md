# Darling Windows primitive matrix

This matrix is the control plane for the incremental port. Each family is
mapped through three deliberately separate layers:

`Darling surface -> Linux/WSL reference -> Windows backend`

Linux or WSL is used to observe POSIX/Darwin-adjacent contracts, error cases,
calling conventions and test vectors. It is not treated as proof that Linux
implements macOS semantics. Windows code is implemented only in the native
adapter layer, with the corresponding source and license provenance retained.

## Batch policy

1. Group symbols by primitive family and ABI shape.
2. Record the Linux/WSL analogue and its limitations.
3. Implement the Win32/Windows adapter as one batch.
4. Add x64 and Win32 smoke coverage.
5. Mark a row `verified` only after build and runtime evidence on both targets.
6. Keep Darwin-only or Apple-framework behavior explicitly `missing` instead
   of silently mapping it to an unrelated Windows behavior.

The machine-readable source is [PRIMITIVE-MATRIX.csv](PRIMITIVE-MATRIX.csv).
The consolidated cross-source extraction is [SOURCE-EXTRACTION-MATRIX.md](SOURCE-EXTRACTION-MATRIX.md).
The repository-wide navigation map is [DOCUMENTATION-INDEX.md](DOCUMENTATION-INDEX.md),
and the current evidence snapshot is [STATUS.md](STATUS.md).
Current verified low-level families include libc/file descriptors, sockets,
process and time primitives, pthread synchronization/TLS, Mach-O loading, and
the process-local Mach C ABI including basic VM and port-type operations. This
matrix does not claim that ordinary macOS applications run yet.

## Status vocabulary

The `status` column describes implementation maturity: `verified` means the
listed test gate passed on both x64 and Win32; `partial` means only part of the
surface or a local adapter is covered; `missing` means no Windows backend is
implemented; `not implemented` and `reference-only` mean the source was
analysed but no runnable port is claimed. The `implementation` column is a
separate description of the technical form, such as `adapter`,
`documentation`, or `none`.

## Current matrix counts

The current CSV contains 18 porting families: 10 are marked `verified`, 5 are
`partial`, 2 are `missing`, and 1 is `not implemented`.
These are family-level counts, not a percentage of Darling and not a claim
that the verified rows compose into a working macOS runtime. The legal
`licensing` row remains `partial` while the external-component audit is open.

## Source boundaries

Linux/WSL references are compatibility evidence and test material, not a new
license grant. PureDarwin, Darling and external components remain catalogued in
`licenses/`; Apple/XNU/libSystem and framework code is not copied into the
Windows adapter without component-level review.
