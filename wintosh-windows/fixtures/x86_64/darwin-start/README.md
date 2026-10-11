# Darwin startup ABI fixture

`darwin-start.macho` is a repository-owned x86_64 Mach-O executable whose
entrypoint reads `argc` from the first word of `RSP`, as a Darwin `crt1`-style
entry would, and then calls the bound `_exit` symbol with that value.

Generate it reproducibly with:

```text
wintosh_macho_exit_smoke.exe --emit-darwin darwin-start.macho
```

Run it through the Darwin startup trampoline with:

```text
wintosh.exe --startup-abi darwin darwin-start.macho one
```

The expected exit code is `2` (`argc` is the image name plus `one`).  It is
fully repository-owned and contains no Apple SDK, macOS system library or
third-party binary.
