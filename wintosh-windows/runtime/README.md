# Runtime

Wintosh process runner and executable-entry support for the native Windows
adapter.

The runner accepts a Mach-O image and invokes the mapped entry point after the
current parser, dyld graph, relocations, bindings, initializers, and Windows
page protections have been applied. On an x86_64 Windows build this execution
path is limited to x86_64 Mach-O images whose entry ABI and imported symbols
are compatible with the Wintosh bridge.

Example:

```text
wintosh-run.exe --inspect path\to\program
wintosh-run.exe path\to\program arg1 arg2
```

`--inspect` performs parsing and dependency/initialization-order inspection
without entering the image. Execution of 32-bit i386 or ARM64 Mach-O images
is rejected on x86_64 Windows because no CPU translation layer is included.
The runner is therefore an adapter/loader milestone, not a complete Darling
root filesystem or a full macOS framework runtime.

The parser and loader retain Darling-compatible GPL provenance; see the
repository license inventory and the upstream Darling project:
https://github.com/darlinghq/darling
