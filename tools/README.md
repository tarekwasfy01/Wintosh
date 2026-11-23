# Wintosh porting tools

These scripts support the native Windows port without bundling upstream
Darling source code.

## Mathematical capability report

`python tools/porting_matrix.py` composes Boolean transformation bounds for
the native and Darwin/Linux/WSL1/Windows routes. Supply CTest JUnit results
to enable bounded native evidence; without results all implemented rows
remain unproven. See [the model and commands](../docs/porting/MATHEMATICAL-MODEL.md).

## MIG inventory

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Scan-MigDefinitions.ps1 `
  -SourceRoot .\darling-source `
  -OutputCsv .\build\mig-definitions.csv
```

The inventory records paths, SHA-256 values, detected subsystem/routine names,
and an explicit upstream-license review marker. It does not copy, transform,
or relicense upstream files.

## Stub registration fragment

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Generate-MigRegistry.ps1 `
  -InventoryCsv .\build\mig-definitions.csv `
  -OutputCpp .\build\generated-mig-registry.cpp
```

Generated entries are empty integration stubs with local IDs. They are not
Darwin implementations and must be replaced or completed after reviewing the
corresponding upstream definitions and licenses.

## Bounded i386 Mach-O linker

`python tools/link_i386_macho.py --output app.macho main.o helper.o` accepts
little-endian i386 `MH_OBJECT` files. It combines each input's `__TEXT,__text`,
one optional `__TEXT,__cstring` or `__const`, and optional `__DATA,__data` and
zerofill sections, resolves external
definitions between those sections, and supports 32-bit
`GENERIC_RELOC_VANILLA` PC-relative and absolute references. Zerofill and
`__common` storage are placed in the zero-initialized data segment; absolute
pointers receive dyld type-2 rebases for the image slide. It resolves bounded
`__IMPORT,__pointers` slots against supplied objects and emits local function
stubs plus bind opcodes for unresolved PC-relative calls. Those calls resolve
only if Wintosh's host-symbol table exports the requested Darwin name; this
does not load Darwin shared libraries. Constant-section bytes are emitted in
a `__TEXT,__const` section in the non-writable `__TEXT` segment. Other
relocation forms, dylibs, CRT, and frameworks remain outside this prototype.
It emits a segmented `MH_EXECUTE` and drops unsupported
metadata such as unwind sections. Two-object call and data fixtures, plus
single-object initialized and zero-initialized global fixtures, ran in Wintosh
and returned 42. `__cstring` pointer and direct references returned 65 and 66.
Host-bound `_puts` and `_printf` calls also ran; the formatted guest returned
3. A heap guest called `_malloc`/`_free` and returned 42, and `_exit(23)`
terminated with status 23. These are host exports, not a complete libSystem.
See
[LINKER-RESEARCH.md](../docs/porting/LINKER-RESEARCH.md) for the exact contract.

The scripts and this documentation are original Wintosh tooling. Upstream
Darling, WSL, and other referenced projects retain their original licenses;
see the repository license inventory before distributing derived artifacts.
