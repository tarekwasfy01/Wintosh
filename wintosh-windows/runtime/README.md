# Runtime

Wintosh process runner and executable-entry support for the native Windows
adapter.

The runner accepts a Mach-O image or an application `.app` bundle and invokes the mapped entry point after the
current parser, dyld graph, relocations, bindings, initializers, and Windows
page protections have been applied. On an x86_64 Windows build this execution
path is limited to x86_64 Mach-O images whose entry ABI and imported symbols
are compatible with the Wintosh bridge.
The Win32 target compiles the host library and runner and has launched a
hand-built i386 `LC_MAIN` Mach-O with no imports; its compatible `main` returned
42. This confirms the basic mapping and cdecl entry path, not general app
compatibility. A separate hand-built i386 image has also entered through the
Darwin-start stack, resolved `_exit` through `LC_DYLD_INFO_ONLY`, and exited
with code 42. Compiler-generated CRT startup, app dependencies, and framework
behavior remain unverified.
The `tools/link_i386_macho.py` prototype links Clang-produced i386 `__text`
and optional `__data` sections. It resolves cross-object symbols and handles
32-bit Vanilla PC-relative and absolute relocations; absolute pointers receive
dyld type-2 rebases. The two-object `_main` to `_helper` fixture returned 42
in either input order. A mutable global initialized to 40 and an uninitialized
`__common` global were each written, read back, and returned as 42 under the
Win32 runner. Zerofill is folded into the prototype's zero-initialized data
segment. A two-object global fixture also resolved Clang's
`__IMPORT,__pointers` indirect slot to a definition in the companion object,
then wrote and read the value and returned 42. This resolves only symbols
defined by supplied object files; Darwin dylib imports, CRT, and framework
linking remain outside the prototype. A pointer from mutable data to a
`__cstring` literal also rebased correctly and returned its first byte (65).
The linker emits constant bytes in a non-writable `__TEXT,__const` section.
Apple's [dyld4 design notes](https://github.com/apple-oss-distributions/dyld/blob/main/doc/dyld4.md)
describe process startup with argc, argv, envp, and Apple parameters on the
initial stack. That current description does not establish compatibility with
historical i386 dyld/CRT binaries; a real i386 guest launch remains the needed
runtime check.
The Windows ARM64 source now admits matching ARM64 guest code through the
native instruction path and uses a dedicated Darwin-start stack transfer.
This path is intended for Clang Windows ARM64 builds; ARM64 MSVC remains
fail-closed for real guest entry. A fresh ARM64 CMake configure currently fails
during compiler identification because `msvcrtd.lib` is missing, so the runner
and guest path are not verified in this environment. All 32 host C++ translation
units plus the ARM64 startup trampoline compiled directly with Clang and archived
as 33 ARM64 COFF members in the earlier source pass. The Objective-C guest
`objc_msgSend`, `objc_msgSendSuper`, and `objc_msgSendSuper2` entries now use
ARM64 assembly adapters to pass Apple's variadic stack area into the
signature-aware typed dispatcher; Super2 begins lookup at the superclass of
the supplied class. The updated Objective-C source, Windows symbol resolver,
and adapters each compile as ARM64 COFF objects. The shims also restore
`x0-x1` and `d0-d3` return registers for the supported scalar, HFA, and
NSRange return classes. This establishes source code generation only, not a
refreshed full archive, executable link, or working guest ABI. Unsupported
method signatures, indirect returns, and overflow cases remain unverified.

For `.app` input, it reads `Contents/Info.plist` through Wintosh's existing
CoreFoundation XML or binary property-list decoder, resolves a string-valued
`CFBundleExecutable`, and falls back to the bundle filename when metadata is
missing or invalid. The executable name must be a single path component and
is resolved under `Contents/MacOS`; Windows drive prefixes and path separators
in the plist value are rejected. The selected path is normalized to an absolute
path before bootstrap. This only selects the entry image. It does not imply
complete bundle resource, plug-in, signing, or framework support.

Example:

```text
wintosh-run.exe --inspect path\to\program
wintosh-run.exe --diagnose-host
wintosh-run.exe --init-prefix path\to\darwin-prefix
wintosh-run.exe --startup-abi darwin path\to\darwin-start
wintosh-run.exe path\to\program arg1 arg2
```

`--inspect` performs parsing and dependency/initialization-order inspection
without entering the image. Execution of i386 Mach-O is rejected on x86_64
Windows because no CPU translation layer is included. A Win32 build now has a
native i386 entry path, but its guest execution has not yet been verified with
a real 32-bit Mach-O runtime fixture. ARM64 execution on x86_64 Windows also
requires a CPU translation layer.
`--diagnose-host` is independent of an input image and reports the current
Windows architecture, pointer width, supported native Mach-O target, prefix
source, preferred universal-binary slice, and the absence of cross-architecture
CPU translation. This diagnostic reports host-level capability; app-specific
ABI and framework coverage remain separate.
`--init-prefix` creates the minimal directory layout used by the native
Darwin adapter.  It deliberately does not download or invent Apple system
libraries; the current `libSystem` and `libobjc` boundary is provided by the
host symbol table, while real Darwin dylibs and frameworks remain a separate
licensed runtime component.
`--startup-abi darwin` selects the native x86_64, i386, or ARM64 trampoline
for a matching-architecture Mach-O whose entrypoint is a Darwin `crt1`/`dyld`
start routine. The Win32 i386 library and runner build successfully and use a
MASM trampoline to install argc, argv, envp, and Apple parameters on the guest
initial stack. The i386 Darwin-start transfer remains unverified. The ARM64
transfer has only been exercised by the minimal host-`_exit` fixture described
above. The ARM64 path requires Clang and remains source/build verified only; an ARM64 runner
executable and guest runtime have not yet been verified.
`--startup-abi main` is the real compatible `int(argc, argv, envp)` path and
requires Clang's System-V ABI support on x86_64. On Win32, Darwin i386 and
Windows use the same stack-based cdecl convention, so the MSVC build can enter
this i386 path. On ARM64 the basic integer/pointer entry parameters use x0-x2.
Apple ARM64 differs from Windows for other ABI cases, so this does not establish
general ARM64 Objective-C or framework-call compatibility. Other MSVC
architecture paths fail closed before entering guest code.
`--startup-abi synthetic-main` is reserved for the repository's marked
synthetic fixtures, which intentionally use the Windows-compatible calling
convention and are not proof of Apple ABI execution.
The runner automatically adds `executable_path=<resolved-image>` to the Apple
parameter vector. Additional entries can be supplied with `--apple-param`.
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
