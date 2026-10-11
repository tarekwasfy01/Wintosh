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
wintosh-run.exe --diagnose-host
wintosh-run.exe --init-prefix path\to\darwin-prefix
wintosh-run.exe path\to\program arg1 arg2
```

`--inspect` performs parsing and dependency/initialization-order inspection
without entering the image. Execution of 32-bit i386 or ARM64 Mach-O images
is rejected on x86_64 Windows because no CPU translation layer is included.
`--diagnose-host` is independent of an input image and reports the current
Windows architecture, pointer width, supported native Mach-O target, prefix
source, and the deliberately unsupported full-macOS boundaries.
`--init-prefix` creates the minimal directory layout used by the native
Darwin adapter.  It deliberately does not download or invent Apple system
libraries; the current `libSystem` and `libobjc` boundary is provided by the
host symbol table, while real Darwin dylibs and frameworks remain a separate
licensed runtime component.
The runner is therefore an adapter/loader milestone, not a complete Darling
root filesystem or a full macOS framework runtime.

`DarwinBootstrap::Run` currently performs the verified native preparation
sequence: dependency-graph resolution, segment mapping/protection, rebases,
classic and chained bindings, relocations, TLV setup, Objective-C section
registration, initializers, entry invocation, and reverse-order terminators.
It does not yet create a Darwin process image with real `dyld` startup frames,
Mach kernel syscalls, per-process namespaces, or complete Apple framework
state; those are separate porting families and are intentionally not hidden
behind the current CLI success path.

The parser and loader retain Darling-compatible GPL provenance; see the
repository license inventory and the upstream Darling project:
https://github.com/darlinghq/darling
