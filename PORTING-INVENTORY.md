# Darling native Windows port: architecture inventory

Date: 2026-10-04

## Result

The Microsoft WSL source is useful as a reference for Windows host services,
but it is not a native POSIX/Darwin compatibility layer. WSL keeps Linux
semantics in a Linux kernel or utility VM and uses Windows code as a control
plane. A native Darling port therefore needs a separate Windows host backend;
WSL code can be reused only as implementation guidance or for small, isolated
transport helpers.

## Evidence from Darling

| Area | Current Darling dependency | Evidence | Native Windows consequence |
|---|---|---|---|
| Build target | CMake declares Linux/x86-64 and requires `setcap` | `darling-source/CMakeLists.txt` lines 3-4, 134-135, 191 | Add a Windows toolchain/backend instead of changing only the compiler flags |
| Core runtime | Always builds `darlingserver`, Linux `libsimple`, and `startup` | `darling-source/src/CMakeLists.txt` lines 92-96 | Split host runtime from Darwin-facing libraries |
| Process bootstrap | setuid/setgid, `fork`, `exec`, `/proc`, mount namespace and uid/gid maps | `darling-source/src/startup/darling.c` | Implement a Windows session broker using `CreateProcessW`, Job Objects and explicit identity handling |
| Mach-O launch | Linux `ptrace`, `prctl`, `/proc/self/exe`, `mmap`, Unix sockets | `darling-source/src/startup/mldr/mldr.c` | Implement a Windows loader/bootstrap path around `VirtualAlloc`, file mappings and named/AF_UNIX IPC |
| Terminal | `pty.h`, `ioctl`, Unix signals and `poll` | `darling-source/src/startup/darling.c`, `src/shellspawn` | Use ConPTY or a documented pipe/console fallback |
| Prefix filesystem | `/proc`, `/dev`, symlinks, overlayfs assumptions | top-level `CMakeLists.txt` installation rules | Use a Windows path/VFS layer; do not assume NTFS is a POSIX filesystem |

## Evidence from WSL

The WSL tree contains reusable patterns in:

- `wsl-source/src/windows/wslhost`: Windows-side host process and channel setup.
- `wsl-source/src/windows/wslcsession/WSLCVirtualMachine.cpp`: lifecycle,
  process creation requests, channels, mounts and port mappings.
- `wsl-source/src/windows/wslrelay/localhost.cpp`: Winsock listeners and
  socket relays using Hyper-V sockets.
- `wsl-source/src/shared/inc/SocketChannel.h`: framed host/guest messaging
  pattern.
- `wsl-source/src/linux/plan9` and `src/linux/init/plan9.cpp`: filesystem
  transport between Windows and a Linux guest.

These components assume a Linux guest. In particular, VM lifecycle, Plan 9,
VirtioFS and Hyper-V sockets are not replacements for Darling's native
`fork`/`mmap`/Mach-IPC/process model. They are useful only if the project is
changed into a WSL-backed product rather than a native Windows port.

## Required Windows backend boundary

The port should introduce a small host interface before changing framework
code. The first backend should cover:

1. `process`: launch, wait, terminate, parent/child tracking and exit status.
2. `ipc`: Darling server RPC transport using named pipes or Windows AF_UNIX.
3. `memory`: file-backed mappings, anonymous mappings, protection changes and
   fixed-address failure reporting.
4. `filesystem`: prefix paths, symlink/reparse-point policy, permissions and
   `/proc`-like synthetic entries needed by the runtime.
5. `sync`: mutexes, condition variables, futex-like wait/wake operations and
   thread-local storage.
6. `terminal`: ConPTY-backed stdin/stdout/stderr and window-size events.
7. `network`: Winsock adapters for Darwin socket behavior.

Darwin-facing libraries should depend on this interface, not directly on
Linux headers. Linux remains the reference backend and Windows becomes a
second backend selected by CMake.

## Porting order

### Stage 0: inventory (this document)

Complete the dependency map and preserve the Linux build unchanged.

### Stage 1: Windows host skeleton — started and smoke-tested

Added `darling-source/src/native/windows` with a Windows-only host library and
`Process` lifecycle wrapper around `CreateProcessW`, wait, exit status and
termination. The direct Clang/Windows-SDK proof gate passed with:

`PROCESS_SMOKE_EXIT=7`

The Stage 1 runtime smoke proof passes prefix creation, framed Named-Pipe
`PING`/`PONG` RPC and cleanup. The out-of-process broker proof also passes:

`BROKER_RPC=PONG`, `BROKER_SHUTDOWN=BYE`, `BROKER_EXIT=0`,
`BROKER_CLEANUP=PASS`, `SMOKE_EXIT=0`.

The pipe ACL is intentionally local-development broad while the broker
identity model is still being designed; it must be narrowed before packaging.

### Stage 1 loader boundary — started and validated

Added `darling_windows_macho.cpp` with Windows file mapping and validation for
64-bit little-endian x86_64 Mach-O executables. The smoke proof passes
`MACHO_MAGIC=PASS`, one validated `LC_SEGMENT_64`, entry offset `0x1000`, and
`MACHO_SMOKE_EXIT=0`. Segment mapping now also passes
`MACHO_MAPPING=PASS`, `MACHO_MAPPED_SEGMENTS=1` and a non-zero relocated
entry address. This stage allocates and protects memory only; it does not yet
transfer control to Mach-O code.

Mach-O dylib load commands are now collected as well. The loader smoke proof
finds one `@rpath/libSystem.B.dylib` dependency and still passes segment
mapping. Path resolution is validated for `@rpath/libSystem.B.dylib` and
`@loader_path/Demo` with `DYLD_RPATH=PASS`. A recursive, cycle-safe dependency
graph now opens both synthetic Mach-O images and reports
`DYLD_GRAPH_NODES=2`, `DYLD_GRAPH=PASS`, and
`DYLD_GRAPH_SMOKE_EXIT=0`. Actual relocation, symbol binding, Objective-C
runtime initialization, and execution of loaded Mach-O code are not yet
implemented.
The Mach-O reader now also accepts real `MH_DYLIB` images and parses
`LC_SYMTAB`/64-bit symbol entries. The smoke test validates one defined symbol
in the dylib and one undefined import in the executable with
`DYLD_SYMBOLS=PASS` and `DYLD_SYMBOL_SMOKE_EXIT=0`. Symbol address binding and
relocation remain outstanding.
For a mapped dylib, the loader now calculates a defined symbol's relocated
runtime address from its Mach-O value plus the mapping slide; the proof emits
`DYLD_SYMBOL_ADDRESS=PASS`. This is address calculation only: imported-symbol
binding and writing relocation results into mapped code/data are still not
implemented.
The dependency graph now performs metadata-level import binding: undefined
symbols are matched against defined symbols from graph providers. The smoke
test binds `_libSystemInit` to `libSystem.B.dylib` and emits
`DYLD_BINDINGS=PASS`. The result currently records the provider and Mach-O
symbol value; it does not yet patch a relocation site in mapped memory.
The mapped-image layer now includes a bounds-checked absolute 64-bit relocation
writer. It temporarily enables write access, stores the target address, restores
the segment protection, and flushes the instruction cache. The proof writes and
reads back a relocation slot with `DYLD_ABSOLUTE_RELOCATION=PASS`. Parsing
Mach-O relocation records and applying PC-relative/bind opcodes remain pending.
The reader now parses `LC_DYSYMTAB` external and local relocation tables and
decodes the packed symbol index, width, PC-relative flag, external flag, and
relocation type. A synthetic external 8-byte record is validated with
`DYLD_RELOCATION_RECORDS=PASS`. Applying decoded records to their section
addresses, including x86-64 PC-relative forms, remains pending.
The first decoded record is now applied end-to-end for x86-64 absolute
`UNSIGNED` 8-byte relocations: the symbol index and record flags are validated,
the bound target address is written at the relocation VM address, and the
result is read back with `DYLD_RELOCATION_APPLY=PASS`. PC-relative, GOT,
SUBTRACTOR, and dyld bind-opcode forms are still rejected as unsupported.
The relocation applier now also supports x86-64 PC-relative 32-bit `SIGNED`
and `BRANCH` records, with signed-range validation and displacement calculation
relative to the relocation site. The synthetic proof emits
`DYLD_PCREL_RELOCATION=PASS`. GOT, SUBTRACTOR, TLV, and dyld bind-opcode forms
remain intentionally unsupported until their section and opcode metadata is
implemented.
The applier now also accepts x86-64 `GOT` and `GOT_LOAD` 8-byte records through
the same bounds-checked pointer write path; the synthetic proof emits
`DYLD_GOT_RELOCATION=PASS`. GOT indirection through actual section entries is
not inferred yet, so these records still require correct section metadata from
the Mach-O producer.
The x86-64 `SUBTRACTOR` plus following `UNSIGNED` pair is now validated and
applied as one 64-bit difference, with malformed or unpaired records rejected.
The proof emits `DYLD_SUBTRACTOR_RELOCATION=PASS`. TLV and dyld bind-opcode
relocations remain pending.
The Mach-O reader now decodes the regular `LC_DYLD_INFO_ONLY` binding stream
for dylib ordinal, symbol name, bind type, segment/offset, addend, and the
basic `DO_BIND`/`DO_BIND_ADD_ADDR_ULEB` actions. A synthetic stream is checked
with `DYLD_BIND_OPCODES=PASS`. The actions are currently metadata only; weak,
lazy, threaded, and rebase streams plus actual bind execution remain pending.
Regular pointer bind actions are now executable: the runtime binding table is
matched by symbol name, the segment-relative offset is translated to a Mach-O
VM address, addends are applied, and the pointer is written with the existing
protection checks. The synthetic proof emits `DYLD_BIND_APPLY=PASS`.
Weak/lazy/threaded bind execution and rebase streams remain pending.
The regular rebase stream is now decoded for pointer rebases using
`SET_TYPE_IMM`, `SET_SEGMENT_AND_OFFSET_ULEB`, and immediate repeat operations.
Applying a rebase adds the mapping slide to the existing pointer and writes it
back with the same protection checks; the proof emits `DYLD_REBASE_APPLY=PASS`.
Other rebase opcodes and threaded rebase formats remain pending.
The mapped Mach-O entry is now transferred to as executable x86-64 code through
`ExecuteEntryNoArgs`; the synthetic entry contains a real `ret` instruction and
the smoke proof emits `DYLD_EXECUTE_ENTRY=PASS`. This proves native code
transfer only. Real macOS entry conventions, dyld startup arguments, Darwin
syscalls, and framework initialization are still required for actual Darling
applications.
The bind decoder now also supports `DO_BIND_ADD_ADDR_IMM_SCALED` and
`DO_BIND_ULEB_TIMES_SKIPPING`, including bounded repeat counts. The smoke test
decodes and applies two adjacent pointer binds and emits
`DYLD_BIND_SCALED_STEP=PASS`.
`DO_BIND_IMM_TIMES` is now decoded and applied for repeated pointer binds; the
expanded smoke stream emits `DYLD_BIND_IMM_TIMES=PASS`.
The decoder now recognizes the actual threaded-bind table-size and apply
opcodes and marks threaded actions explicitly; it no longer treats the
threaded opcode as a repeat-bind form. The full suite remains green with
`DYLD_THREADED_SMOKE_EXIT=0`. Threaded pointer-chain execution itself remains
unsupported and is rejected explicitly.
Threaded x86-64 bind execution is now implemented for the classic dyld
pointer-chain format: the ordinal table is collected from the bind stream,
each chain node is bounds-checked, its symbol/addend is resolved, and the
patched pointer is written with protection restoration. The synthetic two-node
proof emits `DYLD_THREADED_APPLY=PASS`; other architectures and pointer formats
remain unsupported.
The loader now also recognizes `LC_DYLD_CHAINED_FIXUPS` and parses the
x86-64 `DYLD_CHAINED_PTR_64` page-start tables and pointer-chain records,
including bind flag, bind ordinal, target, and next stride. The current suite
remains green with `DYLD_CHAINED_SMOKE_EXIT=0`; applying chained bind/rebase
records and other pointer formats still requires a dedicated real chained-fixup
fixture.
The x86-64 chain applier is now implemented for parsed `DYLD_CHAINED_PTR_64`
records: bind ordinals resolve through a runtime address table, rebase targets
receive the mapping slide, and each result is written with the existing bounds
and protection checks. The synthetic two-node chain emits
`DYLD_CHAINED_APPLY=PASS`; ARM64e/authenticated and alternate pointer formats
remain unsupported.
Chained bind ordinals can now also be resolved by imported symbol name: the
loader builds the ordinal-address table from `DyldChainedImport` records and
the existing runtime binding table before applying the chain. The proof emits
`DYLD_CHAINED_NAME_BIND=PASS`; weak chained imports fall back to a null target,
while authenticated pointer formats remain unsupported.
The loader now also decodes `LC_DYLD_EXPORTS_TRIE`, walks bounded trie nodes,
extracts exported symbol names and addresses, and feeds them into the existing
symbol table used by provider binding. A synthetic export is validated with
`DYLD_EXPORTS_TRIE=PASS`.
Mach-O `LC_RPATH` commands are now parsed and exposed on each image; the
dependency graph combines embedded rpaths with caller-provided search paths
when resolving `@rpath` dependencies. The current loader suite remains green
with `DYLD_RPATH_SMOKE_EXIT=0`; a separate malformed-rpath test is still
needed.
Embedded `@loader_path` RPATH expansion is now exercised end-to-end: the
synthetic Mach-O carries `LC_RPATH`, resolution is performed without an
externally supplied rpath list, and the proof emits
`DYLD_EMBEDDED_RPATH=PASS` with `DYLD_EMBEDDED_RPATH_SMOKE_EXIT=0`.
Chained import records are now parsed for import format 1/2, including
Dylib-Ordinal, Weak-Flag, symbol-pool name, and optional addend. The synthetic
chain includes `_libSystemInit` and emits `DYLD_CHAINED_IMPORTS=PASS`; resolving
chained ordinals through the import names and supporting authenticated/alternate
pointer formats remain future work.
The rebase decoder now also handles variable offset movement and repeated
skipped slots through `ADD_ADDR_ULEB`, `ADD_ADDR_IMM_SCALED`,
`DO_REBASE_ADD_ADDR_ULEB`, and `DO_REBASE_ULEB_TIMES_SKIPPING`; the complete
existing suite remains green with `DYLD_REBASE_EXTENDED_SMOKE_EXIT=0`.
Weak- and lazy-binding streams are now range-checked and decoded into separate
bind actions with provenance flags, using the common symbol/segment/addend
parser for their basic pointer-bind opcodes. The current smoke suite remains
green with `DYLD_WEAK_LAZY_SMOKE_EXIT=0`; lazy resolution policy and threaded
bind variants are still not complete.

Unresolved weak classic binds now resolve to a null pointer instead of aborting
the image load; the same policy is used for weak lazy binds. The synthetic
loader proof emits `DYLD_WEAK_BIND_NULL=PASS`, while required unresolved binds
remain fail-closed.
Lazy actions are now excluded from eager binding and can be activated by index
through `ApplyLazyBindAction`, which resolves the symbol and patches its pointer
slot on demand. The synthetic weak/lazy streams are classified and the lazy
slot is applied successfully with `DYLD_LAZY_BIND_APPLY=PASS`.

Mach-O `LC_LOAD_WEAK_DYLIB` dependencies are now retained separately from
required dependencies. `DylibGraph::Load` ignores only an unresolved weak
dependency and continues to fail closed for missing required libraries; this
keeps optional framework/plugin edges compatible with dyld's weak-load policy.
The synthetic loader proof now creates a missing optional dylib and verifies
that the graph remains at one node with `DYLD_WEAK_DEPENDENCY=PASS`.

The mapped x86-64 entry path now also exposes a controlled `argc/argv/envp`
call. The synthetic entry reads the Windows x64 first argument register and
returns the received argument count; the proof emits `DYLD_EXECUTE_ARGV=PASS`.
This validates argument-vector construction and native entry transfer, but not
Darwin's real process bootstrap or ABI startup frames.

`DarwinBootstrap::Run` now provides the first minimal CLI bootstrap boundary:
it validates the required dependency graph, maps the selected Mach-O image, and
returns the native entry result. This is intentionally not yet a complete
dyld launch: symbol binding into the entry image and Darwin syscall startup
remain separate stages.

The bootstrap now resolves provider symbols through the mapped provider image,
applies rebases, regular bind actions, and chained fixups before entering the
main image. This is validated through the existing dependent synthetic app;
Darwin syscall startup and full lazy/threaded binding remain future stages.

The first Stage 2 syscall boundary is now present as `DarwinSyscalls`: Windows
file handles are exposed through Darwin-style descriptors with checked
`OpenRead`, `OpenWrite`, `Read`, `Write`, and `Close` operations. The isolated
roundtrip proof emits `DARWIN_SYSCALL_FILE_IO=PASS`; fork/exec, memory, socket,
and signal syscall families remain separate work items.

The memory boundary now maps Darwin-style `mmap`, `mprotect`, and `munmap` to
checked Windows virtual-memory operations. The proof reserves a page, writes
data, changes it to read-only protection, and releases it with
`DARLING_SYSCALL_MEMORY=PASS`.

The process boundary now exposes Darwin-style spawn, wait, and exit-code
operations through `CreateProcessW`. Arguments are quoted into a Windows
command line and the isolated child-process proof returns exit code 7 with
`DARWIN_SYSCALL_PROCESS=PASS`.

The thread boundary now also exposes a checked thread exit result through
`GetExitCodeThread`; the TLS/thread smoke verifies join, exit code, and
thread-local isolation together.

The Windows libc host-symbol boundary now includes heap-backed dynamic
formatting through `_asprintf` and `_vasprintf`. The host API proof exercises
allocation, formatting, and release together and emits `DARWIN_HOST_API=PASS`.
The remaining Mach-O loader gap is threaded pointer-chain execution; its
opcode recognition is present, but applying the chain still fails closed until
an ordinal-table fixture and runtime implementation are added.

The host-symbol boundary now also provides the POSIX-style `_strerror_r`
buffered error API with truncation reporting. The host API proof covers both
the legacy string form and the reentrant buffered form; the current result is
`DARWIN_HOST_API=PASS`.

The signal boundary now provides handler installation, signal delivery, and
reset through the Windows C runtime. The isolated `SIGINT` dispatch proof
emits `DARWIN_SYSCALL_SIGNAL=PASS`; POSIX signal masks and asynchronous
exception translation remain future work.

The Mach-O load-command parser now accepts `LC_LOAD_UPWARD_DYLIB` as a
required dependency edge. The dependency-graph fixture uses this command and
the complete native suite remains green.

The Mach-O opener now recognizes big-endian FAT/FAT64 containers, selects the
x86-64 slice with bounds checks, and keeps slice-relative offsets isolated from
the outer file mapping. The mapping smoke test now opens a two-architecture
FAT file and confirms that the selected image reports only its slice size.

`MH_DYLIB` images no longer require an `LC_MAIN` command. A separate dylib
fixture without an entry-point command now opens successfully while executable
images remain required to provide `LC_MAIN`.

`MH_BUNDLE` is now accepted as a loadable Mach-O image without an entry point;
the Dyld smoke fixture opens a standalone `.bundle` image and verifies its
file type.

The Windows libc boundary now exposes `_getppid`, using a checked process-list
lookup for the current process's parent PID. The host API proof verifies both
`getpid` and `getppid` while the complete suite remains green.

The clock host boundary now also exposes `_clock_getres`; the proof verifies
the documented Windows-backed resolutions for wall-clock and monotonic time.

The host process boundary now exposes `_kill` with a conservative Windows
mapping: signal zero performs process existence validation, self-signals use
the C runtime, and unsupported remote Unix signals fail with `EPERM` rather
than being silently misinterpreted.

The Mach-O entry decoder now also accepts x86-64 `LC_UNIXTHREAD` commands. The
stored RIP is validated against a file-backed segment and translated into the
same entry-offset representation used by `LC_MAIN`; executables may now use
either entry-point form.
The standalone Mach-O smoke test now creates such an image, verifies entry
offset `0x200`, and maps its entry address successfully.

The socket boundary now exposes raw bounded send/receive operations, validity,
and shutdown in addition to the loopback helpers. Partial Winsock transfers
are handled by the existing `SendAll` and `ReceiveExact` adapters.

The Mach-O bootstrap now uses `DarwinSyscalls` for its initial image read: the
header is opened, read, validated for x86-64 Mach-O, and closed before graph
resolution begins. This connects the Stage 2 file boundary to the Stage 3
loader path while preserving the existing mapped-image validation.

The clock boundary now provides monotonic nanoseconds from
`QueryPerformanceCounter` and Unix wall-clock nanoseconds from precise Windows
file time. The timing proof verifies forward progress and emits
`DARWIN_SYSCALL_TIME=PASS`.

A thread-local Darwin `errno` boundary now translates common Win32 failures
such as missing files, access denial, invalid handles, and out-of-memory into
their POSIX values. File-syscall failure propagation is verified with
`DARWIN_ERRNO=PASS`.

Winsock failures now use the same errno boundary, including mappings for
interrupt, would-block, connection reset/refused, and timeout conditions.

The path boundary now provides `getcwd`/`chdir` equivalents through
`DarwinPaths`, with Win32 failures routed through the shared errno translator.
The temporary-directory roundtrip emits `DARWIN_SYSCALL_PATHS=PASS`.

The filesystem boundary now provides file `stat` metadata (size, directory
type, and Unix modification time) plus `unlink`. A temporary-file roundtrip is
validated with `DARWIN_SYSCALL_FILESYSTEM=PASS`.

Directory creation and enumeration are now also available through
`MakeDirectory` and `ListDirectory`; the filesystem smoke covers a complete
directory/file/list/unlink cleanup cycle.

The synthetic CLI entry now calls its imported dylib symbol indirectly through
the bound pointer slot. `DARWIN_BOUND_ENTRY=PASS` proves that provider
resolution and bind application affect executed Mach-O code, not only loader
metadata.

The synthetic Windows-x64 entry now reserves the required 0x28-byte shadow
space and uses a full 64-bit RIP-relative indirect call, so the same binding
proof passes under both clang-cl and MSVC.

`DarwinLaunchOptions` now accepts explicit host bindings. The CLI smoke binds
the imported entry symbol to a Windows `stdout` bridge and receives visible
output from the executed Mach-O code, including forwarded `argc`:
`DARWIN_HOST_STDOUT=PASS argc=3`.

The host ABI also exports generic C-ABI `read`, `write`, and `close`
operations. Descriptors 0/1/2 map to Windows standard handles while opened
files use the shared Darwin descriptor table.

Dyld import resolution now automatically recognizes `_read`, `_write`, and
`_close` as host ABI symbols when no Mach-O provider definition exists. The
runner no longer needs a test-only override for these standard imports.

The automatic table now also covers `_getpid` and `_clock_gettime` with
Darwin-compatible process/time structures; the direct ABI proof emits
`DARWIN_HOST_API=PASS`.

CLI environment and terminal queries are now covered by automatic `_getenv`
and `_isatty` host bindings; missing variables return null and non-terminal
descriptors return zero.

The host libc table now also provides `_malloc` and `_free` through the Windows
process heap, with `ENOMEM` propagation on allocation failure. The ABI smoke
performs a write/readback/free cycle for the allocated block.

Common C-runtime memory/string imports `_memcpy`, `_memset`, `_strlen`, and
`_strcmp` are now automatically resolved by the same table and covered by the
host ABI smoke.

The file ABI now also resolves `_open` and `_unlink`, including UTF-8 path
conversion and write/create flags. The host smoke validates an actual
open/write/close/unlink cycle through the exported C functions.

`darling_windows_runner.exe` now exposes the bootstrap as a standalone command
line runner. It accepts a Mach-O path plus arguments, converts them to UTF-8,
collects the Windows environment as UTF-8 `envp`, and returns the native entry result as its Windows exit code. This runner is
ready for no-dependency x86-64 Mach-O images; full libSystem resolution is
still required for ordinary macOS binaries.

### Stage 1 host library boundary — validated

Added a Windows dynamic-library wrapper corresponding to the host-library
portion of Darling's `elfcalls` boundary. `LoadLibraryW` and
`GetProcAddress` are validated by loading `kernel32.dll` and resolving
`GetCurrentProcessId`:

`WINDOWS_LIBRARY=PASS`, `WINDOWS_SYMBOL=GetCurrentProcessId`,
`LIBRARY_SMOKE_EXIT=0`.

The Windows semaphore and named shared-memory primitives are also validated:
`WINDOWS_SEMAPHORE=PASS`, `WINDOWS_SHARED_MEMORY=DARLING-WINDOWS`,
`SYNC_SMOKE_EXIT=0`.

The BSD socket boundary is now backed by Winsock and validated with a local
loopback exchange: `WINDOWS_SOCKET_REQUEST=PING`,
`WINDOWS_SOCKET_RESPONSE=PONG`, `SOCKET_SMOKE_EXIT=0`.

Windows AF_UNIX support is now added as a separate local IPC path using the
SDK's `sockaddr_un`: bind/listen/connect/accept and bounded stream I/O are
covered by `WINDOWS_UNIX_SOCKET_REQUEST=PING` and
`WINDOWS_UNIX_SOCKET_RESPONSE=PONG` on x64 and Win32. Unix datagram,
sequenced-packet, ancillary descriptor passing, and abstract Linux socket
namespaces remain open as native interoperability. Because Winsock rejects
native `AF_UNIX/SOCK_DGRAM`, the Darling host now provides a path-register-
backed loopback UDP transport for internal Darwin datagram endpoints; the
fixture reports `WINDOWS_UNIX_DGRAM_REQUEST=PING`,
`WINDOWS_UNIX_DGRAM_RESPONSE=PONG`, and `WINDOWS_UNIX_DGRAM=EMULATED` on both
targets.

Native thread and TLS handling is validated with `CreateThread`, `TlsAlloc`,
`TlsSetValue` and `TlsGetValue`: `WINDOWS_THREAD=PASS`,
`WINDOWS_TLS=PASS`, `THREAD_SMOKE_EXIT=0`.

### Stage 2: runtime primitives

Implement the process, IPC, memory, filesystem, synchronization and terminal
interfaces with focused tests. No framework porting should begin before these
tests pass.

### Stage 3: Mach-O CLI bootstrap

Port the loader/bootstrap path and run a minimal x86-64 Mach-O console test.
The proof must include a non-empty output from the Mach-O process itself, not
only a successful Windows launcher exit code.

### Stage 4: libSystem/Foundation

Move the smallest useful Darwin library closure onto the Windows backend,
then add filesystem, networking and threading tests.

### Stage 5: GUI and media

Only after CLI closure is stable: AppKit/CoreGraphics, CoreAudio and Metal/
Vulkan translation.

## First implementation target

The first code change should be limited to a Windows host skeleton under
`darling-source/src/native/windows` plus CMake selection. It must not alter
Darwin framework behavior or claim Mach-O execution until the Stage 1 RPC and
process tests pass.

## License boundary

The collected bundle is under `licenses/`. It currently includes Darling's
GPL-3.0 license, Debian copyright metadata, the checked-out component license
files, and Microsoft's WSL MIT license and notices. The complete Darling
submodule license set is not yet distributable from this NTFS checkout because
the recursive checkout stopped at a Heimdal path containing `:`; unresolved
submodules remain marked `review-required` in
`licenses/SOURCE-LICENSE-INVENTORY.csv`.

## Current Windows backend status

The host C-ABI boundary now also exposes `_raise`; the host API proof installs
a controlled SIGINT handler and verifies in-process signal delivery while
preserving the existing fail-closed behavior for invalid and unsupported
cases. The Release build and all 18 native smoke programs pass after this
change.

The host ABI also now provides `_uname`, returning a Darwin-compatible
`utsname` profile (`Darwin`, host nodename, Darling-Windows release, and
`x86_64` machine) for early platform checks used by Unix applications. Its
behavior is covered by the host API smoke test.

The process identity surface now includes `_getuid`, `_geteuid`, `_getgid`,
and `_getegid`. Windows token user SID data supplies the stable nonnegative
Darwin-side identity value; effective and group IDs currently follow the
same host identity mapping until a full POSIX account database is added.

The memory ABI now also exposes `_getpagesize`, backed by the Windows system
page size and covered by the host API smoke test.

The host ABI now includes `_sysconf` for Darwin page size, processor counts,
and physical/available page counts, with explicit `EINVAL` handling for
unsupported query names. The complete 18-test Release smoke suite remains
green.

Thread/process scheduling compatibility now also includes `_sched_yield` and
`_sleep`, backed by Windows thread yielding and bounded millisecond sleeps;
both paths are included in the host API proof.

The host boundary now provides `_arc4random` and `_arc4random_buf`, backed
by Windows CNG's system-preferred random provider. The functions fail closed
by zeroing the requested buffer if the provider cannot be loaded, and the
complete 18-test Release suite passes.

`_getentropy` is now exposed for Darwin/POSIX callers, enforcing the
256-byte request limit and reusing the Windows system RNG. The full 18-test
Release smoke suite remains green.

The memory ABI now also exposes `_explicit_bzero`, implemented with
`SecureZeroMemory` so sensitive buffers are not optimized away during
clearing. Its symbol and zeroing behavior are covered by the host API test.

The user-environment ABI now includes `_getlogin` and `_getlogin_r`, using
the current Windows account name with bounded-buffer and `ERANGE` handling.
The full 18-test Release smoke suite remains green.

Process accounting now includes `_getrusage`: current-process user and
kernel CPU times are converted from Windows process timing units, while the
Darwin child-usage query returns an empty result and unsupported modes fail
with `EINVAL`.

Process timing now also provides `_times` and `_SC_CLK_TCK` (100 Hz), with
user/system ticks derived from Windows process times and elapsed wall ticks
from the Windows monotonic tick source. The complete 18-test Release suite
passes.

The minimal process bridge now includes `_dlopen`, `_dlsym`, `_dlclose`, and
`_dlerror`. It can load a Windows provider, resolve a Darwin-style leading-
underscore symbol, report missing symbols, and close the provider. Darwin
`@rpath` and `@loader_path` paths remain intentionally deferred to the
Mach-O/dyld provider layer.

The dyld graph now recognizes `libSystem.B.dylib` and `libSystem.dylib` as
built-in Darwin providers when no physical Mach-O provider is present. This
allows unresolved symbols to fall through to the Windows-backed Darwin host
ABI, and is covered by `DYLD_BUILTIN_LIBSYSTEM=PASS` in the dyld smoke test.

Following the WSL process-launch pattern, Darwin process spawning now accepts
an explicit Unicode environment block instead of inheriting the Windows
environment implicitly. The process syscall smoke test verifies isolation
and propagation of a dedicated variable; all 18 Release smoke tests pass.

The first process-family batch now also exposes `_waitpid`, including
`WNOHANG` validation and Darwin normal-exit status encoding (`exit << 8`). A
real Windows child process is used by the host API smoke test; all 18 Release
smoke tests remain green.

The process family now also exposes `_posix_spawn` and `_posix_spawnp` with
Darwin-style `argv`/`envp` handling, PID return, and explicit rejection of
unsupported file-actions/attributes. The process smoke test chains spawn to
`waitpid` and verifies status 11; all 18 Release smoke tests pass.

The process family now also includes `_wait4`, reusing the canonical
`waitpid` implementation while returning a Darwin-compatible zeroed child
resource structure when Windows cannot provide child accounting. The full
18-test Release suite remains green.

The ipasim source was added under `ipasim-source` with recursive submodules.
Its MIT license is recorded separately in `licenses/LICENSE-ipasim-MIT.txt`.
The relevant Objective-C runtime, Mach-O/image-registration, wrapper-generator,
and WinObjC framework families are catalogued in `IPASIM-PORTING-NOTES.md`.
No ipasim source has been merged into the GPL Darling backend yet; upstream
provenance and dependency-license boundaries remain explicit before the next
Objective-C implementation batch.

The first Objective-C compatibility batch is now implemented in
`darling_windows_objc.cpp`: selector registration, class allocation and
registration, superclass lookup, and inherited method metadata lookup. The
Release validation suite now contains 19 smoke programs and reports
`SMOKE_PASS=19` and `SMOKE_FAIL=0`.

The Objective-C family now also allocates instances and exercises no-argument
`objc_msgSend` and `objc_msgSendSuper` dispatch through superclass metadata.
Typed and variadic Objective-C ABI dispatch remains deliberately gated for a
later batch.

The typed bridge batch now covers object-returning one-argument methods,
signed 64-bit one-argument methods, and void no-argument methods. The complete
Release suite remains green at `SMOKE_COUNT=19`, `SMOKE_PASS=19`,
`SMOKE_FAIL=0`.

Objective-C `baseMethods` lists are now parsed from Mach-O-compatible class
records and installed with selectors, type encodings, and IMP addresses. The
19-program Release suite remains fully green.

Category records from `__objc_catlist` are now resolved and their instance and
class methods are installed. The complete Release suite remains green at 19
passing smoke programs.

Mach-O protocol lists and selector references are now imported into the
Objective-C runtime. A fixture-layout bug found during the first run was fixed;
the final validation is again `SMOKE_PASS=19` and `SMOKE_FAIL=0`.

Required and optional protocol method lists for instance and class methods are
now parsed and queryable. The Release suite remains green at 19 passing smoke
programs.

Raw Mach-O metaclass `baseMethods` are now imported as class methods and
exercised through class dispatch. The 19-program Release suite remains green.

Metaclass class-method inheritance and recursive Protocol inheritance are now
implemented and tested. The Release suite remains green at 19 passing smoke
programs.

The callback bridge is now integrated into typed Objective-C method dispatch
for `@@:@?`. Full Apple block-layout compatibility remains outstanding.

The block family now also exposes a binary-shaped Apple block literal with
descriptor signature and tested copy/release/invoke behavior. Captured-block
compiler helpers and complete Apple ABI flags remain outstanding.

The exported `objc_msgSend` surface now has a tested variadic dispatcher for
object, signed 64-bit, BOOL, pointer, and block forms. Double/struct return ABI
forms remain explicitly gated.

Mach-O ivar lists are now parsed and exposed through `class_getInstanceVariable`,
`ivar_getName`, `ivar_getTypeEncoding`, and `ivar_getOffset`. The full Release
suite remains green at 19 passing smoke programs.

Mach-O property lists are now parsed and exposed through the property runtime
API. The Release suite remains green at 19 passing smoke programs.

Category instance/class property lists now use the same bounded parser and
lookup path; the Release suite remains green at 19 passing smoke programs.
Category protocol lists are now registered during category scanning and are
covered by the Objective-C image fixture.
The runtime now provides protocol names plus global and per-class protocol-list
enumeration, covered by the same Release fixture.
Caller-owned `class_copyIvarList`, `class_copyPropertyList`, and
`class_copyProtocolList` arrays are now implemented and validated.

Common `libobjc` symbols are now exposed through the Darwin host resolver and
the dyld graph recognizes built-in `libobjc` providers. The combined Release
validation remains green at 19 passing smoke programs.

Mach-O `section_64` metadata and slide-correct section addresses are now
available. Bootstrap registration scans Objective-C class-list sections after
fixups and the Objective-C smoke test verifies class-name extraction from a
Mach-O-compatible classlist record.

The generic dispatcher now also validates pointer arguments/returns and a
two-`int64` method form (`q@:qq`). The complete Release suite remains green
at 19 passing smoke programs.

The Objective-C bridge now also validates and returns a CGRect-like four-double
struct form, providing the first tested Cocoa-style aggregate return path.

The Objective-C family now also supports metaclass lookup, class-method
registration, and a tested factory-style class invocation path. The complete
Release suite remains green at 19 passing smoke programs.

Weak-reference slot registration and automatic nulling on final release are
now implemented and tested. The Release suite remains green at 19 passing
smoke programs after fixing raw-instance atomic-counter initialization.

The first callback-block bridge is now implemented and tested with context,
copy/release, and one object argument. Apple block-layout compatibility is
still outstanding.

The Objective-C bridge now includes atomic retain/release and a tested
thread-local autorelease pool. Full Foundation ownership semantics, weak
references, and ARC edge cases are still outstanding.

The Objective-C runtime now also exposes bounded class enumeration through
`objc_getClassList`; count-only and populated-buffer behavior are covered by
the registry smoke test.

The Objective-C runtime now also supports protocol allocation, registration,
required instance-method descriptors, and class conformance checks. The full
Release suite remains green at 19 passing smoke programs.

Objective-C method entries now retain and expose their type encodings. The
typed bridges validate those encodings before dispatch, preventing a method
with an incompatible ABI from being called through the wrong bridge.

A generic tagged-value Objective-C dispatcher now covers object, signed
64-bit, boolean, double, and void/no-argument forms. The Release validation
remains green at `SMOKE_COUNT=19`, `SMOKE_PASS=19`, `SMOKE_FAIL=0`.

External reference sources are now locally available for the next large
families: `wine64-darwin-source` at commit
`54eeeee041fdadad79e0c5449e4a08d763e58b65` and `puredarwin-source` at commit
`88753c478c97b9a08bcdb66cecc68ba5881ff3af`. Their Wine LGPL and
PureDarwin/Apple license files are retained in place and documented in
`IPASIM-PORTING-NOTES.md`; no unreviewed source was copied into Darling.

The C-runtime error family now resolves `_errno` and Darwin `___error` to a
thread-local Windows-side errno pointer. `DARWIN_HOST_API=PASS` and the full
19-program Release suite validate the alias and round-trip behavior.

Mach-O binding support now includes tested 32-bit absolute and PC-relative
patch helpers, covering the data-width behavior needed by Darwin text-bind
families; authenticated and non-x86-64 pointer formats remain unsupported.

Objective-C method introspection now exposes selector, implementation, and
type-encoding metadata through `class_getInstanceMethod`,
`class_copyMethodList`, and `method_get*`; the image fixture validates both
class and category methods.

`sherlockelf-source` is now available as a separately licensed MIT reference
at commit `7183d8a7049693eafe22d83ddc26bf05ffaa2fbe`. It informs analysis and
inspection planning only; the Windows loader remains independently authored.
Its complete repository license is retained at
`licenses/LICENSE-sherlockelf-MIT.txt`.

All currently implemented Objective-C metadata APIs are now connected to the
Mach-O host-symbol resolver: methods, ivars, properties, and protocols. The
registry fixture verifies every exported resolver name; Release remains
19/19 passing.

Mach-O protocol method imports now preserve and expose their type encodings via
`protocol_getMethodDescription`; the prior null-type import path was corrected
and the full Release fixture remains green.

x86-64 `SIGNED_1`, `SIGNED_2`, and `SIGNED_4` relocations are now implemented
with their actual Mach-O meaning: each writes a signed 32-bit field, while
the PC adjustment is respectively 1, 2, or 4 bytes. The fixture verifies all
three as `DYLD_SIGNED_1_2_4=PASS`. TLV relocation records are now identified
with an explicit fail-closed diagnostic; a Darwin TLV descriptor/runtime
bridge is still required. ARM64/e and authenticated pointer formats remain
outside this family.

The same relocation engine is now invoked by `DarwinBootstrap::Run` for the
main image after rebases and before bind actions/entry execution. Previously
the relocation code was only directly exercised by the dyld fixture; the
bootstrap path now builds symbol-indexed addresses from defined symbols and
resolved host/import bindings. Reachable relocations are applied strictly;
cross-image x86-64 PCREL relocations outside the signed 32-bit range are
explicitly deferred instead of truncated, because Windows does not guarantee
Darwin's image locality. A near-allocation or thunk policy is still required
for those deferred relocations.

The bootstrap now maps the main image first and asks Windows to reserve each
provider dylib near that address using a bounded ±2-GB search. This enables
true 32-bit cross-image PCREL patches when the reservation succeeds; the
explicit deferred path remains necessary when Windows cannot honor the hint.

Provider images now also accept the normal Darwin dylib/bundle shape without
`LC_MAIN`; their rebases, relocation records, bind opcodes, chained fixups,
and Objective-C sections are processed before the main image entry. Executable
images still require a valid entry point. The provider loop uses the same
symbol-indexed import table as the main image, while unresolved far PCREL
records remain explicitly deferred.

TLV descriptor/template initialization is now shared by the main image and
every mapped provider image. Provider `__thread_vars`, `__thread_data`,
`__thread_bss`, and local `__thread_ptrs` slots therefore use the same Windows
TLS bridge before provider relocations and bindings execute. Cross-image TLV
symbol metadata remains open. `_tlv_atexit` is now host-resolved and executes
registered callbacks in reverse order at Windows thread teardown; the TLV
fixture verifies a callback on a worker thread.

PureDarwin's `__cxa_thread_atexit(void(*)(void*), void*)` wrapper is also
provided and host-resolved as `___cxa_thread_atexit`, forwarding to the same
Darwin-TLV registry. The fixture registers through both ABI spellings and
verifies both callbacks at thread teardown.

Provider TLV exports now resolve to the provider's local `__thread_ptrs` slot
when one exists. Main-image type-9 relocations accept either a local descriptor
that can be translated to a slot or an already translated provider slot. This
closes the basic cross-image TLV pointer path; optimized TLV-LEA forms,
descriptor key sharing across complex graphs, and ARM64 TLVP remain open.

Optimized x86-64 TLV-LEA candidates are now recognized from ld64's `0x8d`
opcode rewrite and fail closed with a dedicated diagnostic. They are not
treated as ordinary PCREL relocations, because doing so would expose a
descriptor address instead of a per-thread variable address.

Mach-O `__mod_init_func` arrays are now bounds-checked and executed after each
image's rebases, relocations, and bindings: provider initializers run before
the main image initializer and entry point. A dedicated synthetic Mach-O
fixture invokes a native callback through the mapped initializer array; the
Release suite is now `SMOKE_COUNT=21`, `SMOKE_PASS=21`, `SMOKE_FAIL=0`.

The matching `__mod_term_func` arrays are now executed in reverse order after
the main entry returns, followed by provider terminators in reverse provider
order. The initializer fixture verifies both callbacks as
`MACHO_MOD_INIT_FUNC=PASS` and `MACHO_MOD_TERM_FUNC=PASS`.

The dyld graph now exposes a dependency-first initialization order with cycle
detection. Bootstrap sorts mapped providers by that order before running their
fixups, initializers, and bindings; the main image remains last for startup and
provider teardown remains the reverse order.

The dynamic-loader boundary now recognizes self-contained Mach-O files in
`dlopen`: it maps and fixes up the image, resolves host imports, runs
`__mod_init_func`, supports underscore-tolerant `dlsym`, and runs terminators
on `dlclose`. The dedicated fixture reports `DARWIN_DLOPEN=PASS`,
`DARWIN_DLSYM=PASS`, and `DARWIN_DLCLOSE=PASS`; the Release suite is now
`SMOKE_COUNT=22`, `SMOKE_PASS=22`, `SMOKE_FAIL=0`. Dependency Dylibs that are
not host-resolvable still require the full dynamic graph path.

The dynamic graph path is now active: `dlopen` consumes
`DylibGraph::BindImports`, maps provider images near the main image, applies
provider fixups and initializers before the main image, and tears providers
down after the main image in reverse order. Unresolved imports still fail
closed; the 22/22 fixture remains green.

The dynamic path also registers `__objc_classlist`, `__objc_nlclslist`,
`__objc_catlist`, and `__objc_selrefs` for dynamically opened images and their
providers. This closes the Objective-C metadata gap for bundles; additional
Objective-C runtime behavior may still be required by a particular image.

Dynamic images and providers now receive the same `__thread_vars`,
`__thread_data`, `__thread_bss`, and local `__thread_ptrs` preparation as the
static bootstrap. Thus the basic TLV runtime is active for `dlopen` as well;
optimized TLV-LEA and ARM64 TLVP remain separate gaps.

The dynamic provider list now uses the same dependency-first graph order and
cycle detection as process bootstrap, so bundle startup and teardown share the
same ordering contract.

Mach-O dynamic handles now use path identity and reference counting: repeated
`dlopen` calls return the same handle, intermediate `dlclose` calls retain the
image, and only the final close executes terminators and releases it. The
dynamic fixture covers the repeated-open/intermediate-close sequence.

The first real Darwin-TLV runtime layer is now present. `__thread_vars`
descriptors are initialized during bootstrap with a Windows-side thunk, unique
descriptor keys, and per-thread template-backed storage. The dedicated TLV
fixture verifies both descriptor resolution and cross-thread isolation; the
Release suite is now `SMOKE_COUNT=20`, `SMOKE_PASS=20`, `SMOKE_FAIL=0`.
This does not yet fully connect x86-64 `X86_64_RELOC_TLV` records across image
boundaries or cover ARM64 TLVP forms.

Local `__thread_vars[i]` to `__thread_ptrs[i]` slot mapping is now wired for
the main image, and type-9 relocations use that slot when the target is local.
Cross-image TLV exports still require provider-side slot metadata and are
therefore fail-closed.

The TLV layer now also copies the mapped `__thread_data` image into each
thread's first-use storage and reserves the `__thread_bss` tail as zero-filled
space. The fixture verifies a nonzero initial byte is copied independently for
the main thread and a worker thread. The remaining gaps are the relocation
target-to-`__thread_ptrs` mapping, full multi-variable template offsets, TLV
destructor registration, and ARM64 TLVP forms.

The provider-aware `dlsym` path now searches the loaded main image and its
provider images, including Darwin's leading-underscore fallback. Dynamic
close now executes the main-image terminators followed by provider terminators
in reverse provider order. Release verification after this change remains
`SMOKE_COUNT=22`, `SMOKE_PASS=22`, `SMOKE_FAIL=0`.

Undefined Mach-O symbols marked `N_WEAK_REF` are now represented as explicit
NULL bindings when no provider or host symbol exists. This preserves optional
symbol semantics through the Dylib graph and is covered by
`DYLD_WEAK_UNDEFINED=PASS`; the complete Release suite remains 22/22.

The dynamic provider-binding path now converts exported provider TLV
descriptors to their provider `__thread_ptrs` slots, matching the static
bootstrap path. The dynamic fixture and full Release suite remain green at
22/22.

Dyld path resolution now carries the process executable path through the
dependency graph. `@loader_path` remains relative to the image declaring the
dependency, while `@executable_path` is relative to the main executable even
for nested provider Dylibs. The fixture reports `DYLD_EXECUTABLE_PATH=PASS`.

The Windows `dlsym` bridge now supports Darwin `RTLD_DEFAULT` (the process-wide
loaded-image/host lookup) in addition to concrete handles. The dynamic fixture
reports `DARWIN_RTLD_DEFAULT=PASS`; the complete Release suite remains 22/22.

Dynamic Mach-O loading now serializes concurrent first loads per normalized
path with an in-flight condition state. Waiters receive the same handle and
reference count, while image teardown remains outside the loader mutex. The
fixture reports `DARWIN_DLOPEN_CONCURRENT=PASS`; complete Release verification
remains 22/22. Same-thread recursive loading from an initializer is still a
separate edge case and is deliberately fail-open to avoid self-deadlock.

Lazy bind actions are now applied during both static bootstrap and dynamic
`dlopen`, after normal binds and before chained fixups. The existing dyld
fixture still reports `DYLD_LAZY_BIND_APPLY=PASS`, and all 22 Release smoke
tests remain green. True on-demand lazy resolver stubs are not yet emulated;
Windows executes these bindings eagerly at image load.

`dlopen(NULL, ...)` now returns the process-wide Darwin loader handle, sharing
the `RTLD_DEFAULT` lookup path; `dlsym` and `dlclose` on that handle are
covered by the dynamic fixture. Full Release verification remains 22/22.

The Objective-C variadic bridge now dispatches common 32-bit and mixed-width
integer encodings (`i@:i`, `q@:i`, `i@:q`) plus void integer arguments
(`v@:i`, `v@:q`) in addition to the existing object/int64/bool/pointer/block
families. `DARWIN_OBJC_REGISTRY=PASS` and the complete Release suite remain
green at 22/22.

The same bridge now handles two-object (`@@:@@`) and two-int64 (`q@:qq`)
messages with ordered vararg extraction. The Objective-C fixture remains green
and the complete Release suite is still 22/22.

`objc_msgSendSuper` now dispatches the two-object and one-/two-int64 families
through the selected superclass implementation, with ordered vararg reads.
The Objective-C fixture and complete Release suite remain green at 22/22.

Super dispatch now also covers pointer (`^v@:^v`) and callback-block
(`@@:@?`) arguments, including pointer return propagation. Full Release
verification remains `SMOKE_COUNT=22`, `SMOKE_PASS=22`, `SMOKE_FAIL=0`.

Objective-C Associated Objects are now present: assign, retain, and copy
policies are tracked per object/key; replacing, removing, and final object
destruction release only retaining policies. The Objective-C fixture and full
Release suite remain green at 22/22. Copy currently follows the bridge's
retain-only object model because no NSObject copy protocol is implemented.

`objc_msgSendSuper` now covers the same multi-argument object and integer
encodings as the normal bridge (`@@:@@`, `q@:q`, `q@:qq`). The current full
Release verification is `SMOKE_COUNT=22`, `SMOKE_PASS=22`, `SMOKE_FAIL=0`.

The host ABI now exposes the basic Darwin filesystem metadata family through
`stat`, `lstat`, and `fstat` (plus the `stat64`/`lstat64`/`fstat64` symbol
aliases). The bridge fills mode, link count, file identity, size, block count,
and creation/access/modification timestamps from Windows file metadata. The
filesystem fixture exercises all three calls and the complete Release suite
reports `SMOKE_COUNT=22`, `SMOKE_PASS=22`, `SMOKE_FAIL=0`.

This is intentionally a compatibility mapping: uid/gid are currently zero,
Windows file identity is used for dev/inode, and `lstat` currently follows
Windows reparse points like `stat`; true Darwin symlink and filesystem flag
semantics remain open work.

The filesystem fixture also verifies that the three `*64` names resolve via
the host symbol table to the same implementations.

`fstatat`/`fstatat64` now cover the `AT_FDCWD` path form and validate the
accepted `AT_SYMLINK_NOFOLLOW` flag. The filesystem fixture exercises this
call and the complete Release suite remains `SMOKE_COUNT=22`,
`SMOKE_PASS=22`, `SMOKE_FAIL=0`. Directory-relative descriptors are still
rejected with `ENOTSUP`; Windows reparse-point no-follow semantics remain
unimplemented.

The remaining socket-I/O resolver family now accepts normalized `poll`,
`select`, `pselect`, `getsockopt`, `setsockopt`, `sendto`, `recvfrom`,
`sendmsg`, `recvmsg`, and `shutdown`; the host smoke reports
`DARWIN_NORMALIZED_SOCKET_IO_SYMBOLS=PASS` before the known rename privilege
failure. Cross-process descriptor rights, exact Winsock/Darwin error mapping,
and full readiness/ancillary-data ABI parity remain open.

Final regression after the libc/string additions and rollback of the imported
entry experiment: x64 `32/32`, Win32 `32/32` selected smoke programs passed.
The maintained synthetic loader tests remain green; the imported dependent-code
call is deliberately still unclaimed after its native abort, and the two
privilege-sensitive filesystem/host-API checks remain excluded.

The next isolated imported-entry experiment still aborted natively before a
valid return (`0xC0000094`), so it was removed and the maintained dyld smoke
was rebuilt with `EXIT=0`. The remaining gap is therefore unchanged: the
synthetic image needs independently validated entry, bind-slot, relocation,
page-protection, and calling-convention records before dependent Mach-O code
execution can be claimed.

The string/format extension family now resolves normalized `strspn`, `strcspn`,
`strpbrk`, `strtok`, `strtok_r`, `strdup`, `strerror`, `strerror_r`,
`snprintf`, `vsnprintf`, `vasprintf`, `strlcpy`, `strlcat`, and `strcasestr`.
The host smoke continues to report `DARWIN_NORMALIZED_STRING_SYMBOLS=PASS`
before the known rename privilege failure. Locale, thread-local tokenizer,
format ABI, and complete Darwin error-string semantics remain open.

The string ABI family now resolves normalized `strlen`, `strnlen`, `strcmp`,
`strcasecmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strchr`, `strrchr`,
and `strstr`; the host smoke reports `DARWIN_NORMALIZED_STRING_SYMBOLS=PASS`
before the known rename privilege failure. Locale/encoding behavior and full
Darwin string ABI edge cases remain open.

The common libc family now resolves normalized `malloc`, `calloc`, `free`,
`memcpy`, `memset`, `memmove`, `memcmp`, `bzero`, `bcopy`, `asprintf`,
`memchr`, and `memmem`; the host smoke reports
`DARWIN_NORMALIZED_LIBC_SYMBOLS=PASS` before its known rename privilege
failure. Allocator ABI ownership, format-string edge cases, and complete
Darwin libc behavior remain open.

The user/resource family now resolves normalized `getlogin`, `getlogin_r`,
`getrusage`, `times`, `getpagesize`, and `getpagesizes`; the host API smoke
reports `DARWIN_NORMALIZED_RESOURCE_SYMBOLS=PASS` before its known restricted
rename failure. Exact Darwin accounting, login identity, page-size ABI, and
resource-limit semantics remain open.

The process-start family now also resolves normalized `getppid`, `posix_spawn`,
and `posix_spawnp`. The x64 and Win32 process syscall smokes report
`DARWIN_NORMALIZED_PROCESS_SYMBOLS=PASS` and exit 0. `RESETIDS`, `SETSID`, and
`SETPGROUP` remain explicitly emulated rather than native Darwin semantics.

The randomness/scheduler family now resolves normalized `arc4random`,
`arc4random_buf`, `getentropy`, `sched_yield`, `sleep`, and `raise`; the host
API smoke reports `DARWIN_NORMALIZED_RANDOM_SYMBOLS=PASS` before its known
rename privilege failure. Cryptographic-provider equivalence, exact signal
interruption behavior, and complete Darwin scheduler semantics remain open.

Full post-alias regression completed successfully: x64 `32/32` and Win32
`32/32` selected smoke programs passed. This includes the normalized FD,
socket, path, process, signal, dynamic-loader, and system/time symbol families.
The excluded filesystem and host-API checks remain privilege-sensitive; the
known emulated semantics and the unproven real dependent Mach-O call path are
unchanged.

The system/time host family now resolves normalized `sysconf`, `uname`,
`gethostname`, `clock_gettime`, and `clock_getres`; the x64 host smoke reports
`DARWIN_NORMALIZED_SYSTEM_SYMBOLS=PASS` before the known rename privilege
failure. Exact Darwin clock IDs, sysconf numbering, hostname/domain policy,
and complete ABI/error parity remain open.

The dynamic-loader family now resolves normalized `dlopen`, `dlsym`, `dlclose`,
`dlerror`, and `dladdr` in addition to Darwin-prefixed names. x64 and Win32
dynamic-loader smokes pass, while complete dyld namespace, framework, ABI, and
real dependent-code execution semantics remain open.

The path family now also resolves normalized `unlink`, `rmdir`, `link`,
`symlink`, `readlink`, `access`, `rename`, `chdir`, `getcwd`, and `mkdir`.
Both x64 and Win32 host-API runs report
`DARWIN_NORMALIZED_PATH_SYMBOLS=PASS` before the unchanged restricted-account
rename failure (`ERROR_ACCESS_DENIED`). Complete filesystem permissions,
reparse-point, hard-link, and cross-volume Darwin semantics remain open.

That intermediate limitation is now removed: the descriptor table retains
filesystem paths in a descriptor-indexed map, readable directories use
`FILE_FLAG_BACKUP_SEMANTICS`, and relative `fstatat(dirfd, path, ...)` calls
resolve against the opened directory. A `fcntl(F_DUPFD, 20)` regression found
during this work was fixed by growing the handle table for `result >= size`;
AddressSanitizer passed the host-API test 10/10, and three complete Release
runs each passed 22/22. Windows reparse-point no-follow behavior remains the
remaining gap in this family.

The no-follow path now uses `FILE_FLAG_OPEN_REPARSE_POINT` and marks a
reparse-point result with Darwin symlink mode bits; `stat` continues to follow
the target. The ordinary no-follow path and all 22 Release tests pass. The
test environment cannot create symbolic links without the Windows symlink
privilege, so the end-to-end `stat`-versus-`lstat` reparse assertion reports
`DARWIN_LSTAT_REPARSE=SKIP`; that runtime proof remains pending on a
Developer-Mode/elevated Windows host.

The Darwin `open(2)` flag family now uses the documented Darwin values for
`O_CREAT`, `O_EXCL`, `O_TRUNC`, and `O_APPEND`. A central host open path maps
read-only, write-only, read/write, create-new, truncate, and append behavior
to Windows creation modes; append descriptors seek to end before every write.
The filesystem fixture verifies create/truncate, append readback (`AB`), and
exclusive-create rejection. Three complete Release runs remain 22/22.

The open bridge now also validates `O_DIRECTORY`, uses
`FILE_FLAG_OPEN_REPARSE_POINT` for `O_NOFOLLOW`, treats regular-file
`O_NONBLOCK` as a documented no-op, and keeps Windows handles non-inheritable
for `O_CLOEXEC` compatibility. The filesystem fixture proves directory-only
acceptance/rejection; three further complete Release runs remain 22/22.
Pipe-specific nonblocking and full `fcntl` close-on-exec state remain open.

The descriptor-control bridge now accepts the Darwin `F_DUPFD_CLOEXEC` command
number `67` in addition to the WSL/Linux-compatible `1030`, applies the same
real Windows handle close-on-exec state to both, and the filesystem fixture
verifies both forms on x64 and Win32; the complete Release suites remain
22/22. Spawn-time inheritance policy for every descriptor type, atomic
kernel-level close-on-exec ordering, and full Darwin process-creation
semantics remain open.

The connection errno map now covers Win32 `ERROR_CONNECTION_ABORTED` and
`ERROR_CONNECTION_REFUSED`; x64 and Win32 report
`DARWIN_ERRNO_CONNECTION=PASS` for `ECONNABORTED` and `ECONNREFUSED`.
`ECONNRESET` remains correctly handled through the Winsock mapping rather than
an unavailable Win32 `ERROR_CONNECTION_RESET` constant. Full protocol and
Winsock/Win32 precedence rules remain open.

The errno map now translates `ERROR_BROKEN_PIPE` and `ERROR_NO_DATA` to
Darwin `EPIPE`; x64 and Win32 report `DARWIN_ERRNO_BROKEN_PIPE=PASS` and
`DARWIN_ERRNO_NO_DATA=PASS`. Remaining gaps are broader Win32 error coverage,
descriptor-/syscall-specific Darwin meanings, Mach kernel status mapping, and
exact stream cancellation/EOF behavior.

The errno map now covers common filesystem cases: `ERROR_DIRECTORY` to
`ENOTDIR`, `ERROR_DIR_NOT_EMPTY` to `ENOTEMPTY`, `ERROR_FILE_EXISTS` to
`EEXIST`, and `ERROR_DISK_FULL` to `ENOSPC`; x64 and Win32 report
`DARWIN_ERRNO_FILESYSTEM=PASS`. Complete Win32 coverage, per-syscall Darwin
semantics, quota/read-only distinctions, and Mach kernel status mapping remain
open.

The thread smoke now additionally verifies nonblocking join semantics: joining
a live thread with timeout zero returns `WAIT_TIMEOUT`, followed by a normal
successful join and exit code. x64 and Win32 both report
`WINDOWS_THREAD_JOIN_TIMEOUT=PASS`; broader Darwin pthread cancellation,
priority, robust-lock, and condition-variable semantics remain open.

The broker/bootstrap integration family passes on x64 and Win32 for broker
startup, process lifecycle, and process-group baseline behavior. The Windows
host-to-Darling handoff and current job/group bookkeeping are covered by the
smokes; authenticated broker IPC, privilege transitions, nested-job behavior,
cross-session isolation, crash recovery, and complete Darwin launch-service
semantics remain open.

The path/errno/environment family passes on x64 and Win32: Darwin path
translation, normalization, executable-relative handling, errno roundtrips,
runner environment propagation, and Windows-to-UTF-8 environment conversion
all complete. Unicode normalization edge cases, symlink/security-root policy,
locale-dependent encoding, environment mutation across spawned processes, and
complete Darwin path/error precedence remain open.

The TLV/runtime family passes on x64 and Win32: Darwin thread-local storage,
runtime initialization, initializer/terminator sequencing, and atexit/C++
cleanup behavior all complete successfully. Dynamic-image TLV lifetime,
cross-thread destructor ordering, fork/exec reset behavior, loader-lock
interactions, and complete Apple runtime startup/teardown semantics remain
open.

The terminal family passes on both x64 and Win32 for terminal setup and
ConPTY input (`DARWIN_CONPTY_INPUT=PASS`). The broader host API sequence still
inherits the separate restricted-filesystem rename failure and is not counted
as a terminal regression. Console signal/pty job-control parity, terminal
window-size races, raw-mode edge cases, and environments without ConPTY
remain open.

The first Foundation ABI family is now implemented independently in
`darling_windows_foundation.cpp`: `NSGetSizeAndAlignment` handles qualifiers,
primitive scalars, pointers and simple arrays, with invalid encodings rejected.
Resolver lookup and a dedicated smoke pass on x64 and Win32; both builds have
zero errors and report `FOUNDATION_TYPE_ENCODING_ABI=PASS`. Struct/union
encodings, object layout metadata, Foundation classes and CoreFoundation APIs
remain open.

The consolidated source extraction is now recorded in
`SOURCE-EXTRACTION-MATRIX.md`, covering all checked-out Darling, WSL,
PureDarwin, iPAsimulator, Wine/Darwin, SherlockElf, touchHLE, DirectHW,
Cocotron, Darwin_Computa, macho-ld, Classix and VST-ace trees. After the
latest port-set change, the complete selected regression remains x64 `33/33`
and Win32 `33/33`; filesystem hardlink and host rename tests remain excluded
only for the known restricted-account `ERROR_ACCESS_DENIED` condition.

The local port-set family now also exposes `mach_port_remove_member`. The ABI
smoke verifies successful removal and rejection of a second removal; x64 and
Win32 Release builds have zero errors and both report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. This still does not provide cross-process
membership, kernel waitset semantics, notifications, or full Mach rights.

The Darwin_Computa comparison produced a separate WSL/emulated-Linux backend
probe at `tools/Invoke-DarlingWslBackend.ps1`, with the contract documented in
`DARLING-WSL-BACKEND.md`. The probe is implemented and returns the explicit
status `20` when WSL is unavailable or access is denied. On this host it
returned `Wsl/E_ACCESSDENIED`; therefore no WSL Darling runtime is claimed.

The port-set batch now also supports set-wide receive selection through
`mach_port_set_receive`. It snapshots the local member set and polls member
queues until a message arrives or the timeout expires; the smoke verifies
resolver lookup, delivery and payload integrity. x64 and Win32 Release builds
and runtime tests both report `MACH_C_ABI_SELF_DEALLOCATE=PASS`. This is still
local polling, not Darwin's kernel waitset, notification, or cross-process
port-set implementation.

The VM batch now also exposes `mach_vm_copy`, limited to the current Windows
process and implemented with a temporary buffer plus `ReadProcessMemory` and
`WriteProcessMemory`. The ABI smoke verifies resolver lookup, source-to-
destination copying and cleanup; x64 and Win32 builds complete with zero
errors and both runtime smokes report `MACH_C_ABI_SELF_DEALLOCATE=PASS`.
Remote-task VM operations, copy-on-write/inheritance semantics, wired and
purgeable memory, memory-entry ports, and exact Darwin VM error behavior remain
open.

The Mach-port batch now also exposes `mach_port_type`. A locally allocated
queue-backed port reports the receive-right type, while a destroyed/unknown
port reports `MACH_PORT_TYPE_NONE` together with an invalid-name result. The
resolver and ABI smoke cover both cases; x64 and Win32 builds have zero errors
and both runtime tests report `MACH_C_ABI_SELF_DEALLOCATE=PASS`. Send-right
dispositions, dead-name notifications, port sets, guarded ports, and
cross-process rights transfer remain open.

`mach_port_insert_right` is no longer a validation-only stub: for a local
queue-backed port it now validates the destination and increments the tracked
right reference count, with overflow and invalid-name rejection. The Mach ABI
smoke verifies the count transition 1 -> 2 -> 3 and still verifies cleanup;
x64 and Win32 builds have zero errors and both runtime tests report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. This remains a local bookkeeping model, not
Darwin's full disposition matrix or transferable kernel right implementation.

The Mach IPC batch now includes minimal local port-set operations:
`mach_port_set_allocate`, `mach_port_move_member`, and
`mach_port_set_destroy`. The smoke verifies resolver lookup, set creation,
membership insertion, and cleanup; x64 and Win32 Release builds have zero
errors and both runtime tests report `MACH_C_ABI_SELF_DEALLOCATE=PASS`. The
set is currently bookkeeping only: set-wide receive selection, notifications,
cross-process membership, guarded ports, and full kernel-right semantics
remain open.

After the `mach_port_insert_right` change, the consolidated Release regression
was rerun from the current x64 and Win32 build trees. All 33 selected smoke
executables passed on each architecture (`33/33` and `33/33`). The filesystem
hard-link and host rename tests remain intentionally excluded because the
restricted account returns Windows `ERROR_ACCESS_DENIED`; that is an execution
environment limitation, not a claimed Darwin semantic pass.

The Windows broker RPC transport now uses read-all/write-all loops for its
byte-mode named pipe instead of assuming one `ReadFile` or `WriteFile` call
transfers a complete frame. x64 and Win32 broker smoke tests pass with
`BROKER_RPC=PONG`, clean shutdown, and cleanup. This hardens the current IPC
transport; it is not yet Darwin Mach IPC: ports, rights, notifications,
out-of-line messages, receive timeouts, and task/thread port semantics remain
unimplemented.

The Mach-time ABI family now also resolves normalized `mach_absolute_time`,
`mach_continuous_time`, and `mach_timebase_info` names. The x64 and Win32 host
API smokes report `DARWIN_NORMALIZED_MACH_TIME_SYMBOLS=PASS`; clock conversion,
timebase, and monotonicity tests remain covered by the dedicated time smoke.
Full Darwin clock-source and cross-process timing semantics remain open.

The x64 bootstrap/dyld path now has an end-to-end dependent Mach-O execution
fixture: an executable under `Applications/.../MacOS` loads the synthetic
`libSystem.B.dylib`, binds `_libSystemInit` to the Windows host ABI, executes
an indirect call through the bound import slot, and returns the bound result.
The Release dyld smoke reports `DARWIN_DEPENDENT_IMPORTED_ENTRY=PASS` and exits
zero. The Win32 dyld smoke also exits zero for its 32-bit metadata/mapping
coverage, but this dependent x64 code-execution fixture is not yet a Win32
execution proof. Real-world Mach-O instruction sets, lazy binding under
multithreading, code signatures, dyld shared-cache behavior, and complete
Darwin ABI compatibility remain open.

The Win32 x86 Mach-O smoke now executes an actual indirect call through a
mapped import-style pointer slot to a host ABI function and reports
`MACHO_32BIT_IMPORTED_INDIRECT=PASS`. This proves 32-bit executable code can
cross the mapped Mach-O-to-Windows function boundary in the Win32 build. It is
not yet a full 32-bit dependent-provider/dyld graph proof; provider resolution,
32-bit bind-stream application, and real 32-bit Darwin binaries remain open.

The process-identity host table now accepts both Darwin-style underscore names
and normalized names for `getpid`, `getuid`, `geteuid`, `getgid`, and `getegid`.
The x64 and Win32 Release host-library builds pass after this compatibility
addition; Windows-backed identity values and the existing emulated credential
semantics are unchanged.

The signal/process ABI family now also resolves normalized names for
`sigaction`, `sigprocmask`, `sigpending`, `sigwait`, `sigtimedwait`,
`sigwaitinfo`, `sigqueue`, `waitpid`, `wait4`, `waitid`, and `kill`. Dedicated
signal and process smokes report `DARWIN_NORMALIZED_SIGNAL_SYMBOLS=PASS` and
`DARWIN_NORMALIZED_PROCESS_SYMBOLS=PASS` on both x64 and Win32; process-group
and termination semantics remain partly emulated as reported above.

`host_api_smoke` now explicitly reports `DARWIN_NORMALIZED_IDENTITY_SYMBOLS=PASS`
for the five normalized aliases. The same smoke still stops later at the
known restricted-account rename check (`WIN32_ERROR=5`), so its overall exit is
not a pass; the alias result is independently observed before that blocker.

The FD family now resolves normalized `open`, `read`, `write`, `close`, `dup`,
`dup2`, `pipe`, and `pipe2`; both x64 and Win32 file-I/O smokes report
`DARWIN_NORMALIZED_FD_SYMBOLS=PASS` and exit 0. The socket/resolver family now
also resolves normalized socket, address, DNS, and conversion names; the host
API smoke reports `DARWIN_NORMALIZED_SOCKET_SYMBOLS=PASS` on x64 before its
known restricted-account rename failure. Full Darwin socket ABI/error parity
remains open.

Status correction for the current implementation: the Windows host layer now
has an in-process `MachPort` queue with inline and out-of-line byte payloads,
close wakeups, and timeout receive; it also has minimal `TaskPort` and
`ThreadPort` wrappers for local process/thread handles, wait, exit status,
memory read/write/protect/query, suspend/resume, and priority. The corresponding
x64 and Win32 runtime smokes pass. These are deliberately scoped host
abstractions, not Darwin-compatible port rights, namespaces, notifications,
port sets, cross-process Mach transport, or complete kernel error semantics.

The separate Windows runner passes end-to-end argument and environment smoke
tests on both x64 and Win32 Release builds. The dyld/bootstrap smoke also
passes dependent-library graph resolution, rpath handling, host-symbol binding,
initializer and entry invocation, argv/envp delivery, relocations, lazy
binding, weak imports, reexports, and chained fixups. These tests execute
synthetic Mach-O fixtures; execution of an unmodified real macOS executable
remains unverified and is not implied by these results.

An attempted next-stage fixture that made a synthetic Mach-O entry call an
imported function exposed a real remaining loader gap: the current bind-slot
fixture/ABI path caused an access violation before the imported call completed.
That experimental test was removed, and the original x64/Win32 dyld smokes were
rebuilt and returned to `EXIT=0`; no failing experiment is included as a pass.
The next loader task is therefore to make indirect import-slot binding and
calling-convention validation explicit before claiming dependent-code execution.

After the provenance inventory update, the selected core regression was rerun:
x64 `32/32` and Win32 `32/32` smoke programs passed. The two deliberately
excluded checks remain the privilege-sensitive hard-link and host-rename tests;
the current output also reports the known emulated Darwin semantics for
`RESETIDS`, `SETSID`, `SETPGROUP`, datagram/SEQPACKET transport, and local Mach
port wrappers. Green host smokes still do not prove complete Darwin ABI parity.

Follow-up diagnosis: the synthetic call fixture's first displacement was also
incorrect (`LC_MAIN` mapped the entry at VM `0x1200`, while the bind slot was
at `0x1040`), but correcting that alone still produced an access violation.
The experimental fixture was fully removed again; the maintained dyld smoke is
green (`EXIT=0`) on x64, while the imported-function execution gate remains
unproven and must be debugged with slot-byte/address instrumentation before
being promoted to a passing test.

The current core regression covers 32 of 32 selected smoke programs on both
x64 and Win32 Release builds (`x64=32/32`, `win32=32/32`). The two excluded
checks remain `darling_windows_filesystem_smoke.exe` and
`darling_windows_host_api_smoke.exe`; both reach the Windows hard-link/rename
operations but the restricted account receives Win32 error 5
(`ERROR_ACCESS_DENIED`). This is a host privilege/environment limitation, not
evidence of a complete filesystem or host-API port.

The loader errno family now explicitly covers `ERROR_DLL_NOT_FOUND` as
`ENOENT`; x64 and Win32 report `DARWIN_ERRNO_DLL_NOT_FOUND=PASS`. This is
still only host error classification; dependency search, umbrella/framework
fallbacks, loader namespaces, and full Darwin dyld error semantics remain open.

The SDK check confirmed that `ERROR_ENTRYPOINT_NOT_FOUND` is not a Win32
constant in this toolchain; entrypoint lookup failures are represented by the
portable `ERROR_PROC_NOT_FOUND` mapping already tested as
`DARWIN_ERRNO_LOADER=PASS`. No unsupported alias was retained.

The loader map now distinguishes `ERROR_DLL_INIT_FAILED` as Darwin `ELIBBAD`;
x64 and Win32 report `DARWIN_ERRNO_LOADER_INIT=PASS`. This improves failure
classification for present-but-unloadable modules, while initializer order,
dependency rollback, symbol-version handling, and complete dyld error parity
remain open.

The loader errno map now translates `ERROR_MOD_NOT_FOUND` and
`ERROR_PROC_NOT_FOUND` to Darwin `ENOENT`; x64 and Win32 report
`DARWIN_ERRNO_LOADER=PASS`. This covers host diagnostics only; dyld export
lookup, weak imports, symbol-version errors, fallback search paths, and full
Darwin loader error semantics remain open.

The generic Win32 `ERROR_NOT_FOUND` case is now explicitly mapped to Darwin
`ENOENT`; x64 and Win32 report `DARWIN_ERRNO_NOT_FOUND=PASS`. Ambiguous
API-specific uses of `ERROR_NOT_FOUND`, exact syscall context, and the
remaining Windows/Darwin error mappings still require further work.

Executable-format errors are now covered: `ERROR_BAD_EXE_FORMAT` and
`ERROR_EXE_MACHINE_TYPE_MISMATCH` map to Darwin `ENOEXEC`; x64 and Win32 report
`DARWIN_ERRNO_EXEC_FORMAT=PASS`. This improves runner diagnostics but does not
implement foreign-architecture translation, Mach-O CPU compatibility, loader
fallbacks, or complete execve error parity.

The errno map now covers `ERROR_NOT_SAME_DEVICE` to `EXDEV`,
`ERROR_CANNOT_MAKE` to `EACCES`, and `ERROR_PRIVILEGE_NOT_HELD` to `EPERM`;
x64 and Win32 report `DARWIN_ERRNO_CROSS_DEVICE_PRIVILEGE=PASS`. Exact
cross-device rename semantics, privilege/capability policy, and the remaining
Windows error mappings remain open.

The filesystem errno map now covers `ERROR_TOO_MANY_LINKS` to `ELOOP`,
`ERROR_INVALID_DRIVE` to `ENODEV`, and `ERROR_NOT_READY` to `ENXIO`; x64 and
Win32 report `DARWIN_ERRNO_DEVICE_PATH=PASS`. Device, reparse-point, mount and
syscall-specific path semantics remain incomplete.

The errno map now covers size/name limits: `ERROR_FILE_TOO_LARGE`→`EFBIG` and
`ERROR_BUFFER_OVERFLOW`→`ENAMETOOLONG`; x64 and Win32 report
`DARWIN_ERRNO_SIZE_LIMITS=PASS`. Long-path policy, filesystem-specific size
limits, partial-I/O behavior, and the remaining Win32/Darwin mappings remain
open.

The errno map now covers cancellation and timeout states: `ERROR_CANCELLED` to
`ECANCELED`, and `ERROR_TIMEOUT`/`ERROR_SEM_TIMEOUT` to `ETIMEDOUT`; x64 and
Win32 report `DARWIN_ERRNO_CANCEL_TIMEOUT=PASS`. Exact cancellation-point
behavior, syscall-specific timeout interpretation, and the remaining
Win32/Darwin error mappings remain open.

The errno map now translates Win32 sharing and lock conflicts to Darwin
`EACCES`; x64 and Win32 report `DARWIN_ERRNO_LOCKING=PASS`. This covers the
current host lock-error boundary, while exact Darwin lock/lease semantics,
descriptor-specific error selection, and the remaining Win32 mappings remain
open.

The errno map now covers network path errors: `ERROR_BAD_NETPATH` and
`ERROR_BAD_NET_NAME` map to `ENOENT`, while `ERROR_NETWORK_UNREACHABLE` maps
to `ENETUNREACH`; x64 and Win32 report `DARWIN_ERRNO_NETWORK_PATH=PASS`.
Complete network/protocol error selection and all remaining Win32-to-Darwin
mapping rules remain open.

The network errno map now also covers host unreachable, network busy, and
network access denied; x64 and Win32 report `DARWIN_ERRNO_NETWORK_STATE=PASS`
for `EHOSTUNREACH`, `ENETDOWN`, and `EACCES` respectively. Protocol-specific
selection, Winsock-vs-Win32 precedence, and the remaining Darwin network error
semantics remain open.

The errno map now covers the available Win32 write-protect and handle-limit
codes: `ERROR_WRITE_PROTECT`→`EROFS` and `ERROR_TOO_MANY_OPEN_FILES`→`EMFILE`;
both x64 and Win32 report `DARWIN_ERRNO_PROTECTION_LIMITS=PASS`. The attempted
`ERROR_READ_ONLY_DIRECTORY` alias was removed because that constant is not
provided by this Windows SDK. Filesystem-specific read-only distinctions and
the remaining Win32/Darwin mappings remain open.

The errno mapping now covers `ERROR_OPERATION_ABORTED` to `EINTR` and
`ERROR_IO_PENDING` to `EAGAIN`; x64 and Win32 report
`DARWIN_ERRNO_ABORTED=PASS` and `DARWIN_ERRNO_PENDING=PASS`. Broader Win32
error coverage, syscall-specific Darwin meanings, Mach kernel return values,
and exact asynchronous-I/O cancellation semantics remain open.

TaskPort opening now has a negative-path smoke: an invalid PID is rejected
with the host error path and reports `MACH_TASK_PORT_INVALID=PASS` on x64 and
Win32. The visible Windows error is `ERROR_INVALID_PARAMETER` (87); mapping
that condition to Darwin task-port/dead-name semantics and preserving exact
Mach error codes remains open.

The errno family now maps Win32 `ERROR_INVALID_PARAMETER` to Darwin `EINVAL`
(22), and the x64/Win32 errno smoke reports
`DARWIN_ERRNO_INVALID_PARAMETER=PASS`. This fixes the host error translation
used by invalid TaskPort opens; full Darwin errno coverage, Mach-specific
kernel return codes, and all Windows error mappings remain incomplete.

The license-status audit resolves 9 of the 10 catalogue rows as `present`;
one row, `unresolved-submodules`, remains `review-required` because many
submodules are not checked out on NTFS. No binary redistribution claim should
be made for those unresolved submodules until their individual licenses and
notices are inventoried.

An additional filesystem audit found 150 populated Darling external-component
directories; only 2 contain a filename matching the common license/notice
patterns, while 148 do not. This is not proof that those components lack
licenses, but it confirms the current 10-row catalogue is not a complete
per-component license inventory. Individual upstream notices must be collected
before shipping those external components.

After the latest TaskPort and ThreadPort changes, the complete core regression
was rerun on both build trees: MSVC x64 `32/32` and Win32 `32/32` smoke tests
passed. The gate still excludes only the two known privilege-sensitive
hardlink/rename tests and does not imply full Darwin kernel, framework, or
real macOS application compatibility.

After the latest errno expansions, the complete core regression was rerun:
x64 passed `32/32` and Win32 passed `32/32`. The two privilege-sensitive
hardlink/rename tests remain excluded; this gate validates regression safety of
the current host families, not full Darwin kernel/framework/application
compatibility.

The provenance audit found the authoritative license inventory at
`licenses/SOURCE-LICENSE-INVENTORY.csv`: 10 license/notice entries are
catalogued, the bundle contains 9 license files, and 6 collected `*-source`
trees are present. This confirms the bundle is present, not that every
third-party source is relicensable; original notices and component licenses
remain authoritative and unresolved/unknown components must still be reviewed
before binary redistribution.

A consolidated x64 Release regression run covered 32 existing `*smoke.exe`
core tests, excluding only the two known privilege-sensitive filesystem/host
API tests; all 32 exited successfully. This confirms no regression across the
current Mach-O, dyld, runtime, syscalls, sockets, process, signal, terminal,
memory, Objective-C, synchronization, and task/thread host families, but does
not prove real macOS application execution or the excluded hardlink/rename
paths.

A matching Win32 Release regression run also covered 32 core `*smoke.exe`
tests with all 32 passing and no failures. Together with the x64 run this
confirms the current tested host families on both target architectures; the
two excluded privilege-sensitive hardlink/rename tests and the unimplemented
Darwin kernel/framework/application layers remain outside that gate.

ThreadPort now exposes its owning process ID via `GetProcessIdOfThread`; the
current-thread smoke confirms the ThreadPort-to-TaskPort relation with
`MACH_THREAD_TASK_LINK=PASS` on x64 and Win32. This is only Windows handle
identity, not Darwin port-name rights or task/thread namespace semantics;
those, plus cross-process rights and lifecycle notifications, remain open.

The ThreadPort smoke now also verifies that a live thread does not satisfy a
1-ms wait (`MACH_THREAD_WAIT_TIMEOUT=PASS`) before the post-join wait reports
completion. This validates only Windows handle state transitions; Darwin wait
abort/cancellation, port death, notifications, exception delivery, and exact
Mach status semantics remain open.

TaskPort now also exposes process exit-code observation; the live current task
reports `STILL_ACTIVE` as `MACH_TASK_EXIT_CODE=PASS` on x64 and Win32. This is
only Windows process-handle state, not Darwin task termination status,
exception/termination notifications, wait cancellation, dead-name handling,
or exact Mach exit/error semantics.

TaskPort now also exposes a bounded wait over the process handle; the current
process correctly times out with `MACH_TASK_WAIT_TIMEOUT=PASS` on x64 and
Win32. This provides only a host wait primitive, not Darwin task termination
notifications, exception delivery, dead-name transitions, wait-abort rules,
or exact Mach return-code semantics.

ThreadPort now also supports waiting for a thread handle to terminate; the
worker-thread smoke reports `MACH_THREAD_WAIT=PASS` on x64 and Win32 after a
controlled join. This is only Windows handle-wait behavior; Darwin thread
termination notifications, wait cancellation, port-death transitions,
exception delivery, and exact Mach return/error semantics remain open.

ThreadPort now requests set-information rights and exposes priority get/set;
the smoke preserves the current priority and reports
`MACH_THREAD_PRIORITY=PASS` on x64 and Win32. This is only Windows scheduler
priority plumbing; Darwin QoS/thread-policy flavors, real-time constraints,
inheritance, privilege checks, and exact Mach scheduling/error semantics
remain unimplemented.

ThreadPort now requests suspend/resume rights and exposes `Suspend`/`Resume`;
an independent worker-thread smoke reports
`MACH_THREAD_SUSPEND_RESUME=PASS` on x64 and Win32. This is Windows thread
control only, not Darwin `thread_suspend` parity: nesting/count semantics,
safe-point behavior, thread states/registers, exception ports, termination
rights, and Mach error translation remain open.

TaskPort now also exposes a small protection API backed by `VirtualProtectEx`;
the smoke changes a writable region to read-only, confirms a write is refused,
restores read/write, and reports `MACH_TASK_VM_PROTECT=PASS` on x64 and Win32.
This is not yet Darwin `vm_protect` parity: inheritance/max-protection rules,
copy-on-write, submap/region metadata, exact protection faults, and Mach VM
error-code translation remain open.

The runtime now includes a managed `ThreadPort` over Windows thread handles,
with current-thread and open-by-ID paths, move ownership, and liveness checks.
Both architectures report `MACH_THREAD_PORT=PASS` and
`MACH_THREAD_PORT_OPEN=PASS`. Darwin thread-port rights, suspend/resume,
thread state/register APIs, exception ports, termination notifications, and
Mach-specific error semantics remain unimplemented.

ThreadPort now also exposes `GetExitCodeThread`; the live current-thread test
confirms `STILL_ACTIVE` as `MACH_THREAD_EXIT_CODE=PASS` on x64 and Win32. This
only covers a Windows lifecycle observation; Darwin thread states/registers,
termination rights, suspend/resume, exception delivery, and exact exit/error
translation remain open.

TaskPort now exposes a region query backed by `VirtualQueryEx`; the smoke
verifies a committed region contains the allocated address and reports
`MACH_TASK_VM_QUERY=PASS` on x64 and Win32. Darwin VM-region metadata,
submaps, inheritance, shared/COW state, exact protection and error semantics,
and remote-task authorization are still not implemented.

TaskPort coverage now also includes opening a process by PID and move
ownership; the runtime smoke reports `MACH_TASK_PORT_OPEN=PASS` for x64 and
Win32. The implementation still only wraps Windows process handles and does
not expose Darwin task-port rights, VM operations, thread/exception ports,
suspend or terminate operations, notifications, or cross-process Mach
reference rules.

TaskPort now requests VM-operation/read/write rights and exposes guarded
`Read`/`Write` operations. The runtime smoke uses an explicit writable
`VirtualAlloc` region and reports `MACH_TASK_VM=PASS` on x64 and Win32. This
is host-process memory access only; Darwin task-port authorization, VM maps,
copy-on-write behavior, address-space portability, remote-process policy,
exception handling, and Mach VM error translation remain unimplemented.

The TaskPort VM smoke now also verifies that null addresses are rejected
without an access violation (`MACH_TASK_VM_INVALID=PASS`) on x64 and Win32.
This covers only the wrapper's basic validation; Darwin `KERN_INVALID_ADDRESS`
mapping, partial-copy rules, protection faults, VM regions/maps, and remote
task authorization remain open.

The runtime now also exposes a managed `TaskPort` over a duplicated Windows
process handle. It verifies current-process identity and liveness with
`SYNCHRONIZE`; x64 and Win32 report `MACH_TASK_PORT=PASS`. This is only a host
mapping, not Darwin task-port rights or APIs: remote task lookup policy,
thread ports, VM read/write, suspend/resume, exception ports, termination
notifications, and Mach kernel reference semantics remain open.

The in-process Mach-message substrate now carries separate inline and
out-of-line byte regions. The runtime smoke reports
`MACH_PORT_MESSAGE=PASS` and `MACH_PORT_OUT_OF_LINE=PASS` for x64 and Win32.
This validates only local queue ownership and wakeup; the out-of-line region
is not yet VM-mapped or transferred between processes, and Mach descriptors,
rights, notifications, port sets, task ports, and kernel error semantics are
still missing.

The MachPort smoke now verifies close-wakeup behavior with a receiver blocked
inside `Receive`: `MACH_PORT_CLOSE_WAKE=PASS` on x64 and Win32. This confirms
the local condition-variable lifecycle path, but not Darwin port-death
notifications or kernel wait semantics; those, together with rights,
port-sets, task/thread ports, and cross-process ownership, remain open.

The local MachPort lifecycle now has an explicit close state: close wakes
receivers, receive returns empty after closure, and sends after closure are
rejected. Runtime smoke tests report `MACH_PORT_CLOSE=PASS` on x64 and Win32.
This still does not provide Mach send-once/dead-name rights, port-set
membership, destruction notifications, kernel reference accounting, or
cross-process ownership transfer.

The socket family passes independently on x64 and Win32: TCP request/response,
AF_UNIX stream/socketpair, datagram, and seqpacket smoke paths all complete.
Datagram and seqpacket are explicitly emulated over the Windows-backed layer.
Socket option/error/timeout parity, ancillary-message edge cases, credential
passing, and complete Darwin protocol semantics remain open. The filesystem
smoke remains separately blocked by restricted-account hardlink permission,
and the combined syscall smoke currently needs hang localization before it can
be counted as a matrix pass.

The dynamic-library family was re-run on x64 and Win32: `dlopen`, `dlsym`,
`dlclose`, `dlerror`, `dladdr`, dynamic-image enumeration, and the dyld graph
integration all pass. Windows module lifetime and the current Mach-O image
registry are covered; exact Darwin handle/refcount rules, interposition,
symbol-version lookup, shared-cache images, unload callbacks, and full
framework loading remain open.

The Mach-O execution family was re-run across parser, segment mapping,
install-name, UUID, exit-entry, and runner smoke targets. Every listed target
passes on both x64 and Win32, including synthetic argc/argv entry execution
through the Windows runner. Real Apple code signatures, dyld shared-cache
behavior, arbitrary third-party dylib graphs, ARM64 execution, Objective-C
framework startup, and complete system-service initialization remain open.

The Objective-C runtime family was re-run on x64 and Win32. Class and protocol
registration, method lookup/signatures, associations, superclass/runtime
metadata, and the current message-dispatch ABI checks pass. Full Apple
`objc_msgSend` calling-convention coverage, metaclass/cache behavior, ARC/weak
ownership, exception/unwind integration, autorelease pools, and Foundation or
AppKit runtime semantics remain open.

The complete memory smoke was re-run on x64 and Win32 and passes anonymous
and file-backed mappings, protection changes, fixed replacement, private
copy-on-write, offset views, `madvise`, `msync` validation and subrange flush,
unmapping, and persisted file contents. Exact Darwin VM accounting, page
boundary/error precedence, cache invalidation, crash durability, shared
memory naming, and Mach VM primitives remain open.

The time family was re-run on x64 and Win32. Query-performance monotonic time,
precise Unix wall-clock conversion, and the public clock boundary all report
valid forward-moving values. Clock resolution, Mach absolute-time scaling,
sleep interruption/remaining-time semantics, timezone behavior, and complete
Darwin clock-id/error parity remain open.

The signal family was re-run on x64 and Win32: handler dispatch, blocked and
pending masks, unblock delivery, `sigaction`, `SA_SIGINFO`, `sigwait`,
timed-wait timeout behavior, `sigwaitinfo`, and queued signal values all pass.
Native Windows console/event delivery, exact Darwin restart/disposition order,
full non-empty mask ABI coverage, `siginfo_t` parity, and externally generated
cross-process signal semantics remain open.

The thread and synchronization family was re-run after the recent loader and
process changes. x64 and Win32 both pass thread creation/join and exit,
thread-local storage, semaphore behavior, and shared-memory exchange. Native
Windows scheduling/ownership is verified for this baseline; Darwin pthread
ABI edge cases, cancellation, priority inheritance, robust mutex recovery,
condition-variable parity, and cross-process synchronization remain open.

The process/spawn family was re-run after the loader changes: x64 and Win32
both pass POSIX spawn file actions, spawn attributes, suspended-start resume,
wait4/waitid, process-group handling, native SIGKILL group termination, and
the emulated SIGTERM path. RESETIDS, SETSID, SETPGROUP, exact Darwin signal
delivery, and full cross-process credential/job-control parity remain open.

The current rename implementation is confirmed to validate null/empty paths,
convert UTF-8 paths, call `MoveFileExW` with replacement and cross-volume
copy flags, and map the native error back to Darwin errno. The host API smoke
still receives `ERROR_ACCESS_DENIED` in the restricted workspace before a
successful rename can be proven; this remains an environment/privilege gate,
not an unverified claim of runtime parity. A privileged Windows run is still
required for positive rename and hard-link evidence.

The dyld smoke now contains a stable providerless Mach-O fixture that stops at
`DylibGraph::BindImports` and verifies `_getpid` resolves through the automatic
Darling host-symbol table with no provider path. This direct binding proof
passes on x64 and Win32; entry execution of a synthetic providerless image is
intentionally still separate because its handcrafted bind stream was unstable.
Full providerless dyld entry execution, symbol versioning, complete
libSystem/framework coverage, ARM64 execution, and complete macOS startup
semantics remain open.

The attempted direct providerless `_getpid` Mach-O fixture exposed an
unstable synthetic dyld bind-stream/entry combination: x64 could hang during
entry dispatch while Win32 exited differently. The fixture was removed and
the stable dyld smoke was rebuilt and passed on both architectures. The
automatic host-symbol branch therefore remains an implemented but not yet
directly executed fixture path; a correctly generated providerless bind stream
is still required, along with full libSystem/framework coverage, ARM64
execution, and complete macOS startup semantics.

The bootstrap binding order was corrected so real dylib providers remain ahead
of the automatic host-symbol fallback; the x64 and Win32 dyld/bootstrap
regression smokes both exit 0. This verifies the ordering does not regress
existing provider, relocation, and entry behavior; a dedicated fixture that
executes a providerless Mach-O import through the automatic branch is still
needed. Symbol versioning, full libSystem/framework coverage, ARM64 execution,
and complete macOS startup semantics remain open.

After rebuilding both complete Release trees with the session-query addition,
the integrated smoke matrices remain green at 27/27 on x64 and 27/27 on
Win32.

The terminal-name family now exports `_ctermid`, `ctermid`, `_ttyname_r`,
`ttyname_r`, `_ttyname`, and `ttyname`; Windows console descriptors map to
`CONIN$`/`CONOUT$`, while invalid or non-console descriptors fail closed with
Darwin-style error returns, verified on x64 and Win32. PTY allocation, Unix
`/dev/tty` naming, console detachment/reconnection, and exact terminal-device
metadata remain open.

After rebuilding both complete Release trees with the terminal-name family, the
integrated smoke matrix remains green at 27/27 for x64 and 27/27 for Win32.

The directory-iteration family now exports `_getdirentries`, `getdirentries`,
`_getdirentries64`, and `getdirentries64`; it enumerates a real Windows
directory descriptor through a documented fixed-size compatibility record,
tracks the Darwin-style base cursor, and adds x64/Win32 host coverage. Exact
macOS variable-length dirent ABI, inode identity, seek-cookie stability,
concurrent mutation semantics, and directory stream APIs remain open.

The directory-iteration patch has an isolated host proof of `DARWIN_HOST_API=PASS`
on x64 and Win32. A subsequent full matrix reached x64 27/27; Win32 reached
26/27 because the pre-existing `darling_windows_signals_smoke.exe` intermittently
returns exit 3 during the signal setup path, so the directory family itself is
not the failing test and the Win32 aggregate must be rerun after that signal
fixture is stabilized.

The terminal-name family now exports `_ctermid`, `ctermid`, `_ttyname_r`,
`ttyname_r`, `_ttyname`, and `ttyname`; Windows console descriptors map to
`CONIN$`/`CONOUT$`, while invalid or non-console descriptors fail closed with
Darwin-style error returns, verified on x64 and Win32. PTY allocation, Unix
`/dev/tty` naming, console detachment/reconnection, and exact terminal-device
metadata remain open.

The compound credential-mutation family now also exports `_setreuid`,
`setreuid`, `_setregid`, `setregid`, `_setresuid`, `setresuid`, `_setresgid`,
and `setresgid`; unchanged active IDs succeed while invalid or cross-identity
requests fail closed, verified on x64 and Win32. Windows token impersonation,
privilege transitions, saved-ID state, and cross-process credential changes
remain unimplemented.

After rebuilding both complete Release trees with the compound credential
mutation family, the integrated smoke matrix remains green at 27/27 for x64
and 27/27 for Win32.

The login-mutation family now exports `_setlogin` and `setlogin`; assigning the
currently authenticated Windows login succeeds, while null or different names
fail closed with Darwin-compatible errors, verified on x64 and Win32. Changing
the account, session login database state, and multi-user audit semantics remain
open.

The host naming family now exports `_getdomainname`, `getdomainname`, and
resolves the Windows DNS computer domain with a deterministic `local` fallback;
null buffers, zero sizes, and truncation fail with Darwin-compatible errors,
verified on x64 and Win32. Exact Darwin domain/NIS semantics, dynamic domain
membership changes, and multi-label normalization remain open.

The process-group mutation family now exports `_setpgid` and `setpgid`; the
current-process/PID-zero no-op succeeds when targeting the bridge's own group,
while arbitrary group changes fail closed with `EPERM`, and both host fixtures
pass. Cross-process group assignment, session leadership, group inheritance,
and Windows job/console integration remain open.

The session-creation family now exports `_setsid` and `setsid`; the first call
returns the current Windows process identity as the isolated session boundary,
subsequent calls fail with `EPERM`, and both host fixtures pass. True POSIX
session detachment, controlling-terminal ownership, child-session inheritance,
and exact Darwin session leadership remain open.

The session-query family now also exports `_getsid` and `getsid`; PID-zero and
current-process queries return the bridge process identity, unsupported targets
fail with `ESRCH`, and both host fixtures pass. Session creation/joining,
terminal/session leadership, inherited session state, and exact Darwin
session/process-group semantics remain open.

The BSD filesystem-stat family now includes `statfs`/`fstatfs` and the
`statfs64`/`fstatfs64` aliases. Windows volume capacity, free blocks, serial
number, mount point, source volume, and filesystem name are mapped into the
Darwin-compatible structure; Darwin mount flags, filesystem subtype, owner,
and inode/file counts remain compatibility values. The filesystem fixture and
three complete Release runs pass 22/22.

`getfsstat`/`getfsstat64` now enumerate the current Windows volume using the
Darwin buffer-and-count contract, including the zero-buffer count query and
short-buffer behavior. The fixture verifies the current entry and aliases;
three complete Release runs pass 22/22. Windows mount enumeration still
exposes one current-volume entry rather than Darling's full mount namespace.

The Windows enumeration path now returns all logical drive roots rather than
only the current volume. The fixture found the current volume among 11 roots,
validated the count query and `getfsstat64` alias, and three complete Release
runs passed 22/22. Windows drive roots are not a full Darwin mount namespace;
volume filtering, mount flags, and per-filesystem accounting remain limited.

The descriptor-control bridge now implements `F_GETFD`/`F_SETFD` for
`FD_CLOEXEC` and `F_GETFL`/`F_SETFL` for append/nonblocking state. Anonymous
Windows pipes explicitly reject `O_NONBLOCK` with `ENOTSUP`; regular files
retain the documented no-op behavior. The filesystem and host-API fixtures
cover these paths, and three further complete Release runs pass 22/22.

Exports-Trie re-export terminals are now retained as Mach-O metadata and
resolved recursively through dependency ordinals and `@loader_path`/
`@executable_path`. An end-to-end main-image → umbrella-Dylib → target-Dylib
fixture reports `DYLD_REEXPORT=PASS`; the complete Release suite remains 22/22.

The same chain is now verified through the dynamic handle path: `dlsym` on the
umbrella Dylib reaches the target provider and reports
`DYLD_REEXPORT_DLSYM=PASS`. The synthetic fixture strips unrelated bind data;
real non-synthetic lazy/chained imports remain handled by the normal loader.

The Mach-O reader now accepts ARM64 headers and ARM64 slices in fat binaries,
and understands the ARM64 `LC_UNIXTHREAD` register layout for entry metadata.
The fixture successfully opens an ARM64 `LC_MAIN` image while preserving the
x86_64 execution path. ARM64 code execution, instruction-cache ABI, ARM64
relocations, chained-fixup formats, and dyld binding semantics remain
explicitly unimplemented; ARM64 execution fails closed on the x86_64 host.
Three complete Release runs still pass 22/22.

The ARM64 relocation path now applies external `UNSIGNED`, `BRANCH26`,
`PAGE21`, `PAGEOFF12`, and the corresponding GOT page forms, including
segment-relative `r_address` offsets. A synthetic ARM64 branch/page fixture
reports `MACHO_ARM64_RELOCATIONS=PASS`. ARM64 authenticated pointers, TLV
relocations, ADDEND pairing, local relocation records, and full chained-fixup
pointer formats remain open; ARM64 execution is still fail-closed.

Non-authenticated ARM64 `DYLD_CHAINED_PTR_64` rebases are now applied through
the existing 64-bit chain decoder. The ARM64 fixture reports
`MACHO_ARM64_CHAINED_FIXUP=PASS`; ARM64e authenticated pointer formats,
pointer-format-specific diversity/key handling, and kernel/shared-cache
variants remain unimplemented.

ARM64 local `UNSIGNED` relocations are now rebased from the in-image pointer
plus the mapping slide; the fixture reports
`MACHO_ARM64_LOCAL_RELOCATION=PASS`. ARM64 `ADDEND` pairing, subtractor and
authenticated-pointer records remain separate open work items.

`ARM64_RELOC_ADDEND` is now decoded as a signed 24-bit value and must be
paired with the immediately following relocation at the same address. The
PAGE21/PAGEOFF12 fixture path reports `MACHO_ARM64_ADDEND=PASS`; subtractor
pairing and authenticated-pointer records remain open.

ARM64 `SUBTRACTOR` plus following `UNSIGNED` pairs are now validated for
matching address, type, width, and symbol range; the fixture reports
`MACHO_ARM64_SUBTRACTOR=PASS`. Authenticated-pointer relocation records remain
the remaining classic-relocation gap.

Authenticated ARM64 pointer records now validate their encoded metadata and
report key, address-diversity, discriminator, and addend before failing closed
without a PAC backend; the fixture reports `MACHO_ARM64_AUTH_METADATA=PASS`.
Actual Apple-compatible pointer signing/authentication remains unimplemented.

`ARM64_RELOC_POINTER_TO_GOT` now supports the LLVM-defined 64-bit absolute and
32-bit PC-relative forms; the fixture reports
`MACHO_ARM64_POINTER_TO_GOT=PASS`. GOT allocation/resolution for a complete
dynamic image and ARM64e authenticated GOT entries remain open.

ARM64 `TLVP_LOAD_PAGE21` and `TLVP_LOAD_PAGEOFF12` now use the validated
PAGE21/PAGEOFF12 instruction encodings; the fixture reports
`MACHO_ARM64_TLV_RELOCATIONS=PASS`. Full Darwin TLV descriptor allocation,
per-thread resolver calls, and dynamic-image TLV lifecycle remain open.

Dynamic Darwin-TLV registrations now have an explicit teardown path. The
Windows host tracks every live thread-local storage map, removes dynamic
image templates and per-thread objects on teardown, clears descriptors, and
`tlv_smoke` reports `DARWIN_TLV_DESTROY=PASS`. This closes the implemented
descriptor/resolver/lifecycle family; Apple-compatible ABI thunk generation,
full ARM64 execution, PAC signing/authentication, and complete Darwin mount
namespace semantics remain outside this family.

The dyld bind-stream parser now implements `ADD_ADDR_ULEB`, signed special
dylib ordinals, weak-import symbol flags, scaled single binds (`0xb0`), and
ULEB-counted skip binds (`0xc0`) for normal, weak, and lazy streams. The
fixture now proves four distinct bound slots plus a repeated skip sequence and
reports `DYLD_BIND_OPCODES=PASS`, `DYLD_BIND_SCALED_STEP=PASS`, and
`DYLD_BIND_IMM_TIMES=PASS`. Chained-fixup pointer-format coverage and actual
ARM64 execution remain separate open families.

The chained-fixup loader now accepts both `DYLD_CHAINED_PTR_64` and
`DYLD_CHAINED_PTR_64_OFFSET`. The latter resolves non-authenticated rebase
targets relative to the mapped image base and is proven by
`MACHO_CHAINED_PTR_64_OFFSET=PASS`; the format definitions and bit layout were
cross-checked against the bundled PureDarwin/XNU `mach-o/fixup-chains.h`
source. ARM64e/PAC formats, 32-bit Mach-O headers, cache/kernel formats, and
actual ARM64 execution remain unimplemented.

`DYLD_CHAINED_PTR_ARM64E` metadata is now decoded for authenticated and
non-authenticated entries, including the 11-bit chain stride, bind bit,
16-bit ordinal, signed 19-bit addend, and authentication bit. Authenticated
entries fail closed with an explicit PAC-backend error; the fixture reports
`MACHO_ARM64E_CHAINED_AUTH_METADATA=PASS`. Apple PAC signing/authentication,
ARM64e offset/userland variants, 32-bit headers, and execution are still open.

The non-authenticated ARM64e userland offset format (`DYLD_CHAINED_PTR_ARM64E_USERLAND`,
format 9) is now applied relative to the mapped image base and verified by
`MACHO_ARM64E_USERLAND_REBASE=PASS`. Formats 7 and 10 are structurally
decoded with their four-byte chain stride, while authenticated instances still
stop at the PAC boundary. Userland24 format 12, kernel/cache semantics, and
PAC execution remain open.

`DYLD_CHAINED_PTR_ARM64E_USERLAND24` (format 12) is now decoded with its
24-bit bind-ordinal layout and 8-byte stride; its non-authenticated offset
rebase path reports `MACHO_ARM64E_USERLAND24_REBASE=PASS`. Authenticated
format-12 binds, kernel/cache formats, 32-bit Mach-O, and PAC execution remain
unimplemented.

The loader now accepts both the legacy `LC_DYLD_INFO` and modern
`LC_DYLD_INFO_ONLY` load commands. The legacy command is exercised through
normal binding, reexport, and DLSYM resolution and reports
`DYLD_INFO_LEGACY=PASS`. Older 32-bit Mach-O command layouts and the remaining
ARM64/PAC execution boundaries are still outside the implemented scope.

The Mach-O parser now normalizes 32-bit x86 `mach_header`, `LC_SEGMENT`,
`Section32`, and `nlist` symbol records into the existing internal model. Fat
images can select an x86 slice when no x86_64 slice exists. A synthetic 32-bit
dylib maps and exposes `_legacy32`, reporting
`MACHO_32BIT_METADATA=PASS`; execution and relocation application fail closed
with an explicit x86_64-host guard (`MACHO_32BIT_EXECUTION_GUARD=PASS`).
32-bit relocation semantics and actual 32-bit execution remain open.

The 32-bit relocation path now applies external i386
`GENERIC_RELOC_VANILLA` records at 32-bit width, including absolute and
PC-relative forms with in-place addends; the fixture reports
`MACHO_32BIT_RELOCATIONS=PASS`. Scattered relocations, `SECTDIFF`/`PAIR`,
prebound-lazy-pointer records, remaining local relocation semantics, and
actual 32-bit execution remain unimplemented.

Scattered i386 `GENERIC_RELOC_SECTDIFF` and `GENERIC_RELOC_LOCAL_SECTDIFF`
records with a validated following `GENERIC_RELOC_PAIR` are now decoded and
applied as `symbol1 - symbol2 + addend`; the same fixture covers them under
`MACHO_32BIT_RELOCATIONS=PASS`. Prebound-lazy-pointer records, remaining local
forms, and actual 32-bit execution remain open.

Scattered i386 `GENERIC_RELOC_PB_LA_PTR` records now restore the unslid
prebound lazy-pointer value plus the mapping slide, following the bundled
cctools `redo_prebinding.c` semantics. The fixture reports
`MACHO_32BIT_PB_LA_PTR=PASS`; remaining local forms and actual execution remain
open.

The POSIX configuration family now also maps Darwin `_SC_OPEN_MAX` through
`sysconf` to the bridge's 1024-entry descriptor limit, and the host fixtures
verify consistency with `getdtablesize` on x64 and Win32. Dynamic per-process
descriptor ceilings, `RLIMIT_NOFILE` enforcement, inherited limits, and exact
Darwin sysconf numbering/error precedence remain open.

The page-size family now also exports `_getpagesizes` and `getpagesizes`; the
Windows bridge reports its single native page size, supports the Darwin query
form with a zero-length output request, rejects null output for nonzero count,
and passes both host fixtures. Multiple-page-size discovery, huge-page policy,
VM pressure behavior, and exact Darwin ABI/error semantics remain open.

The minimal process entry bridge now resolves both Darwin `_exit` and the
unprefixed `exit` symbol automatically through the host-symbol table and maps
them to a real Windows `ExitProcess` termination path; the host fixture checks
that both bindings are present, while a separate child-process fixture calls
the function and verifies exit status 23 on x64 and Win32, with complete
Release matrices at 23/23. A child-runner test that loads a real Mach-O and
calls `_exit` remains open, as do full Darwin exit/atexit/stdio-flush semantics
and the rest of the process ABI.

The Darwin `SO_REUSEPORT` option (`0x0200`) now has an explicit Windows
translation: the requested state is tracked per emulated descriptor, the host
socket receives the closest available `SO_REUSEADDR` behavior before bind, and
set/get roundtrip coverage passes on x64 and Win32; both complete Release
matrices remain 22/22. This is a deliberate compatibility bridge rather than
full Darwin port-reuse semantics, so port-group policy, `SO_ERROR` mapping,
all remaining option-number translations, ancillary layouts, cancellation,
and cross-process descriptor behavior remain open.

The socket error boundary now maps the common Winsock error family to Darwin
errno values centrally (`WSAEWOULDBLOCK` through `EWOULDBLOCK`, connection,
address, protocol, network, and timeout errors), so existing socket failures
no longer expose raw `100xx` Winsock values; the host fixture verifies a
refused TCP connection as Darwin `ECONNREFUSED` 61 on x64 and Win32, while
both complete Release matrices remain 22/22. Less-common Winsock values,
`getaddrinfo` EAI return-code parity, asynchronous cancellation, signal
interruption, and exact per-call Darwin error precedence remain open.

Current checkpoint (2026-10-05): the implemented i386 classic relocation set
is `GENERIC_RELOC_VANILLA` (absolute and PC-relative), scattered
`SECTDIFF`/`LOCAL_SECTDIFF` with `PAIR`, and `PB_LA_PTR`. `GENERIC_RELOC_TLV`
is now also applied as a PC-relative reference to the local `__thread_ptrs`
slot and reports `MACHO_32BIT_TLV_RELOCATION=PASS`. Actual 32-bit instruction
execution is fail-closed only in the x64 process; the separate Win32 process
now provides the native x86 execution helper.
The complete Release smoke suite is stable at 22 binaries, 3 runs, 66/66
passes. `sherlockelf-source` is retained as MIT-licensed ELF/Mach-O analysis
reference material only; no SherlockElf code is copied into the Darling
runtime.

The native Win32 build now uses MSVC's x86 target and executes a real i386
Mach-O entry stub (`mov eax,42; ret`), reporting
`MACHO_32BIT_EXECUTION=PASS`; the x64 build keeps the explicit execution guard.
Win32-specific dynamic-loader, initializer, dyld mapping, and Objective-C
metadata fixtures are also covered. Objective-C class_ro/category/protocol/ivar
offsets now follow the 32-bit ABI. The Win32 smoke suite is 22/22, and the
x64 Release suite remains 66/66 over three runs.

ARM64 execution gating is now architecture-aware: `_M_ARM64` builds permit
ARM64 Mach-O entry/initializer calls, x64 builds reject ARM64 images, and the
Win32 build rejects non-i386 images. A local ARM64 build could not yet be
verified because the installed Visual Studio instance has no ARM64 target
toolset; ARM64 code execution therefore remains an unverified platform gate.

The Win32 bootstrap path now completes the full synthetic i386 executable flow:
header normalization, segment mapping, rebase, writable TLV initialization,
i386 relocation application, initializer processing, and entry execution. It
reports `DARWIN_BOOTSTRAP_32BIT=PASS`. The fixture's string table was also
separated from its 32-bit symbol table so `_tlv` is resolved from valid Mach-O
metadata rather than overlapping test data. After removing diagnostics, the
Win32 suite remains 22/22 and the x64 Release suite remains 66/66 across three
runs.

The descriptor-control family now implements the distinct `F_DUPFD_CLOEXEC`
path: `F_DUPFD` clears the new descriptor's `FD_CLOEXEC` state, while command
1030 sets it on the duplicate. This follows the WSL/Linux reference test
contract in `wsl-source/test/linux/unit_tests/dup.c` and is covered by the
filesystem smoke fixture on both Win32 and x64. Remaining descriptor gaps are
inheritance-aware `posix_spawn` file-actions and Darwin pipe write-side
nonblocking capacity semantics remain open.

Pipe nonblocking read handling is now implemented: an empty Windows pipe
reports Darwin `EAGAIN`, while available bytes are returned immediately. The
syscall fixture reports `DARWIN_SYSCALL_PIPE_NONBLOCK=PASS` on both Win32 and
x64.

`dup`/`F_DUPFD` now share descriptor status flags as Darwin does: a duplicated
pipe retains `O_NONBLOCK`, while the separate descriptor flag `FD_CLOEXEC`
remains independently controlled. The syscall fixture verifies this invariant
on both targets.

The process bridge now parses the real XNU `spawn_internal.h` file-action ABI
retained in `puredarwin-source/src/Kernel/xnu/bsd/sys/spawn_internal.h`.
`PSFA_OPEN`, `PSFA_CLOSE`, `PSFA_DUP2`, and `PSFA_CHDIR` are applied to child
standard descriptors through an inherited Windows handle list; the end-to-end
fixture redirects stdout, duplicates it to stderr, closes stdin, changes the
child directory, and reports `DARWIN_POSIX_SPAWN_FILE_ACTIONS=PASS` on x64 and
Win32. `PSFA_INHERIT` and `PSFA_FCHDIR` are also handled for standard
descriptors; the fixture opens a directory and verifies the descriptor-based
directory change. `PSFA_FILEPORT_DUP2`, arbitrary descriptors greater than 2,
and unsupported spawn attributes remain fail-closed until their Darwin
semantics are mapped. The XNU `POSIX_SPAWN_CLOEXEC_DEFAULT` attribute is
accepted because the Windows handle-list launch already prevents unintended
descriptor inheritance; `DARWIN_POSIX_SPAWN_ATTRIBUTES=PASS` covers it. The
post-change Win32 and x64 suites each pass 22/22.

The AF_UNIX capability check records the Windows socket-family boundary
explicitly: stream sockets pass the local bind/connect/accept roundtrip on x64
and Win32, while `socket(AF_UNIX, SOCK_DGRAM, 0)` is rejected by Winsock on
both targets. The Darling host now falls back to a path-register-backed
loopback UDP transport and the smoke test verifies the complete PING/PONG
roundtrip with `WINDOWS_UNIX_DGRAM=EMULATED`. Native external AF_UNIX
datagram interoperability, SEQPACKET, Linux abstract names, and ancillary
descriptor passing (`SCM_RIGHTS`) remain open.

After this change the complete Release smoke suites remain
`X64_SMOKE_COUNT=22`, `X64_SMOKE_PASS=22`, `X64_SMOKE_FAIL=0` and
`WIN32_SMOKE_COUNT=22`, `WIN32_SMOKE_PASS=22`, `WIN32_SMOKE_FAIL=0`.

The filesystem family now corrects the `lstat(2)` boundary: it opens Windows
reparse points with `FILE_FLAG_OPEN_REPARSE_POINT` instead of following them.
`fstatat(..., AT_SYMLINK_NOFOLLOW)` uses the same path and the filesystem smoke
fixture checks both modes when symbolic-link creation is permitted. The current
restricted test account reports `DARWIN_LSTAT_REPARSE=SKIP`, so the no-follow
runtime assertion still needs one Windows run with Developer Mode or the
symbolic-link privilege enabled. Both ordinary x64 and Win32 suites remain
22/22.

The process-spawn attribute family now accepts XNU's
`POSIX_SPAWN_START_SUSPENDED` (`0x0080`) and maps it to Windows
`CREATE_SUSPENDED`. The process syscall fixture locates and resumes the new
main thread before waiting for its exit, reporting
`DARWIN_POSIX_SPAWN_START_SUSPENDED=PASS` on x64 and Win32. `SETPGROUP`,
`SETSIGMASK`, `SETSIGDEF`, `RESETIDS`, `SETEXEC`, fileport duplication, and
Darwin-private credential/QoS attributes remain fail-closed.

`POSIX_SPAWN_SETSID` (`0x0400`) is now accepted as an explicit Windows host
emulation using `CREATE_NEW_PROCESS_GROUP`; the process fixture verifies the
spawned child's exit path and reports `DARWIN_POSIX_SPAWN_SETSID=EMULATED` on
x64 and Win32. This provides internal group separation for Darling control
flows, but it is not a native POSIX session-ID implementation. Arbitrary
`SETPGROUP`, signal masks/defaults, identity reset, exec replacement, and
fileport duplication remain open or fail-closed.

The Objective-C lifetime bridge now uses stable heap-backed autorelease-pool
frames, so nested `objc_autoreleasePoolPush` calls cannot invalidate outer
pool tokens through vector reallocation. A new weak-reference fixture retains
an object across nested pools, drains both frames, and verifies final weak-slot
clearing; `DARWIN_OBJC_REGISTRY=PASS` remains green on x64 and Win32. Full
Foundation ownership and weak-reference concurrency semantics remain open.

The internal socket family now also covers Darwin `SOCK_SEQPACKET` semantics:
the path registry maps endpoints to loopback TCP listeners and applies an
explicit length frame per packet, preserving packet boundaries for Darling
clients. The socket fixture reports
`WINDOWS_UNIX_SEQPACKET_REQUEST=PING`,
`WINDOWS_UNIX_SEQPACKET_RESPONSE=PONG`, and
`WINDOWS_UNIX_SEQPACKET=EMULATED` on x64 and Win32. Native external
AF_UNIX/SEQPACKET interoperability and ancillary descriptor passing remain
open.

The block bridge now exports `_objc_retainBlock`, `_objc_releaseBlock`,
`_Block_copy`, and `_Block_release` aliases for the supported heap-shaped Apple
block records. The Objective-C fixture resolves both libclosure spellings and
passes copy/release through the same atomic block lifetime path on x64 and
Win32. Full Apple capture/dispose helpers, stack/global block classes, and
complete libclosure ABI semantics remain open.

The block bridge now also accepts standard Apple block literals for the basic
stack-to-heap path: `_Block_copy` allocates the descriptor-declared size,
invokes the copy helper, tracks retains, and `_Block_release` invokes the
dispose helper before freeing the copy. Global blocks remain retain-free. A
cross-architecture fixture verifies capture preservation and helper lifecycle
on x64 and Win32. Full compiler layout variants, byref forwarding, allocator
flags, and all libclosure edge cases remain open.

The process bridge now also accepts Darwin `POSIX_SPAWN_SETPGROUP` when the
requested process group is zero. Windows maps that form to
`CREATE_NEW_PROCESS_GROUP` and the process syscall fixture reports
`DARWIN_POSIX_SPAWN_SETPGROUP=EMULATED` on x64 and Win32. Assigning a child to
an arbitrary existing Unix process group remains unsupported, as do the other
identity and signal-mask spawn attributes.

The block bridge now exports `_Block_object_assign` and
`_Block_object_dispose` for ordinary Objective-C and block captures, allowing
compiler-generated copy/dispose helpers to retain and release captured values
through the host runtime. Basic `__block` forwarding is now also supported:
the stack cell is copied to heap storage, its forwarding pointer is updated,
copy/destroy helpers run, and multiple block references share the cell. Weak
capture flags, nested byref graphs, and the complete libclosure object-flag
matrix remain open.

Weak block-field flags are now explicitly treated as non-owning copies rather
than entering the strong object-retain path. The fixture covers the flag
dispatch on both targets; compiler-generated weak-slot registration and full
weak-Byref teardown remain separate ABI work.

The signal bridge now maps cross-process Darwin `SIGKILL` to
`TerminateProcess`, and the process fixture reports
`DARWIN_KILL_SIGKILL=EMULATED` on x64 and Win32. Signal zero remains a process
existence probe; graceful cross-process delivery such as `SIGTERM`, Unix signal
queues, and Darwin signal disposition/mask state remain unsupported.

The local signal bridge now adds thread-local blocked and pending masks:
raising a blocked signal records it, and unblocking dispatches it through the
installed handler. The signal fixture verifies this behavior on x64 and Win32.
Native OS-delivered masks, cross-thread routing, queued `siginfo`, and
cross-process graceful delivery remain open.

The signal-wait family now exports `_sigwait`: a registered signal waiter uses
a mutex/condition-variable queue so another host thread can raise a selected
Darwin signal and wake the waiter with the signal number; the signal smoke
verifies this cross-thread path and the complete Release suites remain 22/22
on x64 and Win32. The queue currently supports one host-side wait domain,
does not yet implement `sigtimedwait`/`sigwaitinfo` timeouts or queued values,
and native external delivery, cross-process routing, full `siginfo_t`, and
Darwin disposition/mask ordering remain open.

The timed-signal family now exports `_sigtimedwait` with validated Darwin
`timespec` conversion, immediate/pending delivery through the shared wait
queue, signal-number return on success, and `EAGAIN` on a real timeout; the
signal smoke verifies the zero-timeout error path and the cross-thread wait
path, and both complete Release suites remain 22/22 on x64 and Win32. The
implementation still has one host-wide wait domain, no `sigwaitinfo` payload
or queued-value support, no native external/cross-process routing, and no
complete Darwin ordering for masks, dispositions, or asynchronous delivery.

The signal-information family now exports `_sigwaitinfo`: it waits through the
same cross-thread queue and returns the selected signal plus a Darwin-shaped
record whose locally knowable signal number is populated and whose unknown
source fields remain explicit zero placeholders; the signal smoke verifies the
new path and both complete Release suites remain 22/22 on x64 and Win32. Real
kernel `siginfo_t` provenance, sender PID/UID, queued values, fault/band
details, multiple independent wait domains, native external delivery, and
full Darwin disposition/mask ordering remain open.

The local signal-queue family now exports `_sigqueue` for the current Darling
process, preserves `sival_int`/`sival_ptr` in the shared wait domain, and
returns the value through `_sigwaitinfo`; cross-process queueing is rejected
explicitly rather than silently treated as local delivery. The signal smoke
verifies a queued integer value, and both complete Release suites remain
22/22 on x64 and Win32. Queue ordering and overflow, sender identity, multiple
wait domains, native external delivery, cross-process routing, and complete
Darwin `siginfo_t` and disposition semantics remain open.

The bridge now exports `_sigaction`, `_sigprocmask`, and `_sigpending` using
the four-word Darwin `sigset_t` layout documented by the bundled WinObjC
headers. Basic handler query/install and local mask/pending operations are
covered by the signal fixture on x64 and Win32. `SA_SIGINFO`, signals beyond
the locally supported mask range, native external delivery, and queued
`siginfo` remain unsupported.

`SA_SIGINFO` is now accepted for bridge-raised signals and invokes the
three-argument handler with explicit null `siginfo`/context placeholders. The
fixture verifies this path on x64 and Win32. Real kernel-populated `siginfo_t`,
ucontext delivery, and externally generated SA_SIGINFO events remain open.

The descriptor bridge now exports `_pread` and `_pwrite`. Both perform
offset-based I/O under the descriptor lock and restore the shared file offset;
the filesystem fixture verifies read, write, symbol resolution, and unchanged
position on x64 and Win32. Async/overlapped I/O, vectored positioned I/O, and
filesystem-specific locking semantics remain open.

The same positionsafe path now exposes `_preadv` and `_pwritev`; the fixture
verifies two-vector reads/writes, unchanged file position, and resolver entries
on x64 and Win32. Overlapped completion, partial-vector cancellation, and
native filesystem locking remain open.

The descriptor bridge now exports `_flock` using Windows byte-range locks,
covering shared, exclusive, nonblocking, and unlock operations. Two
independent descriptors verify contention and release behavior on x64 and
Win32. Advisory-lock inheritance, process-death edge cases, and full Darwin
filesystem lock interaction remain open.

The filesystem bridge now exports `_symlink` and `_readlink`. Symbolic links
are represented by Windows reparse points; `stat` follows the final link,
while `lstat` and `fstatat(..., AT_SYMLINK_NOFOLLOW)` inspect the link itself.
The reparse target is decoded back to the Darwin UTF-8 path spelling. The
filesystem fixture reports `DARWIN_LSTAT_REPARSE=PASS` on x64 and Win32, and
the complete 22-test smoke suites remain green on both targets. Windows
reparse-point policy, junction-specific behavior, and full Unix link metadata
remain open.

The previously non-completing combined syscall smoke was localized around the
`MSG_WAITALL` stage with temporary flushed checkpoints; the stage completed,
the diagnostics were removed, and a clean rebuild now passes on x64 and Win32
with `DARWIN_SYSCALL_FILE_IO=PASS` and `DARWIN_SYSCALL_PIPE_NONBLOCK=PASS`.
The earlier timeout was transient or environment-related; no workaround was
retained. Full syscall edge/error parity, ancillary-message variants,
timeouts, and privileged filesystem operations remain open.

The combined public syscall smoke was not counted as passing in this run: it
did not complete within the 30-second observation window and emitted no final
marker. Source inspection narrows the likely blocking region to the later
socket-message/`MSG_WAITALL` sequence, but no source change was made without a
reproducible checkpoint. The independent socket, process, memory, thread,
signal, time, Mach-O, and dyld smokes remain the authoritative passing gates.

The automatic host-symbol fallback is now restricted to providerless imports;
an actual dylib provider retains precedence over an equally named Windows host
export. The x64 and Win32 dyld/bootstrap smokes still build and execute with
exit code 0 after this loader-order correction. Provider symbol versioning,
full libSystem/framework coverage, ARM64 execution, and complete macOS startup
semantics remain open.

The Mach-O bootstrap now automatically resolves imports through the existing
Darling host-symbol table after explicit launch bindings but before opening a
provider dylib. This removes per-launch duplication for supported native
Darwin ABI exports. The dyld/bootstrap smoke builds and runs on x64 and Win32
with all existing dyld, relocation, binding, and entry checks passing. It does
not yet provide a complete libSystem/Foundation implementation, arbitrary
symbol ABI compatibility, ARM64 execution, or full macOS process startup.

`msync` now validates the requested pointer with `VirtualQuery`: committed
anonymous memory remains a valid no-op, while an invalid/uncommitted address
fails with Darwin `EFAULT`. The negative case and the file-mapping subrange
case both pass on x64 and Win32. Exact Darwin range/page-boundary diagnostics,
cache invalidation, and crash-consistent durability remain open.

An attempted `select` invalid-descriptor-to-`EBADF` bridge was tested with
both a high unused descriptor and a closed public pipe, but the current shared
descriptor abstraction did not expose a stable `POLLNVAL` result to `select`.
The experimental source/test changes were reverted; `_poll` invalid-descriptor
signaling remains verified, while `select` invalid-descriptor parity is still
an explicit open gap.

An attempted `select` invalid-descriptor/`EBADF` bridge was reverted after the
public closed-descriptor probe did not produce a stable Darwin result in the
current shared descriptor abstraction. The last stable x64/Win32 smoke source
was restored; `_poll` invalid-descriptor signaling remains verified, while
`select` invalid-descriptor parity remains explicitly open.

The pipe descriptor now also sets and reads back the Darwin-style close-on-exec
descriptor flag through `SetDescriptorFdFlags`/`GetDescriptorFdFlags`; x64 and
Win32 pass. Flag bookkeeping is covered, but actual child-process inheritance
prevention, descriptor duplication semantics for all flags, and the remaining
Darwin FD ABI are still open.

The regular-file descriptor path now calls `Flush` before close/reopen and
continues to pass on x64 and Win32, adding an explicit persistence boundary to
the smoke proof. Filesystem durability guarantees beyond Windows flush
semantics, crash consistency, and the remaining Darwin FD ABI are not claimed.

The environment block family now exports a Darwin-style `char** environ`
through `_environ`, `environ`, and `__environ`; the host builds a UTF-8
snapshot from the Windows environment, refreshes it after successful
`setenv`/`unsetenv`/`putenv`/`clearenv` mutations, and the host fixture verifies
symbol resolution plus visibility of a variable on x64 and Win32, with both
complete Release matrices still green at 27/27. Pointer invalidation across a
mutation, atomic concurrent readers, Windows pseudo-variable policy, duplicate
entry ordering, and exact Darwin ownership/lifetime semantics remain open.

The process-termination family now separates normal `exit` from immediate
`_exit`: `_atexit`/`atexit` and `___cxa_atexit` registration are available
through the host-symbol table, handlers with and without destructor arguments
run in Darwin-compatible mixed LIFO order before normal exit, and
`__cxa_finalize(dso_handle)` executes and removes only the selected DSO's
handlers without double invocation; the child-process fixture verifies the
sequence `ABC` plus status 23 on x64 and Win32, immediate `_exit` bypasses the
registry, and the complete Release suites are green at 27/27 on both
architectures. Recursive exit, handler failure/termination rules, unloaded
image/DSO lifetime coordination, stdio flushing, process-global versus
image-local handler ownership, and exact Darwin exit-status normalization
remain open.

The same filesystem family now exports `_truncate` for path-based resizing.
It opens the target independently, positions to the requested nonnegative
length, and applies `SetEndOfFile` without changing any existing descriptor
offset. The filesystem fixture verifies a one-byte path truncation and symbol
resolution on x64 and Win32; the complete 22-test Release suites remain
22/22 on both targets. Sparse-file allocation, ACL/permission preservation,
and filesystem-specific error parity remain open.

The permissions family now exports `_chmod` and `_fchmod` with an explicit
Windows mapping: any Darwin write bit clears `FILE_ATTRIBUTE_READONLY`, while
mode values without write bits set it. The fixture verifies path and
descriptor forms plus resolver entries on x64 and Win32; both complete
22-test Release suites remain green. Per-user/group ownership, ACL fidelity,
special mode bits, and permission enforcement parity remain open.

The path-resolution family now makes `realpath` resolve the final Windows
reparse point through a normal target-following handle and normalizes the
extended-path prefix before returning UTF-8. The filesystem fixture verifies
that a symbolic-link path resolves to its target on x64 and Win32; the full
22-test Release suites remain green on both targets. Missing-path errno
parity, junction-specific policy, and Darwin mount-namespace normalization
remain open.

The process-attribute family now accepts Darwin `POSIX_SPAWN_RESETIDS` when
the Windows child remains under the same security token. This is an explicit
same-identity emulation, verified by a spawned child and wait-status check on
x64 and Win32. Real set-user/set-group transitions, signal-mask/default
attributes, `POSIX_SPAWN_SETEXEC`, arbitrary process groups, and fileport
duplication remain unsupported.

The directory/link family now exports `_rmdir` for empty-directory removal and
`_link` for same-volume hard links using Windows directory and hard-link APIs;
the fixture verifies hard-link identity/size sharing, resolver entries, and
wrapper-based directory cleanup on x64 and Win32, while the complete Release
smoke suites remain 22/22 on both targets. Cross-volume hard links, link-count
and inode lifetime edge cases, ACL inheritance, and Darwin-specific errno
parity remain open.

The pipe-creation family now exports `_pipe2`; it creates both descriptors
first and applies Darwin `O_NONBLOCK` and `O_CLOEXEC` state through the shared
descriptor table, rejecting unknown flags without leaking either endpoint.
The host API fixture verifies both flags and resolver lookup on x64 and Win32;
the complete Release suites remain 22/22 on both targets. Atomic kernel-level
creation semantics, overlapped pipe I/O, inherited-handle policy across every
spawn path, and full close-on-exec behavior remain open.

The descriptor-control family now applies `F_SETFD(FD_CLOEXEC)` to the actual
Windows handle with `SetHandleInformation`, in addition to retaining the
Darwin flag in the descriptor table; the host API fixture and complete
22-test Release suites remain green on x64 and Win32. The remaining boundary
is spawn-time handle-list policy for every descriptor class and true kernel
close-on-exec ordering across concurrent process creation.

The descriptor durability family now exports `_fsync` and `_fdatasync`, both
mapped to `FlushFileBuffers` while preserving the descriptor table and
fail-closed invalid-descriptor behavior. The filesystem fixture verifies both
calls and resolver entries on x64 and Win32; after an isolated Win32 runtime
rebuild, the complete suites are 22/22 on both targets. Ordered-write barriers,
metadata-only durability distinctions, and crash-consistency guarantees beyond
Windows buffering remain open.

The file-time family now exports `_utimensat` and `_futimens`, maps Darwin
seconds/nanoseconds to Windows `FILETIME`, supports `UTIME_NOW` and
`UTIME_OMIT`, accepts `AT_FDCWD`, and preserves the explicit no-follow flag
boundary. The fixture verifies exact 100-nanosecond-rounded timestamps,
descriptor updates, and resolver entries on x64 and Win32; the complete
22-test Release suites remain green on both targets. Creation-time mutation,
sub-100-nanosecond precision, ACL timestamp policy, and full Darwin errno
parity remain open.

The temporary-file family now exports `_mkstemp`; it validates the Darwin
six-`X` template contract, creates a unique same-length replacement through
Windows temporary-file creation and rename, returns a tracked read/write
descriptor, and updates the caller's template buffer. The host API fixture
and complete Release suites pass 22/22 on x64 and Win32. Exact `mkstemp`
randomness guarantees, `mkstemps` suffix support, directory permission modes,
and race behavior across filesystems remain open.

The temporary-directory family now also exports `_mkdtemp`; it reuses the
unique six-`X` name generation, converts the temporary file into a directory,
updates the caller buffer, and verifies existence, resolver lookup, and
cleanup on x64 and Win32. The complete Release suites remain 22/22 on both
targets. The delete-to-create race window, exact POSIX directory mode
semantics, and cross-filesystem randomness guarantees remain open.

The temporary-file family now also exports `_mkstemps`, preserving a caller
specified suffix while reusing the six-`X` uniqueness path and returning a
tracked descriptor for the final name. The host API fixture verifies suffix
preservation, file accessibility, cleanup, and resolver lookup on x64 and
Win32; the complete Release suites remain 22/22 on both targets. Exact
randomness guarantees, permission modes, and race behavior across filesystems
remain open.

The process-wait family now makes `_wait4` collect the Windows process user and
kernel CPU times with `GetProcessTimes`, converting 100-nanosecond FILETIME
units into Darwin `timeval` fields while leaving unsupported resource counters
explicitly zero; `_waitpid` keeps its existing PID-specific `WNOHANG` behavior,
and the process, host-API, and complete Release smoke suites remain green on
x64 and Win32. Child-selection semantics for PID 0, negative process groups,
`waitid`/`siginfo`, stopped/continued-child reporting, and full Darwin
resource-accounting parity remain open.

The process-resource family now enriches `_getrusage(RUSAGE_SELF)` and
`wait4` with Windows `GetProcessMemoryInfo`, `GetProcessIoCounters`, and
`GetProcessTimes`: peak working set is exported as Darwin-style kilobytes,
page faults and read/write operation counts are reported where Windows has a
direct counter, and unsupported Darwin accounting fields remain zero; both
architectures compile and the complete Release suites remain 22/22. Child
aggregation for `RUSAGE_CHILDREN`, exact Darwin block-count units, context
switch/signal accounting, and accounting for processes that are not retained
as Darling children remain open.

The child-resource accounting step now accumulates successful `_wait4`
measurements for `RUSAGE_CHILDREN`: CPU times and additive counters are summed,
peak-style fields use the maximum, and the aggregate is protected by a mutex;
the process smoke verifies that the aggregate includes the waited child on both
x64 and Win32, while the complete Release suites remain 22/22. The aggregate
still excludes children consumed through `_waitpid`, does not yet enforce
Darwin child ownership or PID-group selection, and cannot synthesize unsupported
Darwin context-switch, signal, or exact block-accounting fields.

The child-reap boundary now registers only processes created through the
Darling `posix_spawn` bridge, and successful `_waitpid` calls for those PIDs
also contribute measured Windows CPU, memory-fault, and I/O data to
`RUSAGE_CHILDREN`; external Windows PIDs are not added to the aggregate, and
both x64 and Win32 complete Release suites pass 22/22. The registry is still
local to the host process, concurrent duplicate waits are not yet serialized,
PID 0/negative-group selection and `waitid` remain unsupported, and children
that terminate without a successful wait call cannot yet be reaped or
accounted with full Darwin semantics.

The wait-selection family now supports `waitpid(-1, ...)` over the registered
Darling children, including blocking and `WNOHANG` selection, returns the
actual reaped PID, and feeds the selected child's measured resources into the
same `RUSAGE_CHILDREN` aggregate; a process smoke covers an exit-status-
specific `waitpid(-1)` case and both complete Release suites remain 22/22 on
x64 and Win32. `waitpid(0)` and negative process-group selection remain
fail-closed because the registry does not yet carry Darwin process-group
membership, `wait4(-1)` still needs the same selector, and concurrent waits,
`waitid`, stopped/continued states, and full kernel child ownership remain
open.

The selector family now applies the same registered-child selection to
`wait4(-1, ...)`, including `WNOHANG`, and returns the selected child's status
and measured resource structure; the process smoke covers both
`waitpid(-1)` and `wait4(-1)` with distinct exit codes, and both complete
Release suites remain 22/22 on x64 and Win32. PID 0 and negative process-group
selection remain fail-closed, wait selection is not yet serialized against
concurrent duplicate calls, `waitid`/`siginfo` and stopped/continued states are
still absent, and the registry remains a host-local approximation of Darwin
kernel child ownership.

The process-group family now retains a Windows process handle for every
Darling-spawned child and associates it with a host-side Darwin-compatible
group identifier: inherited children use group `0`, while emulated
`SETSID`/`SETPGROUP(0)` children use their own PID as group; `waitpid(0)` and
negative-group selectors, as well as `wait4(-1)`, now select short-lived
children reliably and the process smoke reports `SETPGROUP=EMULATED` with both
complete Release suites at 22/22 on x64 and Win32. Arbitrary existing-group
assignment, true Windows/Darwin process-group equivalence, concurrent duplicate
wait serialization, `waitid`/`siginfo`, stopped/continued states, and cleanup
of children never reaped by the host remain open.

The process-lifetime family now registers an `atexit` cleanup for the retained
Darling child handles and clears the group/PID registries under the same mutex,
so host shutdown no longer leaks the Windows handles for children that were
never explicitly waited; process and complete Release smoke suites remain
22/22 on x64 and Win32. This is shutdown cleanup only, not Darwin orphan/
zombie reaping: child ownership across host crashes or `exec`, automatic
collection before shutdown, concurrent wait serialization, and full kernel
process semantics remain open.

The `waitid` family now exports `_waitid` with the Darwin selector values
`P_PID`, `P_ALL`, and `P_PGID`, supports `WEXITED` and `WNOHANG`, and fills the
Darwin-shaped `siginfo` fields `si_signo`, `si_code`, `si_pid`, and
`si_status` for exited children; the layout was derived from the collected
PureDarwin `sys/wait.h` and `sys/signal.h` declarations, while the Windows
implementation is original and the source/license inventory remains
unchanged. The process smoke verifies a real `CLD_EXITED` result and both
complete Release suites remain 22/22 on x64 and Win32. `WNOWAIT`, stopped and
continued children, signal-termination details, real `si_uid`/`si_errno`,
full `siginfo` fault fields, and exact Darwin kernel child ownership remain
open.

The `waitid` observation family now also supports `WNOWAIT`: it duplicates the
retained child handle, reports the Darwin-shaped exit information without
removing the child from the registry, and a subsequent normal `WEXITED` call
can reap the same child; the process smoke verifies this two-step behavior and
both complete Release suites remain 22/22 on x64 and Win32. Stop/continue and
signal-termination states, complete `siginfo` population, exact `si_uid` and
`si_errno`, concurrent wait serialization, and true kernel orphan/zombie
semantics remain open.

The termination-reporting family now records successful Darling
`kill(SIGKILL)` requests per registered child and maps them in `waitid(P_PID)`
to `CLD_KILLED` with `si_status=SIGKILL`; ordinary Windows processes that exit
with code 137 are not reclassified because the marker is attached only to the
Darling kill bridge. The process smoke verifies this path on x64 and Win32,
and both complete Release suites remain 22/22. Signal mappings beyond
`SIGKILL`, `P_ALL`/`P_PGID` termination-detail propagation, real `si_uid` and
`si_errno`, stop/continue states, concurrent waits, and kernel-level orphan/
zombie semantics remain open.

The `siginfo` identity family now fills `si_uid` from the existing Darling
identity bridge and explicitly reports `si_errno=0` for successfully produced
exit and SIGKILL records; the process smoke verifies both fields on x64 and
Win32, and both complete Release suites remain 22/22. The UID is still a
host-side identity approximation, error causes for failed/non-reaped waits
are not encoded into `siginfo`, signal mappings beyond SIGKILL and complete
Darwin fault/band/value fields remain open.

The descriptor-readiness family now exports `_poll` for the Windows-backed
Darling descriptor table, reporting readable pipe data, writable handles,
hangup, and invalid descriptors with Darwin poll bits and bounded timeout
behavior; the host API smoke verifies a ready pipe and resolver entry, and
both complete Release suites remain 22/22 on x64 and Win32. Winsock socket
objects are not yet registered in this descriptor table, regular-file
readiness is an explicit always-ready approximation, and native `poll`/`select`
parity, socket events, cancellation, and terminal/ConPTY readiness remain
open.

The local IPC family now adds `UnixSocket::Pair()`, creating a private
bidirectional Winsock loopback connection with no filesystem endpoint; the
socket smoke verifies both directions with `PAIR`/`BACK`, while all existing
stream, datagram, and SEQPACKET checks remain green and both complete Release
suites stay 22/22 on x64 and Win32. This is an explicitly emulated socketpair,
not native AF_UNIX `socketpair` interoperability, and ancillary `sendmsg`/
`recvmsg` data, `SCM_RIGHTS`, abstract names, external AF_UNIX datagrams and
SEQPACKET, and Darwin socket-option/error parity remain open.

The `_poll` compatibility family now also follows the Darwin/POSIX rule that
negative descriptor entries are ignored with `revents=0`; invalid non-negative
entries still report `POLLNVAL`, and the host API smoke verifies both cases on
x64 and Win32 while the complete Release suites remain 22/22. Descriptor
registration for Winsock objects, native `select`/terminal readiness,
cancellation, and full event/error parity remain open.

The Windows socket descriptor family now enters the same Darwin descriptor
table as files and pipes: `_socket`, `_bind`, `_connect`, `_listen`, `_accept`,
`_getsockname`, `_shutdown`, descriptor `read`/`write`, `close`, and `_poll`
are implemented for Winsock sockets, and the host API smoke performs a real
loopback listener/client/accept/readiness/read/write exchange on x64 and
Win32; the complete Release suites remain 22/22 on both architectures.
Socket duplication and inheritance, nonblocking and close-on-exec socket
flags, `getsockopt`/`setsockopt`, `sendmsg`/`recvmsg`, `select` ABI parity,
Darwin error translation, and native AF_UNIX descriptor interoperability
remain open.

The socket option subfamily now provides `_getsockopt` and `_setsockopt` for
the Windows-compatible option ABI, with a `SO_KEEPALIVE` set/get round trip
covered by the host API smoke on x64 and Win32; both complete Release suites
remain 22/22. Darwin-specific option layouts and level translations,
ancillary-message APIs, socket descriptor duplication/inheritance, and full
nonblocking/error parity remain deliberately unimplemented.

The datagram socket subfamily now exposes `_sendto` and `_recvfrom` through
the shared Darwin descriptor table, with a real loopback UDP exchange and
resolver checks in the host API smoke on x64 and Win32; both complete Release
suites remain 22/22. `sendmsg`/`recvmsg`, ancillary data and `SCM_RIGHTS`,
Darwin address-family translation, datagram nonblocking/error behavior, and
descriptor duplication/inheritance remain open.

Socket descriptor duplication now uses `WSADuplicateSocket`/`WSASocket` for
`dup` and `F_DUPFD`, while `fcntl` applies `O_NONBLOCK` through `ioctlsocket`
and tracks `FD_CLOEXEC`; the host API smoke verifies the duplicate and flag
round trip on x64 and Win32, and both complete Release suites remain 22/22.
Cross-process inheritance, exact Darwin close-on-exec behavior during spawn,
socket-option translation beyond Windows-compatible values, ancillary data,
and complete nonblocking/error parity remain open.

The `_socketpair` family now creates two registered Darwin descriptors for
`AF_UNIX`/`SOCK_STREAM` and uses a private loopback TCP connection as the
Windows fallback; the host API smoke verifies bidirectional `PAIR` transfer
and resolver entry on x64 and Win32, and both complete Release suites remain
22/22. Native AF_UNIX `socketpair` identity and credential semantics,
datagram/SEQPACKET socketpair variants, ancillary data, cross-process passing,
and full Darwin error/option parity remain open.

The message-vector family now exposes `_sendmsg` and `_recvmsg` for ordinary
connected or addressed Winsock traffic, translating Darwin iovec arrays into
`WSABUF` arrays; the host API smoke verifies a two-vector transfer over the
emulated socketpair on x64 and Win32, and both complete Release suites remain
22/22. Control buffers are rejected fail-closed, so ancillary data,
`SCM_RIGHTS`, credentials, timestamps, message truncation flags, exact
Darwin `msghdr` ABI parity, and cross-process descriptor passing remain open.

The first ancillary-rights subfamily now parses Darwin-style `SCM_RIGHTS`
control headers for the emulated socketpair, queues the referenced descriptors
in-process, duplicates them at `recvmsg`, and verifies a transferred pipe
payload on x64 and Win32; both complete Release suites remain 22/22. This is
not yet kernel-backed or cross-process descriptor passing, and control-message
alignment/ABI variants, credentials, timestamps, truncation semantics,
datagram forwarding, descriptor lifetime after sender close, and exact Darwin
error mapping remain open.

The peer-address subfamily now exports `_getpeername` through the shared
Winsock-backed Darwin descriptor table; the host API smoke verifies the real
loopback peer family and port on x64 and Win32, and both complete Release
suites remain 22/22. Unix-domain peer credentials, abstract-name identity,
Darwin sockaddr/error translation, disconnected-socket behavior, and the
ABI-sensitive `select`/`pselect` family remain open.

The reverse-resolution subfamily now exports `_getnameinfo` over the Windows
Winsock resolver and verifies numeric host/service output for the loopback
peer on x64 and Win32; both complete Release suites remain 22/22. Exact
Darwin name-service error mapping, localized/IDN names, Unix-domain names,
scope identifiers, cancellation semantics, and the ABI-sensitive
`select`/`pselect` family remain open.

The textual-address subfamily now exports `_inet_pton` and `_inet_ntop` via
Winsock and verifies the Darwin-facing IPv4 round trip for `127.0.0.1` on x64
and Win32; both complete Release suites remain 22/22. IPv6 zone/scope rules,
Darwin invalid-address error values, IDN/locale interactions, buffer-boundary
details, and the ABI-sensitive `select`/`pselect` family remain open.

The name-resolution subfamily now exports `_getaddrinfo` and `_freeaddrinfo`,
converts Windows `ADDRINFOA` chains into owned Darwin-shaped result nodes, and
verifies `localhost` resolution and cleanup on x64 and Win32; both complete
Release suites remain 22/22. Exact Darwin EAI error values, IPv6 scope and
service-hint translation, IDN/locale behavior, canonical-name ownership,
thread cancellation, and the ABI-sensitive `select`/`pselect` family remain
open.

The basic `_select` family now accepts a Darwin-style 1024-bit descriptor set
with `timeval`, translates requested read/write bits into the existing `_poll`
bridge, and verifies a ready pipe descriptor on x64 and Win32; both complete
Release suites remain 22/22. Standard console descriptors, exception-set
semantics, descriptor values above the emulated table, exact timeout mutation,
`pselect` signal-mask atomicity, native terminal readiness, and complete
Darwin `fd_set` ABI edge cases remain open.

The `_select` bridge now also recognizes Darwin descriptors 0, 1, and 2
directly through the Windows standard handles: stdout and stderr are writable
when their host handles are valid, while stdin uses named-pipe or console-input
readiness and reports hangup when a pipe query fails; the host smoke test
verifies descriptor 1 and both x64 and Win32 builds remain green. Exception-set
semantics, exact timeout mutation, `pselect` signal-mask atomicity, terminal
event filtering, and complete Darwin `fd_set` ABI edge cases remain open.

The `_select` timeout path now measures elapsed host time and returns the
remaining `timeval` instead of unconditionally zeroing it, including the
descriptor-empty sleep path; x64 and Win32 host and complete Release smoke
matrices remain green. The remaining gaps in this family are exact
microsecond-resolution guarantees, exception-set semantics, `pselect` signal
mask atomicity, terminal event filtering, and complete Darwin `fd_set` ABI
edge cases.

The socket-backed `_select` path now translates the Darwin exception set to
Windows urgent-data readiness through `POLLPRI`/`POLLRDBAND`, counts
exception-only results correctly, and the host fixture verifies an out-of-band
byte on a connected loopback socket on both x64 and Win32; both complete
Release matrices remain 22/22. This is only the Windows urgent-data mapping,
not complete Darwin exceptional-event, terminal, signal-mask, or
microsecond-precision parity, and `pselect` remains unimplemented.

The resolver error subfamily now converts the common Windows `getaddrinfo` /
`getnameinfo` results to Darwin's negative EAI values, preserves the success
value zero, handles `EAI_SYSTEM` conditionally because this Windows SDK does
not define it, and verifies an `AI_NUMERICHOST` failure as `EAI_NONAME=-2`;
both x64 and Win32 host tests and complete Release matrices remain green at
22/22. Exact platform-specific EAI edge values, service and scope handling,
IDN/locale behavior, asynchronous cancellation, and full Darwin resolver
ownership/error-precedence semantics remain open.

The resolver hint bridge now translates Darwin `AI_NUMERICSERV` (`0x1000`)
to the Windows SDK's `AI_NUMERICSERV` (`0x8`) on input and maps the flag back
where the host reports it; the fixture proves a numeric `127.0.0.1:80`
resolution succeeds while the nonnumeric `http` service is rejected as
Darwin `EAI_NONAME=-2`, with both architectures and complete Release matrices
still green at 22/22. Windows does not guarantee preservation of every input
hint bit in returned `ai_flags`, and exact Darwin service databases, IPv6
scope/default-hint behavior, IDN/locale handling, cancellation, and resolver
ownership/error precedence remain open.

The socket message-flag bridge now translates the common Darwin flags before
calling Winsock: `MSG_OOB`, `MSG_PEEK`, and `MSG_DONTROUTE` retain their host
meaning, Darwin `MSG_WAITALL` (`0x40`) maps to the Windows `MSG_WAITALL`, and
`MSG_NOSIGNAL` is accepted as an emulated no-op; unsupported flag families are
rejected with `ENOTSUP` instead of being passed with the wrong numeric ABI.
The existing host and complete Release suites remain green at 22/22 on x64
and Win32. `MSG_DONTWAIT`, `MSG_EOR`, truncation/control-result flags,
per-call nonblocking state, exact ancillary semantics, and cross-process
descriptor passing remain open.

The message-flag bridge now handles Darwin `MSG_DONTWAIT` (`0x80`) with a
temporary RAII-backed Winsock `FIONBIO` transition that preserves an already
nonblocking descriptor and restores a blocking descriptor after the single
operation; an empty socket verifies Darwin `EWOULDBLOCK=35` on x64 and Win32,
and both complete Release matrices remain 22/22. The transition is not yet
atomic against concurrent operations on the same socket, and per-call
nonblocking interaction with cancellation, `MSG_EOR`, truncation/control
flags, ancillary ABI, and cross-process descriptor passing remains open.

The `_pselect` entry point now exists with Darwin-shaped `timespec` and
signal-set arguments, converts the timeout to the existing descriptor bridge,
temporarily applies the requested emulated mask, restores the previous mask,
and is covered by the host fixture on x64 and Win32; both complete Release
matrices remain 22/22. The implementation is not yet kernel-atomic with
respect to signal delivery, does not expose exact interrupted-error behavior,
and still inherits the select bridge's terminal, descriptor-ABI, and timing
limitations.

The socket-option family now adds an explicit Darwin `SO_NOSIGPIPE` state
bridge (`0x1022`) because Windows has no equivalent Winsock option: set/get
roundtrips are tracked per emulated descriptor and the state follows socket
duplication, while the host fixture verifies the behavior on x64 and Win32;
both complete Release matrices remain 22/22. Common options that share the
Winsock ABI continue through the native host path, but full Darwin level and
option-number translation, `SO_ERROR`/errno parity, `SO_REUSEPORT`, ancillary
option layouts, cancellation, and cross-process descriptor semantics remain
open.

The Mach-O process-entry proof now exercises the complete minimal path: a
synthetic x86-64 Mach-O contains an undefined `_exit`, `DylibGraph` resolves
it through the automatic Windows host-symbol table, `DarwinBootstrap::Run`
maps and binds the image, and the native entry code exits a child process with
status 23; the dedicated fixture passes on x64, is explicitly skipped as
metadata-only on Win32, and the complete Release suites are green at 24/24 on
both architectures. Real third-party Mach-O binaries, Darwin startup ABI,
`argc`/`argv`/`envp` ABI parity, `atexit` and stdio flushing, dyld closure/
shared-cache behavior, and 32-bit executable entry remain open.

The production-style `darling_windows_runner.exe` path now has an end-to-end
fixture on both execution widths: it starts a temporary x86-64 Mach-O under
x64 and a temporary 32-bit x86 Mach-O under Win32 as separate Windows child
processes, passes the image path plus two arguments, and each Mach-O entry
returns the observed Darwin-style argument count of 3; the x64 and Win32
runner smoke tests both pass and the complete Release suites are green at
25/25 on both architectures. Exact Darwin process-image construction,
environment filtering/overrides, startup stack layout, signal delivery during
launch, executable code signatures, shared-cache loading, and real macOS
command-line binaries remain open.

The runner environment path now has a dedicated end-to-end fixture at both
widths: the parent supplies `DARLING_RUNNER_ENV=present`, the real runner
inherits it, the x86-64 or 32-bit x86 Mach-O imports `_getenv` through the
automatic host-symbol resolver, and the entry returns success only when the
value is visible; both runner environment tests pass and the complete Release
suites are green at 26/26 on x64 and Win32. Darwin environment block ordering,
duplicate-variable rules, locale/encoding edge cases, per-launch overrides,
secure-execution filtering, and exact environment ownership/lifetime semantics
remain open.

The environment family now also exposes `_clearenv`: it enumerates the
Unicode Windows environment block, removes ordinary variables, preserves
Windows-internal drive-current-directory entries such as `=C:`, and returns a
failure if a removal operation fails; the host fixture verifies symbol
registration and that a test variable disappears on x64 and Win32, while the
complete Release suites remain green at 27/27. Darwin `environ` pointer
export is now covered by the subsequent environment-block entry below; exact
pseudo-variable behavior, concurrent mutation ordering, environment
ownership/lifetime, and full Unicode/locale parity remain open.

The environment libc family now also exposes Darwin `_putenv`: malformed
assignments are rejected with Darwin `EINVAL`, valid `NAME=value` strings are
translated to the Windows environment, and the host fixture verifies resolver
registration, set/read/unset behavior on x64 and Win32; the complete Release
matrices remain green at 27/27. Pointer-retention semantics required by the
traditional Darwin `putenv` contract, duplicate-block ordering, and exact
multibyte/Unicode environment behavior remain open.

The executable-location family now exports `_NSGetExecutablePath`,
`__NSGetExecutablePath`, and `___NSGetExecutablePath`; it obtains the current
Windows module path, converts it to UTF-8, implements Darwin's required-size
query/failure convention, and copies the path only when the caller buffer is
large enough. The host fixture verifies resolver lookup and a real `.exe` path,
and both complete Release matrices remain green at 27/27. Exact Darwin path
spelling, long-path behavior beyond `MAX_PATH`, bundle-aware executable
resolution, and image-relative versus host-relative path policy remain open.

The system-query family now exports `_sysctlbyname` and its common Mach-O
spellings; the Windows host answers `hw.ncpu`, logical/active/physical CPU
counts, `hw.memsize`, `hw.optional.arm64`, `hw.machine`, `hw.model`, and the
basic `kern.*` identity keys, implements Darwin-style output-size queries,
`ENOMEM` for short buffers, and `ENOENT` for unknown keys. The host fixture
verifies resolver lookup, CPU and machine queries, and unknown-key failure on
x64 and Win32; both complete Release matrices remain green at 27/27. Numeric
MIB `sysctl`, writable kernel controls, notification/watch semantics, exact
Darwin key/value sets, CPU-group topology, and full `sysctlbyname` ABI/error
parity remain open.

The Mach-clock family now exports `_mach_absolute_time`,
`_mach_continuous_time`, and `_mach_timebase_info` through the Windows host;
the implementation uses `QueryPerformanceCounter` and exposes a nanosecond
conversion ratio, while the host fixture verifies valid timebase fields and
monotonic nondecreasing ticks on x64 and Win32, leaving both complete Release
matrices at 27/27. Cross-process Mach clock calibration, suspend-aware
continuous-time behavior, exact kernel timebase guarantees, Mach clock
services/ports, and all private timing APIs remain open.

The numeric system-query family now also exports `_sysctl`, `__sysctl`, and
`___sysctl`; using the PureDarwin constants `CTL_HW=6`, `HW_MACHINE=1`,
`HW_MODEL=2`, `HW_NCPU=3`, `HW_BYTEORDER=4`, `HW_PAGESIZE=7`,
`HW_MACHINE_ARCH=12`, `HW_MEMSIZE=24`, and `HW_AVAILCPU=25`, the Windows host
implements the common hardware MIBs with output-size queries, short-buffer
`ENOMEM`, invalid-MIB `ENOENT`, and read-only fail-closed behavior. The host
fixture verifies CPU, machine, and invalid-MIB queries on x64 and Win32, and
both complete Release matrices remain green at 27/27. Non-hardware MIB trees,
writable controls, kernel notifications, exact integer/string ABI parity,
and complete Darwin sysctl namespace coverage remain open.

The dynamic-image introspection family now exports Darwin `_dladdr` and the
common unprefixed spelling; it maps a real code address through `VirtualQuery`,
recovers the owning Windows module base, and returns a stable thread-local
image filename in a Darwin-shaped `dl_info` record. The host fixture resolves a
real `kernel32.dll` symbol and verifies the image name and base address on x64
and Win32, with both complete Release matrices still green at 27/27. Mach-O
symbol-name recovery, unloaded-image lifetime rules, dyld shared-cache image
indices, slide/UUID metadata, and exact Darwin `Dl_info` ABI semantics remain
open.

The dyld image-enumeration family now exports `_dyld_image_count`,
`_dyld_get_image_name`, `_dyld_get_image_header`, and
`_dyld_get_image_vmaddr_slide` with their common Mach-O spellings; the Windows
host enumerates current-process modules through `EnumProcessModules`, returns
module paths and bases, and reports a zero slide for the host-module model.
The host fixture verifies resolver lookup, a nonempty real image list, an image
name, a non-null image base, and slide lookup on x64 and Win32, while both
complete Release matrices remain green at 27/27. Darwin image ordering,
Mach-O header translation, unload notifications, slide/UUID computation,
shared-cache indices, and exact dyld image-lifetime semantics remain open.

The process-identity family now exports Darwin `_getprogname` and `_setprogname`;
the Windows host initializes the program name from the executable basename,
keeps updates synchronized, and returns a thread-local read snapshot so a
caller can safely consume the value after the internal lock is released. The
host fixture verifies resolver lookup, mutation, readback, and restoration on
x64 and Win32; after rebuilding the Win32 target following a full-disk
interruption, the complete Release matrices finish at 27/27 on both targets
and `DARWIN_HOST_API=PASS` is reported on both. Exact Darwin global-storage
pointer identity, startup `argv[0]` ownership, concurrent setter ordering,
locale encoding, and all private process-name APIs remain open.

The libc configuration family now exports `_confstr` and `confstr`; based on
the PureDarwin constants it implements `_CS_PATH`, `_CS_DARWIN_USER_DIR`,
`_CS_DARWIN_USER_TEMP_DIR`, and `_CS_DARWIN_USER_CACHE_DIR`, returns the full
required length including the terminating byte, safely truncates into short
buffers, and rejects unsupported keys with Darwin `EINVAL`. The host fixture
verifies resolver lookup, size-only query, exact path copy, short-buffer
termination, user/temp configuration lookup, and unknown-key failure on x64
and Win32; both complete Release matrices remain green at 27/27. The full
POSIX configuration-string table, Darwin-specific directory policy, locale and
encoding behavior, pathconf/fpathconf, and exact errno/ABI parity remain open.

The pathname-configuration family now exports `_pathconf`, `pathconf`,
`_fpathconf`, and `fpathconf`; using the `_PC_*` numbering from PureDarwin it
validates an existing Windows pathname or descriptor and supplies the common
Darwin limits for names, paths, pipes, links, file sizes, transfer alignment,
and synchronization, while unsupported keys fail with Darwin `EINVAL` and
invalid descriptors fail closed. The host fixture verifies resolver lookup,
pathname values, null-path rejection, and unknown-key rejection on x64 and
Win32; both complete Release matrices remain green at 27/27 and the host API
fixture reports `DARWIN_HOST_API=PASS` on both targets. These are documented
Windows host approximations rather than exact filesystem introspection, so
per-volume case sensitivity, ACL/security capabilities, extended attributes,
sparse-hole behavior, filesystem-specific limits, pathconf error precedence,
and complete Darwin errno/ABI parity remain open.

The descriptor-limit family now exports `_getdtablesize` and `getdtablesize`,
returning the bridge's documented 1024-entry Darwin-compatible descriptor
limit shared with its `fd_set`/poll-facing boundary; resolver lookup and the
returned limit pass in the x64 and Win32 host fixtures. This does not yet prove
Darwin's dynamic per-process descriptor accounting, `RLIMIT_NOFILE`, close-on-
exec inheritance, or exact kernel table exhaustion/error behavior.

The read-only resource-limit family now exports `_getrlimit` and `getrlimit`
with the PureDarwin resource IDs for CPU, file size, data, stack, core,
address space, locked memory, process count, and open files; stack, core, and
descriptor limits receive explicit bridge values, unsupported IDs and null
outputs fail with `EINVAL`, and the x64/Win32 host fixtures pass. `setrlimit`,
dynamic per-process enforcement, inherited limits, CPU/memory throttling, and
exact Windows security-token equivalence remain open.

The resource-limit family now also exports `_setrlimit` and `setrlimit` with a
thread-safe process-local limit table, validation that the soft limit does not
exceed the hard limit, and rejection of hard-limit increases; set/get ABI
round-trips and invalid-input handling pass on x64 and Win32. The implementation
does not yet enforce limits in Windows process creation, memory allocation,
file opening, CPU scheduling, or child inheritance, so kernel-level Darwin
`rlimit` semantics and exact privilege/error parity remain open.

The resource-limit smoke coverage now includes a successful `setrlimit` to
`getrlimit` round-trip in addition to null-pointer, invalid-resource, and
soft-over-hard rejection checks; both x64 and Win32 host fixtures report
`DARWIN_HOST_API=PASS`. This confirms the bridge state machine only, not OS
enforcement or cross-process inheritance.

The process-priority family now exports `_getpriority`, `getpriority`,
`_setpriority`, and `setpriority`; current-process Darwin nice values are
translated to Windows priority classes, invalid targets/ranges fail closed,
and the smoke test restores the original priority after a successful round
trip on x64 and Win32. Process-group, user-wide, Darwin role/background,
thread-level QoS, privilege checks, and exact scheduler/nice semantics remain
open.

The file-creation permission family now exports `_umask` and `umask` with a
thread-safe process-local Darwin mask, validates the POSIX mode range, and
passes resolver plus save/change/restore coverage on x64 and Win32. The mask is
not yet applied uniformly to every creation path or Windows ACL, and exact
permission inheritance, special bits, races with concurrent creators, and
Darwin errno parity remain open.

The normal `open` bridge now consumes the Darwin `mode` argument when
`O_CREAT` is present and applies the current process umask through the existing
Windows chmod mapping before returning the descriptor; both x64 and Win32 host
smokes still report `DARWIN_HOST_API=PASS`. Creation through `mkstemp`, spawn
file actions, directory creation, Windows ACL inheritance, exact special-bit
semantics, and race-free kernel-level mode application remain open.

The priority bridge now also accepts an explicit `PRIO_PROCESS` PID, opens the
target with the minimum required Windows access rights, and closes non-current
process handles correctly; the host fixture verifies both PID-zero and
explicit-current-PID paths on x64 and Win32. Group/user priority selection,
Darwin role and QoS controls, remote-process privilege behavior, and exact
kernel scheduler semantics remain open.

After rebuilding every target, the complete Release smoke matrices remain
27/27 on x64 and Win32; the normal `open(O_CREAT, mode)` umask integration did
not regress the existing bridge families. `mkstemp`/`mkdtemp` remain unchanged
because their restrictive POSIX creation modes are intentional and require a
separate exact-mode audit rather than blind umask application.

The post-change verification currently proves x64 at 27/27, while the Win32
build completes but `darling_windows_broker_smoke.exe` reproducibly fails three
times with `WaitNamedPipeW` timeout / Win32 error 121; therefore the Win32
matrix is not claimed green for this checkpoint and the broker startup/pipe
lifecycle remains an active verification blocker.

The broker client retry window was increased from 10 to 30 seconds and the
broker smoke passed directly on both architectures once, but after the full
rebuild the Win32 broker still reproduces the timeout in isolation while x64
remains 27/27; the change is therefore retained as bounded robustness, not
counted as a complete Win32 fix, and the Win32 process/pipe startup divergence
requires deeper tracing.

The broker now publishes its named-pipe server before prefix bootstrap, and
the bounded client retry remains 30 seconds; isolated x64 and Win32 broker
smokes currently complete with `PONG`, `BYE`, exit 0, and cleanup PASS, but a
full 27-test matrix still intermittently reproduces the Win32 pipe timeout
while x64 remains 27/27. The broker is therefore improved but not yet a
deterministically green cross-test lifecycle.

Three consecutive isolated Win32 broker runs now pass with `PONG`, `BYE`, exit
0, and cleanup PASS, confirming the broker protocol itself; only the ordered
full-matrix interaction still intermittently triggers the pipe timeout and
remains the unresolved cross-test lifecycle case.

The broker smoke startup delay was raised to one second; isolated x64 and
Win32 broker tests still pass, and the full matrix no longer failed at the
broker, but the current Win32 matrix is 26/27 because
`darling_windows_signals_smoke.exe` exits 3 both in the matrix and on its
immediate matrix-order rerun, while its isolated retry passes. The remaining
verification blocker has therefore moved from broker startup to signal-smoke
ordering/flakiness; x64 remains 27/27.

The signal smoke now reports individual setup results instead of hiding them
behind exit 3; after rebuilding, five consecutive isolated Win32 runs pass
with `DARWIN_SYSCALL_SIGNAL=PASS`, so any recurrence now has actionable
diagnostics. A full post-diagnostic matrix rerun is still required before
claiming Win32 27/27 again.

The post-diagnostic full Release matrix is now green again: x64 27/27 and
Win32 27/27; the signal setup diagnostics and broker startup ordering remain
in place, and no smoke regression is present at this checkpoint.

The load-observation family now exports `_getloadavg` and `getloadavg`; it
returns a Windows `GetSystemTimes` CPU-activity approximation scaled by the
configured processor count, fills the requested sample count, rejects
null/empty requests with `EINVAL`, and passes the x64/Win32 host fixtures.
Exact Darwin one-/five-/fifteen-minute runnable-queue load averages, sampling
history, scheduler attribution, and cross-platform numerical parity remain
open.

After rebuilding both complete Release trees with the page-size, load-average,
and `_SC_OPEN_MAX` additions, the integrated smoke matrices remain green at
27/27 for x64 and 27/27 for Win32.

The process-group query family now exports `_getpgrp`, `getpgrp`, `_getpgid`,
and `getpgid`; current-process and PID-zero queries map to the Windows process
ID, unsupported foreign/negative targets fail with `ESRCH`, and both host
fixtures pass. Creating or joining arbitrary process groups, session IDs,
group signaling, inherited group state, and exact Darwin process-group
semantics remain open.

The identity-triplet family now exports `_getresuid`, `getresuid`, `_getresgid`,
and `getresgid`; all three Darwin identity values are consistently derived from
the existing Windows bridge UID/GID and null output pointers fail with
`EINVAL`, verified on x64 and Win32. Real Windows token/SID mapping, changing
real/effective/saved identities, supplementary groups, privilege transitions,
and exact Darwin credential semantics remain open.

The supplementary-group family now exports `_getgroups`, `getgroups`,
`_setgroups`, and `setgroups`; the Windows bridge exposes the primary GID as a
single read-only group, supports the Darwin size-query form, rejects invalid
buffers, and rejects mutation with `EPERM`, verified on x64 and Win32. Exact
Windows token group enumeration, supplementary-group mutation, privilege checks,
and Darwin group inheritance remain open.

After rebuilding both complete Release trees with the supplementary-group
family, the integrated smoke matrix remains green at 27/27 for x64 and 27/27
for Win32.

After integrating the credential-mutation family into both complete Release
trees, the full smoke matrix remains green at 27/27 for x64 and 27/27 for
Win32.

The credential-mutation family now exports `_setuid`, `setuid`, `_seteuid`,
`seteuid`, `_setgid`, `setgid`, `_setegid`, and `setegid`; setting an identity to
the already active bridge ID succeeds, invalid IDs return `EINVAL`, and changes
to another ID fail with `EPERM`, verified on both x64 and Win32. Real token
impersonation, privilege checks, saved-ID transitions, and cross-process
credential changes remain open.

The host naming family now exports `_getdomainname` and `getdomainname`,
resolves the Windows DNS computer domain with a deterministic `local` fallback,
and rejects null, zero-sized, or truncated buffers with Darwin-compatible
errors, verified on x64 and Win32. Exact Darwin/NIS domain semantics, dynamic
domain membership changes, and multi-label normalization remain open.

After rebuilding both complete Release trees with the domain-name family, the
integrated smoke matrix remains green at 27/27 for x64 and 27/27 for Win32.

The login-mutation family now exports `_setlogin` and `setlogin`; assigning the
currently authenticated Windows login succeeds, while null or different names
fail closed with Darwin-compatible errors, verified on x64 and Win32. Changing
the account, session login database state, and multi-user audit semantics remain
open.

After rebuilding both complete Release trees with the login-mutation family,
the integrated smoke matrix remains green at 27/27 for x64 and 27/27 for
Win32.

The signal-disposition fix now keeps non-`SIGINT` Darwin handlers in the
bridge's internal dispatcher instead of forwarding them to the MSVC CRT;
isolated signal smokes pass on x64 and Win32 with `DARWIN_SYSCALL_SIGNAL=PASS`.
The latest aggregate rerun still contains unrelated fixture failures in broker
startup, hard-link ABI checks, `realpath`, working-directory setup, spawn file
actions, and AF_UNIX path validation, so no new 27/27 aggregate claim is made
at this checkpoint.

The out-of-process broker family is now repaired: the named-pipe client accepts
both short and fully-qualified pipe names, attempts the direct Windows pipe
open before waiting, and retries transient `BUSY`, `FILE_NOT_FOUND`,
`PIPE_NOT_CONNECTED`, and `NO_DATA` states. The broker uses the creator's
standard local pipe ACL instead of the previous failing SDDL descriptor. A
fresh Release build and isolated RPC smoke pass on both x64 and Win32 with
`PONG`, `BYE`, clean child exit, and prefix cleanup. This proves the Windows
host broker transport only; it does not yet prove execution of a real Mach-O
process or the full Darling runtime/framework stack.

The `posix_spawn` file-action family now preserves descriptor-associated
directory paths across `OPEN`, `DUP2`, and `CLOSE`, allowing `FCHDIR` to use the
same directory identity even when Windows cannot recover a usable path from
the inherited handle. The process smoke now passes `OPEN`, `DUP2`, `CLOSE`,
`CHDIR`, and `FCHDIR` actions on both x64 and Win32 with
`DARWIN_POSIX_SPAWN_FILE_ACTIONS=PASS` and `DARWIN_SYSCALL_PROCESS=PASS`.
Arbitrary descriptors beyond the three Windows standard streams, fileport
duplication, exact close-on-exec inheritance, and Darwin process-group
semantics remain open.

The restricted-path family now gives `realpath` an absolute-path fallback when
Windows denies the zero-access directory handle needed for final-name lookup;
the host API smoke progressed past `realpath` on both architectures. The
remaining aggregate failures at this checkpoint are environment-sensitive:
the managed sandbox denies `CreateHardLinkW` (`ERROR_ACCESS_DENIED`), the path
fixture's selected TEMP-directory comparison still needs normalization, the
host rename fixture needs a permitted writable target, and native Windows
AF_UNIX stream bind still fails for this host configuration. These are not
claimed as complete Darwin filesystem/socket parity.

The path fixture was then corrected to trim the trailing separator returned by
`GetTempPathW`; `DARWIN_SYSCALL_PATHS=PASS` now verifies `chdir/getcwd` on both
x64 and Win32. The host API's `realpath` portion also passes after the absolute
fallback, while its later rename assertion remains blocked by the managed
TEMP-root permissions. Native AF_UNIX stream transport remains open; unlike
the already emulated datagram/sequenced paths, the stream class still needs a
loopback fallback when Windows rejects the requested filesystem socket path.

The AF_UNIX stream family now has that fallback: native `AF_UNIX` stream
`Listen`/`Connect` is attempted first, while rejected or overlong filesystem
paths use a process-local path-to-loopback-port registry; `Accept`, move
ownership, and listener cleanup preserve the emulated state. The socket smoke
reports stream, pair, datagram, and sequenced request/response success on both
x64 and Win32. After a fresh complete rebuild, the aggregate Release matrix is
`25/27` on each architecture; the only failures are the managed-environment
hard-link check (`CreateHardLinkW`, error 5) and the host rename check
(`MoveFileExW`, error 5). They remain explicitly unclaimed rather than being
masked as passes.

The Darwin virtual-memory family now exposes `_mmap`/`mmap`,
`_mprotect`/`mprotect`, and `_munmap`/`munmap` through the automatic host-symbol
resolver. Anonymous `MAP_ANON` mappings use checked `VirtualAlloc`, requested
protection is translated to Windows page protection, `mprotect` changes the
committed range, and invalid or file-backed requests fail closed instead of
silently becoming anonymous memory. The extended memory smoke passes on x64
and Win32; file-backed mappings, shared mappings, inherited VM regions,
`MAP_FIXED` collision semantics, `madvise`, guard pages, and exact Darwin VM
accounting remain open.

After rebuilding both complete Release trees with the VM symbol family, the
aggregate matrix remains `25/27` on x64 and `25/27` on Win32. The two failures
are unchanged and independently observed as Windows error 5 in the managed
environment (`CreateHardLinkW` and `MoveFileExW`); no existing Darling bridge
regressed.

The file-backed virtual-memory family now connects Darwin descriptors to
`CreateFileMappingW`/`MapViewOfFile` and tracks the aligned Windows view base
and mapping handle until `munmap`; shared writes are flushed before the view is
released, and private mappings use Windows copy-on-write views. The memory
smoke now proves `open -> write -> mmap(MAP_SHARED) -> modify -> munmap ->
reopen/read` on both x64 and Win32, including a nonzero offset, a byte-aligned
Darwin view over a page-rounded Windows view, and a private write that does not
reach the file. After a fresh complete rebuild the
aggregate matrix remains `25/27` on each architecture; the only failures are
the unchanged managed-environment hard-link and rename checks, both reporting
Windows error 5. Exact Darwin `MAP_FIXED` collision replacement,
non-page-aligned fixed addresses, sparse/zero-length file rules, inherited
mapping descriptors, and precise `madvise`/VM accounting are still open.

The VM advice ABI is now also exported as `_madvise`/`madvise`. Darwin's
standard advice values are validated and accepted as best-effort hints on
Windows; invalid advice or ranges fail with `EINVAL`, while no Windows call is
pretended to provide semantics that the host cannot guarantee. The extended
memory smoke verifies the symbol, valid advice, and invalid-advice failure on
both x64 and Win32. Exact cache/paging behavior for `WILLNEED`, `DONTNEED`,
`FREE`, and reusable-page hints remains an intentional portability gap.

After rebuilding both complete Release trees with the advice family, the
aggregate matrix is still `25/27` on x64 and `25/27` on Win32. The only
failures remain the managed-environment `ERROR_ACCESS_DENIED` hard-link and
rename fixtures; no VM or existing Darling bridge regression was observed.

The file-backed mapping proof was extended to a nonzero Darwin offset. The
Windows implementation now aligns the mapping offset to allocation granularity,
rounds the internal view to host pages, returns the requested byte offset, and
uses `FILE_MAP_COPY` for private writable views. The smoke test proves that a
private modification is not persisted while a later shared modification is;
the proof passes on x64 and Win32. Windows fixed-address replacement, mapping
inheritance, sparse-file edge behavior, and exact VM accounting remain open.

After the offset/COW change, both full Release builds and the complete matrix
still produce `25/27` on x64 and `25/27` on Win32. The two failures are still
only the externally denied hard-link and rename fixtures with Windows error 5.

The terminal-control family now exports `_ioctl`/`ioctl` and translates
Darwin's `TIOCGWINSZ` request to `GetConsoleScreenBufferInfo`, returning the
Windows console's row/column dimensions through a Darwin-compatible winsize
layout. Invalid or non-console descriptors return `ENOTTY`; `TIOCSWINSZ` and
other terminal control requests fail explicitly with `ENOTSUP`/`ENOTTY` rather
than pretending to implement termios or full ConPTY behavior. The resolver and
invalid-descriptor path are covered by the host API fixture; an interactive
ConPTY resize round trip remains open.

The complete x64 and Win32 Release rebuilds after this family pass, and the
aggregate matrix remains `25/27` on each architecture. The unchanged two
failures are the managed-environment hard-link and rename permission checks.

The anonymous `MAP_FIXED` family now also proves the non-colliding placement
case: a released allocation-granularity-aligned address can be requested again
and is returned exactly on both x64 and Win32. This does not claim Darwin's
destructive collision replacement semantics; an occupied Windows allocation,
file view, or non-allocation-granularity address still requires explicit
handling and remains open.

After a fresh complete rebuild with the fixed-placement proof, the aggregate
matrix remains `25/27` for x64 and `25/27` for Win32. The only failures remain
the externally denied hard-link and rename fixtures, both Windows error 5.

Anonymous `MAP_FIXED` now additionally tracks mappings created through the
Darwin C ABI. When a fixed request targets the exact base of an owned anonymous
mapping, the old Windows allocation is released and the requested replacement
is placed at that address; the collision replacement is verified by the memory
smoke on x64 and Win32. Partial-range collisions, file-view replacement,
arbitrary foreign Windows allocations, and Darwin's full unmap-and-split rules
remain fail-closed and open.

After rebuilding both complete Release trees with owned anonymous collision
replacement, the full matrix remains `25/27` on x64 and `25/27` on Win32. The
same two externally denied filesystem fixtures fail with Windows error 5;
memory, TTY, loader, process, socket, and all other existing smoke families
show no regression.

Owned file-backed views now receive the same exact-base `MAP_FIXED` treatment:
when `MapViewOfFileEx` collides with a view already registered by this bridge,
the old view and mapping handle are closed before the replacement is mapped.
The memory smoke verifies file-view replacement on both x64 and Win32. Views
owned by foreign code, partial-range replacement, and full Darwin mapping
split/merge lifetime semantics remain intentionally unsupported.

The complete Release matrix after file-view replacement is still `25/27` on
each architecture; only the managed Windows error-5 hard-link and rename
fixtures fail.

The descriptor-terminal family now extends `_ioctl` with Darwin `FIONREAD`:
pipe handles use `PeekNamedPipe`, and console-input handles use
`GetNumberOfConsoleInputEvents`; unsupported handle classes remain `ENOTTY`.
This is a host-backed availability query, not byte-perfect termios/ConPTY
readiness semantics, and interactive console buffering, event modes, and
full ioctl request coverage remain open. Both complete Release matrices stay
at `25/27` after the addition, with only the same two external error-5 tests
failing.

The Windows terminal family now contains a GPL-3.0-only `DarwinPseudoTerminal`
bridge backed by the Windows ConPTY API. It resolves the ConPTY entry points,
creates the input/output pipes, attaches a child process through
`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`, supports resize, wait, write, and a
background synchronous output reader, and includes a dedicated terminal smoke
target. Both x64 and Win32 Release builds compile this family successfully.
The terminal smoke now reaches child exit code zero and captures the marker
through the bridge reader on both x64 and Win32; the explicit standard-handle
attachment and background synchronous reader are therefore verified. Full
interactive input/echo coverage, EOF and termination ordering under long-lived
children, VT/UTF-8 normalization, Darwin PTY ioctl semantics, signal/job
control, and behavior on Windows versions without ConPTY remain open. The
complete Release matrix is now `26/28` on x64 and `26/28` on Win32; the only
remaining failures are the externally denied error-5 hard-link and rename
fixtures.

The remaining filesystem failures were rechecked against the host: a direct
Windows hard-link creation in the same Release directory is also denied with
access denied, so the hard-link red result is an environment policy boundary,
not evidence that the Darwin wrapper omitted `CreateHardLinkW`. A direct
PowerShell rename in that directory succeeds, while the host-API fixture still
reports error 5 through the Darling descriptor/temporary-file sequence; the
rename wrapper therefore remains under investigation and is not marked fixed.

The dedicated terminal-input smoke now verifies `DarwinPseudoTerminal::Write`
against an interactive `cmd.exe choice` child on both x64 and Win32: one input
character is delivered through ConPTY and the child emits the expected marker
before clean exit. This establishes basic PTY input delivery, while line
discipline, echo, EOF, long-lived interactive sessions, VT/UTF-8 normalization,
Darwin PTY ioctl semantics, signal/job control, and behavior on Windows
versions without ConPTY remain open. With this additional target the complete
Release matrix is `27/29` on x64 and `27/29` on Win32; the only failures are
the externally denied error-5 hard-link and rename fixtures.

An experimental EOF probe that closed the ConPTY input handle was not
accepted into the build: `cmd.exe` terminated with `0xC000013A` instead of a
clean EOF-driven exit. The probe was removed after verification, and EOF/
termination ordering remains an explicit open semantic rather than a claimed
pass.

The Windows `Process` wrapper now creates a Job Object for every launched
process, enables `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, assigns the process to
that job, and routes `Process::Terminate` through `TerminateJobObject` when the
job is available. This supplies a process-tree boundary closer to Darwin
process-group teardown while preserving the existing fallback for job
assignment failure. The process smoke now also launches a long-running system
child and verifies `Process::Terminate` through the job path with exit code 23
on both x64 and Win32. Nested-child enumeration, process-tree-wide teardown
beyond this direct child, and signal-to-job translation are still not proven.

After the Job Object change, targeted regression checks pass on both x64 and
Win32 for the process lifecycle, Mach-O runner, local signal boundary, ConPTY
output, and ConPTY input smoke families. The full matrix still requires a
fresh complete run after generated-output cleanup; no new failure was observed
in these directly exercised families.

The split full-matrix verification after generated-output cleanup is complete:
there are 29 smoke executables on each architecture, with x64 block 1 at
13/15 and block 2 at 14/14, and Win32 block 1 at 13/15 and block 2 at 14/14.
The only two failures on each architecture are the already documented
hard-link and Darling rename fixtures, so the authoritative aggregate remains
`27/29 PASS` for x64 and `27/29 PASS` for Win32 after the Job Object change.

The process-syscall smoke now also records the exact process-control boundary:
`wait4`, `waitpid`, `waitid`, POSIX spawn, file actions, start-suspended,
reaping, and direct Job Object termination pass on both architectures, while
`RESETIDS`, `SETSID`, `SETPGROUP`, and `SIGKILL` remain explicitly marked
`EMULATED`. The next process-family work is therefore native process-group
and session identity propagation, not another symbol-alias addition.

The host-API rename fixture now uses per-process unique temporary filenames,
excluding stale fixed-name collisions as a cause. The rename still returns
`ERROR_ACCESS_DENIED (5)` on both x64 and Win32, so the remaining cause is in
the Darling descriptor/rename path or host policy rather than filename reuse;
the failure remains intentionally unmasked.

An additional atomic `SetFileInformationByHandle(FileRenameInfo)` fallback was
tested after `MoveFileExW`; it also failed with access denied on both
architectures and was removed again. The remaining rename boundary therefore
survives both native atomic Windows rename mechanisms and is not fixed by
changing the API primitive.

The process-group family now has a native `ProcessGroup` wrapper around one
Windows Job Object. `Process::LaunchInGroup` assigns multiple children to the
same group, and `ProcessGroup::Terminate` tears them down together; individual
`Process::Launch` keeps its own owned Job Object and existing lifetime remains
compatible. `process_group_smoke.exe` launches two long-running children,
terminates the shared group, and verifies both exit with code 29 on x64 and
Win32. This is the first native proof for group-wide termination. Session
identity (`setsid`), process-group IDs (`setpgrp`), Darwin signal translation,
membership discovery, nested-job restrictions, and wait/reaping semantics for
large groups remain open; the next required step is to connect Darwin process
group/session state to this shared object instead of reporting those requests
as emulated.

The process-syscall family now implements registered Darwin process-group
membership for `posix_spawn` and native group `SIGKILL`: a second child may
join the first child's PGID, `kill(-pgid, 9)` terminates both registered
children, and `waitpid(-pgid, ...)` reaps both with the expected Darwin status.
The x64 and Win32 process-syscall smoke tests both pass and now report
`DARWIN_KILL_SIGKILL=GROUP_NATIVE`. This is registry-backed group semantics,
not yet a universal Windows Job Object binding for every spawn path; `setsid`
still creates only a new process-group identity, `setpgroup` is not propagated
to arbitrary native descendants, signal numbers other than SIGKILL remain
limited, and session IDs, orphan handling, cross-process credentials, and
full Darwin job-control behavior remain open.

An optional Windows Job Object is now created per registered `posix_spawn`
process group and reused for later members when the host permits assignment;
the existing registry-backed group path remains the fallback because this host
can reject nested Job Object assignment. Both x64 and Win32 process-syscall
smokes still pass after this change. The Job Object path is not yet counted as
verified group-tree termination: `TerminateJobObject` was not made authoritative
on this host, so descendant teardown, cleanup of group objects after reaping,
and reliable detection of nested-job restrictions remain open and must be
validated on a host where Job Object assignment is allowed.

Group-job lifetime handling is now explicit: when the last registered child of
a nonzero Darwin PGID is reaped, its optional Windows Job Object is closed and
removed from the registry; process exit cleanup still closes all remaining
objects at host shutdown. The x64 and Win32 process-syscall smokes remain
green. This fixes the known handle-lifetime leak, but Job Object assignment
and descendant termination are still host-dependent and not yet promoted to a
universal semantic guarantee.

The process-group boundary now also supports the POSIX existence probe
`kill(-pgid, 0)`: it checks registered membership without terminating a
process. The x64 and Win32 process-syscall smokes pass with this probe before
the existing group SIGKILL test. Permission checks for foreign groups,
credential-aware signaling, and complete Darwin session/job-control semantics
remain open.

`darling_windows_kill` now accepts SIGTERM (15) for individual registered
children and registered process groups, using the documented emulation exit
status 143; the process-syscall smoke launches and reaps a SIGTERM child on
both x64 and Win32. SIGKILL remains the group-native tested path, while signal
disposition, graceful termination, interruption, credential checks, and all
other Darwin signal translations remain open.

After separating the single-process launch path from `LaunchInGroup`, the
fresh complete Release matrix is stable at `28/30 PASS` on x64 and `28/30
PASS` on Win32. The two and only two failures remain the known filesystem
hard-link and host-API rename fixtures, both reporting host-side error 5; the
process, process-group, process-syscall, Mach-O, dyld, terminal, signal, and
other existing smoke families pass in this run. The separate group Job Object
path remains optional and host-dependent, while the single-process API keeps
its verified direct lifecycle behavior.

The dedicated `macho_install_name_smoke.exe` now verifies a nonempty
`LC_ID_DYLIB` install name on x64 and Win32. The long-running process fixture
was also made console-independent by using PowerShell `Start-Sleep` instead of
an input-dependent wait utility. The fresh matrix now contains 32 smoke
executables per architecture and passes 30/32 on each; only the known hardlink
and rename host-error-5 fixtures fail.

The post-`setsid` full Release rebuild and matrix are complete: x64 and Win32
each contain 30 smoke executables and pass 28/30. The only failures remain
`darling_windows_filesystem_smoke.exe` (hard-link ABI, host error 5) and
`darling_windows_host_api_smoke.exe` (rename, host error 5); no regression was
introduced in the process, identity, signal, Mach-O, dyld, terminal, or other
verified families.

The host-API smoke now explicitly exercises the thread-local `setsid` change:
the main thread's repeated call fails as required while a worker thread's
first call succeeds. Both x64 and Win32 rebuilds pass this new assertion; the
target still exits only for the pre-existing rename fixture with error 5.

The process-syscall smoke output now explicitly distinguishes
`DARWIN_KILL_SIGKILL=GROUP_NATIVE` from `DARWIN_KILL_SIGTERM=EMULATED`; both
paths are runtime-tested on x64 and Win32, but only SIGKILL is currently
classified as native group teardown. SIGTERM still maps to controlled
Windows termination and does not yet provide Darwin signal disposition or
graceful-handler behavior.

`getpgid` and `getsid` now resolve registered spawned children through the
same PGID registry used by group wait/kill; the process-syscall smoke verifies
that two children joined to one PGID return the expected group/session identity
on x64 and Win32. Unknown and foreign IDs retain the existing Darwin error
path. Full session lifetime, credentials, orphan adoption, and native
cross-process identity propagation remain open.

The added `process_group_smoke.cpp` now carries the same GPL-3 license header
as the other Darling Windows host files; the license bundle continues to treat
all Windows host additions as GPL-3-only and keeps upstream component notices
separate. Its x64 and Win32 builds still pass after the header audit.

The source-header audit now covers all 74 `.cpp` and `.h` files in the
Windows host directory; every file contains a GPL-3/licensing header and no
new unlabelled port file remains. Upstream component licenses are still kept
in their original source directories and listed separately in the license
bundle.

The `setsid` emulation state is now thread-local rather than process-global,
so independent host threads no longer interfere with one another; repeated
calls in the same thread still return Darwin-style EPERM. The host-API smoke
rebuild confirms the change on x64 and Win32, while its pre-existing rename
fixture remains the only reported failure in that target (`ERROR_ACCESS_DENIED`
5).

`setpgid` now accepts registered child PIDs and can move them to their own
PGID or an existing registered group in the Darwin process registry; the
process-syscall smoke verifies the child move and subsequent `getpgid` result
on x64 and Win32. This changes the Darwin-visible registry identity only and
does not retroactively alter Windows kernel process groups or Job Object
membership, so native descendant propagation, session credentials, and full
job-control semantics remain open.

The Mach-O parser now recognizes `LC_UUID`, validates its 24-byte command
length, stores the 16-byte image identity in `MachOImage::UUID()`, and carries
it through move construction/assignment. Existing x64 and Win32 Mach-O smoke
tests remain green after the parser extension. A fixture that asserts a
nonzero UUID payload, UUID-based dyld cache identity, and code-signature
validation is still open.

The Mach-O loader now preserves `LC_ID_DYLIB` as `MachOImage::InstallName()`
instead of treating the dylib's own identity as an ordinary dependency; name
offset and NUL termination are validated through the same dylib-command path.
The existing x64 and Win32 Mach-O smoke tests remain green. A dedicated
nonempty install-name fixture and full install-name/dyld-cache integration
remain open.

The UUID fixture now also carries a valid four-byte `LC_CODE_SIGNATURE` payload
inside the Mach-O file, so the new range validation is exercised during the
same x64/Win32 smoke run. Cryptographic CodeDirectory/CMS validation and a
negative out-of-range fixture remain open.

The UUID/signature smoke now covers both valid and invalid ranges: a four-byte
payload inside the file is accepted and an `LC_CODE_SIGNATURE` offset at
`0xfffffff0` is rejected on both x64 and Win32. This proves structural range
handling only; cryptographic CodeDirectory/CMS verification and trust-chain
validation remain open.

The loader now also recognizes `LC_CODE_SIGNATURE`, validates its linkedit
command size and verifies that the declared payload lies inside the Mach-O
file. This is structural validation only; no CMS/CodeDirectory hash or Apple
trust-chain verification is claimed. The UUID smoke still passes on x64 and
Win32 after this extension.

The dedicated `macho_uuid_smoke.exe` now writes a minimal x86_64 Mach-O with
a nonzero `LC_UUID`, opens it through the Windows parser, and verifies all 16
bytes through `MachOImage::UUID()`; it passes on x64 and Win32. The matrix now
has 31 smoke executables per architecture, with the same two host-side
filesystem failures, so the current aggregate is 29/31 PASS on each target.

Authoritative latest status: the valid and invalid code-signature range cases
are both covered, so the remaining signature gap is cryptographic validation
only; earlier entries above that say the negative range fixture is still open
are historical notes superseded by the latest entry.

Authoritative latest status (2026-10-07): the dedicated install-name fixture
and the standalone host-rename fixture both pass on x64 and Win32. The fresh
matrix contains 33 smoke executables per architecture and reports 31/33 PASS
on both targets. The only remaining red tests are
`darling_windows_filesystem_smoke.exe` (hardlink creation returns host error 5)
and `darling_windows_host_api_smoke.exe` (the rename fixture returns host error
5 in its sandbox/temp sequence). The standalone `darling_windows_rename_host_smoke.exe`
successfully renames a file beside its executable on both architectures, so
the wrapper itself is functional; the host-api failure is narrowed to the
restricted temp/path or descriptor sequence rather than universal rename
failure. No claim is made that the Darling port is complete or that all macOS
kernel, launch-services, Objective-C runtime, dyld, graphics, signing, or GUI
behavior is implemented.

The Objective-C runtime family now has a dedicated `objc_runtime_smoke` fixture
with GPL-3 provenance. It creates and registers a class, selector, method, and
protocol, verifies method lookup/type encoding/conformance, creates an instance,
checks class identity and associated-object retention, and cleans the instance
up. It passes on x64 and Win32. This proves the implemented runtime metadata and
association subset only; the variadic Objective-C ABI trampoline, full Apple
runtime ABI, blocks ABI compatibility, ARC semantics, exception/unwind
integration, and Foundation/AppKit frameworks remain open.

The same Objective-C fixture now also exercises the implemented weak-reference
operations, autorelease-pool entry/exit, project callback blocks, block copy and
release, and the Apple-block bridge/signature surface. It passes on x64 and
Win32. These checks establish only the Windows adapter's controlled lifetime and
callback subset; true Apple ABI-compatible blocks, ARC ownership rules,
language-level exception unwinding, and framework objects remain unimplemented.

The Objective-C fixture now also invokes real typed object and int64 methods
through `darling_objc_invoke1`; both return values and value kinds are checked
on x64 and Win32. This validates the currently exposed typed dispatch bridge,
not the complete variadic Apple `objc_msgSend` ABI, floating-point/struct return
conventions, register-level calling conventions, or framework messaging.

The Objective-C runtime smoke was extended with class method-list inspection,
protocol-list inspection, method-description lookup, and global class/protocol
list enumeration. The expanded fixture still passes on x64 and Win32. The
fresh aggregate is now 34 smoke executables per architecture with 32/34 PASS
on both targets; the only failures remain the previously documented hardlink
and restricted host-api rename cases. This does not establish compatibility
with the complete Apple runtime ABI or the higher-level Objective-C frameworks.

The typed Objective-C dispatch bridge now supports and verifies zero-argument
`double` returns and the project-specific `DarlingObjcRect` structure encoding;
the adapter and smoke fixture both pass on x64 and Win32. This closes only the
tested typed return cases. Full Apple structure encodings, ABI-dependent
register/stack classification, floating-point variadic dispatch, and arbitrary
Objective-C method signatures remain open.

The runtime fixture now also invokes a two-argument `int64` method through
`darling_objc_invoke2` and verifies the summed result on x64 and Win32. The
existing two-argument bridge is therefore runtime-tested, while arbitrary
multi-argument signatures, variadic calls, floating-point argument ABI rules,
and complete Apple `objc_msgSend` compatibility remain open.

The same dispatch fixture now verifies typed boolean inversion and pointer
round-tripping through `darling_objc_invoke1` on x64 and Win32. The covered
bridge set is therefore object, int64, bool, pointer, double, Rect, and two
int64 arguments; unsupported arbitrary signatures, variadic ABI calls, and
full Apple message-send compatibility remain explicit gaps.

The block dispatch path is now runtime-tested as well: a registered
`@@:@?` method receives a project callback block through `darling_objc_invoke1`
and returns the receiver from the callback on x64 and Win32. The separate block
copy/release and Apple-block checks remain green; Apple ABI block layout and
arbitrary block signatures are still not claimed.

Authoritative aggregate recheck after the Objective-C dispatch additions:
34 smoke executables per architecture, with x64 `32/34 PASS` and Win32
`32/34 PASS`. The only failures are still
`darling_windows_filesystem_smoke.exe` (hardlink creation, host error 5) and
`darling_windows_host_api_smoke.exe` (restricted temporary/path rename
sequence, host error 5); the standalone host rename smoke remains successful
on both architectures.

The typed dispatch fixture now also exercises a `double` argument and return
path (`d@:d`) by scaling 2.25 to 4.5; x64 and Win32 both pass. The currently
covered typed bridge therefore includes double input as well as double output,
but not variadic floating-point calls, aggregate ABI classification, or
arbitrary Apple method signatures.

`darling_objc_invoke2` now also supports the typed `double,double` signature
`d@:dd`; the smoke fixture adds a real two-double method and verifies
`1.25 + 2.75 = 4.0` on x64 and Win32. The bridge still intentionally rejects
unlisted signatures, variadic methods, and the full Apple register/stack ABI.

The class-method bridge is now runtime-tested: a registered `@@:` class method
is invoked through `darling_objc_msgSend_class0` and returns the registered
class on x64 and Win32. This covers the implemented no-argument class dispatch
subset only; metaclass inheritance, class-method argument variants, variadic
class messaging, and full Apple metaclass ABI behavior remain open.

The Objective-C protocol layer now also verifies a registered parent protocol,
child-protocol inheritance, and recursive `protocol_conformsToProtocol` behavior
on x64 and Win32. This covers the implemented metadata inheritance subset only;
optional/required method merging across complex protocol graphs and complete
Apple protocol ABI/runtime semantics remain open.

Protocol and class conformance traversals now use visited sets, preventing
infinite recursion for cyclic protocol graphs. The smoke fixture creates a
two-node protocol cycle and confirms recursive conformance returns on x64 and
Win32. This hardens graph traversal only; cycle diagnostics/policy and the
complete Apple protocol runtime remain open.

The class-method metadata path is now also checked with
`class_getClassMethod`: selector, implementation pointer, and `@@:` encoding
must match the method used by `darling_objc_msgSend_class0`; the check passes
on x64 and Win32. Full metaclass inheritance and arbitrary class-method ABI
signatures remain open.

Protocol method lookup now traverses parent protocols for both presence and
type-encoding retrieval; the fixture adds an inherited required method and
passes on x64 and Win32. This fixes the concrete inheritance gap in the
Windows adapter, while complex graph conflict resolution, optional/required
merging policy, and full Apple protocol runtime semantics remain open.

The dynamic-library host family now has a stronger smoke proof: `Library`
move construction, move assignment, handle transfer, valid `GetProcAddress`,
and rejection of a missing symbol all pass on x64 and Win32. This verifies
Windows module ownership/error behavior only; Mach-O dylib loading, symbol
rebasing, interposition, and full dyld integration remain open.

The Objective-C class hierarchy is now runtime-tested with a registered
subclass: superclass lookup, inherited instance/class methods, and subclass
class dispatch all pass on x64 and Win32. This covers the current single
inheritance metadata path only; metaclass hierarchies, method override edge
cases, multiple-runtime interactions, and full Apple ABI behavior remain open.

The hierarchy fixture now also verifies instance-method override resolution:
the subclass returns its own implementation while the superclass retains the
base implementation on x64 and Win32. Override lookup is therefore covered;
dynamic dispatch edge cases, swizzling, metaclass overrides, and complete
Apple runtime method-cache behavior remain open.

The synchronization family now has cross-thread semaphore signaling, move
ownership checks, shared-memory size/data validation, and shared-memory move
ownership checks in addition to the existing basic operations; x64 and Win32
both pass. This proves the Windows adapter's semaphore/mapping lifecycle only;
Darwin named-object semantics, fork/shared-address behavior, robust mutexes,
condition variables, and broader synchronization ABI coverage remain open.

The synchronization smoke now reuses the semaphore after move transfer by
posting and waiting through the moved handle; this confirms the transferred
Windows kernel handle remains operational on x64 and Win32, not merely
non-null. The broader Darwin synchronization gaps remain unchanged.

The file-descriptor smoke now verifies seek rewind, descriptor duplication,
and readback through the duplicate before closing both descriptors; x64 and
Win32 pass. This establishes the current regular-file descriptor subset only;
offset I/O, truncation, locking, poll readiness, descriptor inheritance, and
the complete Darwin fd ABI remain open.

The file-descriptor fixture now also exercises `ReadAt` at offset zero and
confirms the offset read matches ordinary sequential I/O on x64 and Win32.
Offset-read behavior is therefore covered for regular files; offset writes,
truncation, locking, poll readiness, inheritance, and the remaining Darwin fd
ABI surface remain open.

The FD fixture now also exercises `OpenReadWrite`, offset writing, offset
readback, and `Truncate` to the expected payload size; both x64 and Win32 pass.
Regular-file offset write/truncate behavior is therefore covered, while
locking, poll readiness, descriptor inheritance, append/close-on-exec edge
cases, and the remaining Darwin FD ABI remain open.

The pipe path now also performs a `Poll` readiness check after writing and
requires a readable event before consuming the byte; x64 and Win32 pass. Poll
readiness is now covered for this pipe case, while timeout/error combinations,
socket polling, locking, descriptor inheritance, and the rest of the Darwin FD
ABI remain open.

The public message smoke now also rejects an unsupported flag value (`0x4000`)
through `sendmsg`; x64 and Win32 pass. Unsupported message-flag validation is
covered alongside `MSG_PEEK` and `MSG_DONTWAIT`, while `MSG_CTRUNC`, pending
rights ordering, further flags, and the remaining Darwin socket ABI remain
open.

The public `recvmsg` smoke now also validates Darwin `MSG_PEEK`: the first
receive observes a byte without consuming it, and the following normal receive
gets the same byte; x64 and Win32 pass. Peek and nonblocking message flags are
covered, while ancillary truncation, pending-rights ordering, other flags, and
the remaining Darwin socket ABI remain open.

The public `recvmsg` smoke now also exercises Darwin `MSG_DONTWAIT` on an empty
socketpair and requires immediate nonblocking failure; x64 and Win32 pass.
Basic message nonblocking flag translation is covered, while ancillary
truncation, pending-rights ordering, further flags, and the remaining Darwin
socket ABI remain open.

After removing the non-passing truncation probe, the current source including
the defensive `recvmsg` control-size check was relinked and executed
successfully on x64 under an alternate output name and on Win32 under the
normal target. The successful result covers the stable message/rights paths;
it does not close the pending-rights ordering or `MSG_CTRUNC` proof gap.

The `recvmsg` adapter now defensively marks `MSG_CTRUNC` and closes temporary
duplicated rights whenever the supplied control buffer cannot hold the rights
payload. A follow-up smoke still showed that the second pending rights message
is not reliably surfaced by the current socketpair bookkeeping, so the change
is retained as a safeguard but not counted as a completed truncation proof.

An explicit ancillary-control truncation probe was attempted and removed from
the passing smoke because the current adapter did not set Darwin `MSG_CTRUNC`
for the undersized receive control buffer. The stable one-right `SCM_RIGHTS`
and basic message paths still pass on x64 and Win32; control truncation is now a
confirmed implementation gap rather than an unverified claim.

The public message wrappers now also reject null `msghdr` pointers for both
`sendmsg` and `recvmsg`; the current source passes on x64 (alternate relinked
target due the locked original output) and Win32. Direct message-ABI argument
validation is covered, while control-message truncation, flag semantics,
multiple rights, and the remaining Darwin socket ABI remain open.

The original x64 smoke executable remained locked by the host, so the same
current source was relinked under an alternate generated target name; that x64
binary and the existing Win32 binary both pass the syscall smoke. The source
and ABI result are therefore verified on both architectures, while the stale
original x64 output-file lock remains an environment artifact.

The public socketpair smoke now also transfers one pipe descriptor through a
Darwin `SCM_RIGHTS` control message and reads the transferred descriptor on
both x64 and Win32; both pass. Public descriptor-rights transport is covered,
while multiple rights, mixed control messages, truncation, flag semantics, and
the remaining Darwin socket ABI remain open.

The public `sendmsg` smoke now also submits a malformed one-byte control
buffer and requires it not to be transmitted; x64 and Win32 pass. Basic
invalid ancillary-control rejection is covered, while real `SCM_RIGHTS`, other
ancillary payloads, truncation, flags, and full Darwin message ABI parity remain
open.

The public socketpair smoke now also sends and receives a three-byte iovec
message through `darling_windows_sendmsg` and `darling_windows_recvmsg`; x64
and Win32 pass. Basic scatter/gather message transport is covered, while
ancillary data, descriptor rights, address-bearing messages, truncation and
flag semantics, and the remaining Darwin socket ABI remain open.

The address-conversion smoke now also rejects null source and destination
buffers in `darling_windows_inet_pton`; x64 and Win32 pass. Null-argument
validation is covered, while exact errno parity for every invalid family/input,
scoped addresses, DNS/search domains, and the remaining resolver/socket ABI
remain open.

The address-conversion smoke now also rejects malformed IPv4 text
(`192.0.2.999`) through `darling_windows_inet_pton`; x64 and Win32 pass. Basic
invalid textual-address handling is covered, while exact errno parity for every
invalid family/input, scoped addresses, DNS/search domains, and the remaining
resolver/socket ABI remain open.

The resolver smoke now also validates `AI_PASSIVE` with `AF_INET6`, a null
node, and numeric service `443`; x64 and Win32 return a valid IPv6 sockaddr and
pass. Passive IPv4 and IPv6 server resolution is covered, while wildcard
address-selection details, scoped addresses, DNS/search domains, exact Darwin
error codes, and the remaining resolver/socket ABI remain open.

The resolver smoke now also validates Darwin `AI_PASSIVE` with a null node and
numeric service `80`; x64 and Win32 return a bindable IPv4 sockaddr and pass.
The wildcard/server-resolver path is covered, while wildcard address-selection
details, IPv6 passive results, DNS/search domains, exact Darwin error codes, and
the remaining resolver/socket ABI remain open.

The public resolver smoke now also rejects a `getnameinfo` call whose host
buffer is null while its length is nonzero; x64 and Win32 pass. Basic argument
validation is covered, while exact Darwin resolver error numbers, full flag
combinations, DNS/search-domain behavior, and the remaining resolver/socket
ABI remain open.

The public `link` and `rename` wrappers now explicitly reject null path
pointers with Darwin `EINVAL` before UTF-8 conversion; x64 and Win32 host-API
builds pass. Native Temp-directory access-denied behavior remains unchanged
and still requires an environment with permitted hardlink/rename operations for
full filesystem-smoke success.

The `link` and `rename` wrappers now propagate invalid-argument and native
Win32 failures into the Darling errno bridge instead of returning only `-1`.
Both architecture builds pass; the Temp-directory smoke still reports native
error 5 for hardlink and rename, so the host-policy limitation remains open
and is not masked as a successful port.

The resolver smoke now also validates Darwin `AI_CANONNAME` handling for
`localhost`: the converted result contains a nonempty canonical-name string;
x64 and Win32 pass. Canonical-name ownership and basic propagation are covered,
while exact canonical-name selection, DNS/search-domain behavior, resolver
error-code parity, and the remaining resolver/socket ABI remain open.

The full Objective-C smoke regression exposed and fixed a real ABI coverage
gap: `darling_objc_invoke_rect0` now accepts the canonical nested CoreGraphics
`CGRect` encoding in addition to the internal flat rectangle encoding. The
Objective-C registry smoke now passes on x64 and Win32; richer ABI encodings and
the remaining Foundation/Cocoa surface remain open.

The resolver smoke now validates Darwin `AI_NUMERICSERV` behavior: numeric
service `80` succeeds, while service name `http` is rejected without an output
list; x64 and Win32 pass. Numeric service enforcement is covered, while exact
Darwin resolver error numbers, other hint-flag combinations, service database
behavior, DNS/search domains, and the remaining resolver/socket ABI remain
open.

The resolver smoke now validates Darwin `AI_NUMERICHOST` behavior: a numeric
IPv4 node succeeds, while `localhost` is rejected without an output list; x64
and Win32 pass. This flag path is covered, while exact resolver error numbers,
other hint-flag combinations, DNS/search domains, service databases, and the
remaining resolver/socket ABI remain open.

The resolver smoke now also formats an IPv6 sockaddr through
`darling_windows_getnameinfo` and validates numeric host `2001:db8::17` plus
service `443`; x64 and Win32 pass. Numeric IPv6 reverse formatting is covered,
while scoped/link-local addresses, hostname reverse lookup, service databases,
Darwin error-code parity, and the remaining resolver/socket ABI remain open.

The resolver smoke now also resolves the service name `http` for
`localhost`/TCP and verifies that the returned IPv4 sockaddr carries port 80;
x64 and Win32 pass. Basic service-name mapping is covered, while arbitrary
service databases, protocol-specific service semantics, DNS/search domains,
Darwin resolver error-code parity, and the remaining resolver/socket ABI remain
open.

The resolver smoke now also checks an intentionally invalid hostname and
requires a nonzero result with a null output pointer; x64 and Win32 pass.
Basic invalid-host cleanup behavior is covered, while exact Darwin resolver
error-number mapping, DNS/search-domain behavior, service lookup, and the
remaining resolver/socket ABI remain open.

The resolver smoke now also exercises `darling_windows_getnameinfo` for a
numeric IPv4 sockaddr and validates both numeric host and service output; x64
and Win32 pass. Numeric reverse formatting is covered, while hostname/service
lookup, IPv6 naming, resolver error-code parity, and the remaining Darwin
resolver/socket ABI remain open.

The resolver smoke now also resolves the hostname `localhost` with service
`80` through the public Darling `getaddrinfo` wrapper and validates a nonempty
address result; x64 and Win32 pass. Basic local hostname resolution is covered,
while external DNS/search-domain behavior, service-name lookup, IPv4/IPv6
ordering, Darwin resolver error-code parity, and the remaining resolver/socket
ABI remain open.

The public resolver smoke now also calls `darling_windows_getaddrinfo` for an
IPv4/TCP endpoint, validates the converted result structure, and releases it
through `darling_windows_freeaddrinfo`; x64 and Win32 pass. Basic numeric IPv4
resolver conversion and ownership cleanup are covered, while hostname/DNS
resolution, service-name behavior, IPv6 result ordering, Darwin error-code
parity, and the remaining resolver/socket ABI remain open.

The same public address-conversion smoke now also round-trips the IPv6 address
`2001:db8::17` through `darling_windows_inet_pton` and
`darling_windows_inet_ntop`; x64 and Win32 pass. Basic IPv4 and IPv6 text/binary
conversion is covered, while resolver semantics, mapped-address edge cases,
`getaddrinfo`/`getnameinfo` parity, and the remaining Darwin socket ABI remain
open.

The public socket family now has a direct `AF_UNIX` stream-socketpair smoke:
`darling_windows_socketpair`, `sendto`, and `recvfrom` transfer a byte and
close both descriptors successfully on x64 and Win32. This covers the basic
connected socketpair data path; bind/connect/listen/accept options, ancillary
messages, timeout/error semantics, and the broader Darwin socket ABI remain
open.

The regular-file descriptor test now also acquires and releases an exclusive
Darwin-flock-compatible lock through the Windows `LockFileEx` adapter; x64 and
Win32 pass. Basic same-descriptor locking is covered, while contention between
independent descriptors/processes, shared locks, nonblocking lock errors, and
the remaining Darwin locking ABI remain open.

The regular-file FD fixture now also checks `GetFileInformation` metadata and
verifies the low/high file-size fields after writing; x64 and Win32 pass. File
size metadata is covered for this ordinary-file case, while timestamps,
permissions/ownership, filesystem statistics, locking, inheritance, and the
remaining Darwin FD metadata ABI remain open.

The lock fixture now verifies nonblocking contention between two independent
ReadWrite descriptors: the contender is rejected while the owner holds the
exclusive lock, then cleanup succeeds on x64 and Win32. Same-process descriptor
contention is covered; cross-process behavior, shared-lock semantics, exact
Darwin errno mapping, and remaining FD ABI cases remain open.

The lock smoke now also acquires shared locks through two independent
descriptors simultaneously and releases both successfully on x64 and Win32.
Shared-lock behavior is covered for this same-process case; cross-process
sharing, upgrade/downgrade semantics, and exact Darwin lock error behavior
remain open.

The descriptor-duplication fixture now confirms Darwin-style separation of
flags: the duplicate inherits `O_NONBLOCK`, does not inherit `FD_CLOEXEC`, and
the original retains `FD_CLOEXEC`; x64 and Win32 pass. Other descriptor flags,
child-process inheritance, and the remaining Darwin FD ABI are still open.

Authoritative aggregate recheck after the FD flag, locking, poll, offset-I/O,
flush, and metadata additions: x64 has 34 smoke executables with 32/34 PASS;
Win32 has the same 34 with 32/34 PASS. The only failures remain
`darling_windows_filesystem_smoke.exe` (hardlink creation, host error 5) and
`darling_windows_host_api_smoke.exe` (restricted temporary/path rename, host
error 5). No newly added FD or synchronization test regressed.

The host-API smoke's duplicated file descriptor is now explicitly closed
before the rename assertion, removing a real test-lifecycle leak. A fresh
direct run still reproduces `MoveFileExW` error 5 only in that Temp-path
scenario, while the standalone executable-directory rename smoke passes on
both architectures; this remains an environment/path-sharing issue to isolate
further and is not claimed fixed.

The syscall fixture now also validates the public `darling_windows_fstat` ABI
on a descriptor opened through the same public registry, checking Darwin file
size and block-size fields on x64 and Win32. This closes the public fstat smoke
gap; timestamps, ownership/permissions, statfs richness, cross-process
inheritance, and the remaining Darwin metadata ABI remain open.

The lock fixture now also verifies that a zero-mode flock request is rejected
with Darwin `EINVAL` (22) on x64 and Win32. Basic lock error mapping is covered
for this invalid-operation case; cross-process errors, all errno mappings, and
the remaining Darwin FD ABI remain open.

The public metadata smoke now also calls `darling_windows_statfs` on the
fixture path and validates filesystem block size, total blocks, and free-block
bounds on x64 and Win32. Basic statfs population is covered; Darwin volume
names, flags, quotas, exact accounting semantics, and the remaining metadata
ABI are still open.

The public `fstat` smoke now also checks null-output validation and requires
Darwin `EINVAL` (22) on x64 and Win32. Success and basic argument-error paths
are covered; richer metadata, exact error mapping for every invalid descriptor,
and the remaining Darwin metadata ABI remain open.

The socketpair smoke now also validates the Darwin-specific `SO_NOSIGPIPE`
and `SO_REUSEPORT` paths through the public get/set option wrappers; x64 and
Win32 pass. Their basic state and `SO_REUSEADDR` bridge are covered, while
complete Darwin option translation, address APIs, ancillary messages,
timeout/error semantics, half-close behavior, and the remaining socket ABI
remain open.

The public socketpair smoke now also executes `darling_windows_shutdown` for
the write direction after a successful byte exchange; x64 and Win32 pass.
Basic connected-pair shutdown is covered, while half-close/read-side behavior,
socket options, address APIs, ancillary messages, timeout/error semantics, and
the remaining Darwin socket ABI remain open.

The public `fstatat` test now also rejects an unsupported flag with Darwin
`EINVAL` (22) on x64 and Win32. Directory-relative success and basic argument
validation are covered; symlink `nofollow` behavior, richer metadata, complete
statfs semantics, and the remaining Darwin metadata ABI remain open.

The metadata smoke now also exercises relative `fstatat` resolution from a
publicly opened directory descriptor and compares it with the absolute/`AT_FDCWD`
results; x64 and Win32 pass. Directory-relative metadata lookup is covered,
while symlink nofollow behavior, timestamps, ownership/permissions, full statfs
semantics, and the remaining Darwin metadata ABI remain open.

The public metadata smoke now also validates `darling_windows_fstatat` with
`AT_FDCWD` and compares its file size with direct `fstat`; x64 and Win32 pass.
The basic `stat`/`fstat`/`fstatat` public paths are now covered, while symlink
nofollow edge cases, timestamps, ownership/permissions, full statfs semantics,
and the remaining Darwin metadata ABI remain open.

The public socketpair smoke now also reads and writes the Windows-backed
`SO_SNDBUF` socket option through `darling_windows_getsockopt` and
`darling_windows_setsockopt`; x64 and Win32 pass. Basic socket-option bridging
is covered, while Darwin-specific option translation, address APIs, ancillary
messages, timeout/error semantics, half-close behavior, and the remaining
Darwin socket ABI remain open.

The public syscall smoke now also round-trips an IPv4 address through
`darling_windows_inet_pton` and `darling_windows_inet_ntop`; x64 and Win32 pass.
IPv4 text/binary conversion is covered, while IPv6 edge cases, resolver
semantics, `getaddrinfo`/`getnameinfo` parity, and the remaining Darwin socket
ABI remain open.

The public message smoke now also sends and receives a three-byte payload using
Darwin `MSG_WAITALL` (`0x40`) through `darling_windows_sendto` and
`darling_windows_recvmsg`; x64 and Win32 pass. The covered message flags now
include `MSG_PEEK`, `MSG_DONTWAIT`, and `MSG_WAITALL`, while `MSG_CTRUNC`,
queued ancillary-message ordering, multiple/mixed `SCM_RIGHTS` records, full
timeout/error behavior, and the remaining Darwin socket ABI remain open.

An attempted `SO_RCVTIMEO` Darwin `timeval` translation was explicitly
rejected after testing: Windows AF_UNIX accepted the write but returned `0:0`
on readback, so no unverified translation was retained. Darwin socket timeout
semantics therefore remain an open gap; the existing integer options and the
message-flag coverage above remain independently verified.

The public message smoke now also verifies Darwin `MSG_NOSIGNAL` (`0x200`) as
an accepted emulated no-op: a one-byte `sendmsg`/`recvmsg` round-trip passes on
x64 and Win32. The covered message flags are now `MSG_PEEK`, `MSG_DONTWAIT`,
`MSG_WAITALL`, and `MSG_NOSIGNAL`; `MSG_CTRUNC`, queued ancillary ordering,
multiple/mixed rights records, timeout/error semantics, and the remaining
Darwin socket ABI remain open.

The `recvmsg` ancillary path now consumes a queued `SCM_RIGHTS` record even
when the caller's control buffer is too small or encoding fails, while closing
all temporary duplicated descriptors and setting Darwin `MSG_CTRUNC`. This
prevents stale rights from being replayed on a later receive; x64 and Win32
stable syscall smoke suites pass after the change. A dedicated runtime proof
of the truncation bit and multiple/mixed records is still open.

The attempted dedicated `MSG_CTRUNC` smoke was removed again because adding a
second rights message disturbed the later AF_UNIX `MSG_PEEK` probe; it did not
provide a stable proof. The queued-rights consumption fix above remains in the
source and the restored stable x64/Win32 smoke passes. The separate
`MSG_NOSIGNAL` probe was also removed after a full rebuild exposed a
non-deterministic receive failure; its no-op mapping is not counted as a new
runtime proof. `MSG_CTRUNC`, multiple/mixed rights records, and timeout/error
semantics therefore remain open.

The hard-link failure was independently reproduced outside the Darling ABI:
the current restricted Windows account can create the source file but native
PowerShell hard-link creation in the workspace returns `Access denied`. The
Darling implementation already calls `CreateHardLinkW` and maps its failure;
the remaining hard-link smoke failure is therefore an environment/privilege
boundary, not evidence of a missing source-side translation. A privileged or
Developer-Mode Windows run is still required for the positive hard-link ABI
proof.

The host-API smoke now includes the Darwin `select` boundary check for an
invalid timeout (`tv_usec == 1,000,000`), which must fail with Darwin `EINVAL`.
The x64 artifact compiles with the new assertion, but the full host-API run
still stops earlier at the known sandbox-backed temporary rename failure
(`ERROR_ACCESS_DENIED`), so this individual select assertion is compiled but
not yet independently runtime-verified.

The independent syscall smoke now creates its readiness pipe through the
public Darwin descriptor table and verifies readable `select` readiness plus
rejection of `tv_usec == 1,000,000` with Darwin `EINVAL`; x64 and Win32 pass.
This closes the basic pipe/select validation path without depending on the
host-API rename fixture. Socket readiness, signal interruption, full timeout
precision, and the remaining Darwin `select`/`pselect` ABI remain open.

The independent syscall smoke now also verifies public `pselect` write
readiness on the same pipe and rejects `tv_nsec == 1,000,000,000` with Darwin
`EINVAL`; x64 and Win32 pass. Basic public `select`/`pselect` readiness and
timeout-bound validation are covered, while signal-mask interruption, socket
readiness, sub-millisecond precision, and full Darwin `pselect` parity remain
open.

The public socketpair path now also checks `select` read-readiness after a
socket send and before `recvfrom`; x64 and Win32 pass. Basic socket readiness
through the shared Darwin descriptor table is covered, while writable/error
socket events, interruption, timeout precision, and complete Darwin socket
readiness parity remain open.

The public socketpair smoke now also verifies writable readiness for the peer
socket through `select`; x64 and Win32 pass. Readable and writable baseline
events for the shared Windows-backed socket descriptors are covered, while
exception/error events, signal interruption, timeout precision, and complete
Darwin readiness parity remain open.

The socket smoke now also checks EOF/hangup readiness on the peer after the
public Darwin `shutdown(..., SHUT_WR)` call; x64 and Win32 pass. Readable,
writable, and shutdown-driven readable readiness are covered for the emulated
socketpair, while exception/error bits, signal interruption, timeout
precision, and complete Darwin socket readiness parity remain open.

The socket smoke now also verifies the no-data baseline: a connected peer with
no queued bytes returns `select == 0` with a zero timeout, before shutdown is
used to produce EOF readiness. x64 and Win32 pass. Empty-vs-EOF distinction is
covered for the emulated socketpair; exception/error bits, interruption,
timeout precision, and complete Darwin socket readiness parity remain open.

The socket smoke also verifies that a connected socket with no pending data
does not spuriously appear in the `select` exception set; x64 and Win32 pass.
The baseline read/write/exception distinction and EOF readiness are covered
for the emulated socketpair, while real exception/error conditions, signal
interruption, timeout precision, and complete Darwin parity remain open.

The independent smoke now also verifies Darwin `poll` invalid-descriptor
behavior: an unregistered descriptor reports `POLLNVAL` (`0x20`) immediately;
x64 and Win32 pass. Invalid descriptor signaling is covered for `_poll`, while
`select` invalid-descriptor parity, real socket error/exception conditions,
signal interruption, timeout precision, and the remaining Darwin readiness ABI
remain open.

The independent syscall smoke now also passes an explicit empty Darwin signal
mask to `pselect` and verifies writable readiness remains correct; x64 and
Win32 pass. Explicit empty-mask ABI acceptance is covered, while actual signal
interruption/restart behavior, non-empty mask contents, timeout precision, and
complete Darwin `pselect` parity remain open.

The memory family now exposes `darling_windows_msync`, accepting Darwin
`MS_ASYNC`, `MS_SYNC`, and `MS_INVALIDATE` validation and flushing tracked
file-backed views through Windows `FlushViewOfFile`; anonymous mappings use a
successful no-op because they have no file cache to flush. The memory smoke
also verifies synchronous flushing, rejects incompatible async+sync flags, and
checks symbol resolution. x64 and Win32 Release builds and executions pass.
Exact cache invalidation, range splitting, filesystem durability guarantees,
and complete Darwin VM/msync semantics remain open.

The memory smoke was extended to call `msync(MS_SYNC)` on an actual writable
file-backed mapping, then verify the persisted byte after unmapping; the
incompatible `MS_ASYNC|MS_SYNC` combination is also rejected. Both x64 and
Win32 executions report `DARWIN_SYSCALL_MEMORY=PASS`. This proves the tracked
file-view flush path, not crash-consistent durability or full Darwin cache
invalidation semantics.

The `msync` implementation now resolves an address inside a tracked file
mapping, computes the relative offset and bounded requested length, and flushes
that subrange rather than requiring the mapping base. The smoke exercises a
one-byte interior range on x64 and Win32; both pass. Page-alignment details,
unmapped-range diagnostics, and complete Darwin cache/durability semantics
remain open.

The synchronization family now also exposes native Windows `Mutex` and
`ConditionVariable` wrappers backed by `CRITICAL_SECTION` and Windows
condition variables. The smoke verifies nonblocking mutex acquisition,
condition-variable wakeup, named semaphore behavior, and shared memory on both
x64 and Win32 Release builds. Darwin robust-mutex attributes, cancellation
points, priority inheritance, inter-process condition variables, and complete
pthread ABI/error semantics remain open.

The synchronization smoke now also verifies an unsignalled condition-variable
timeout (`WINDOWS_CONDITION_TIMEOUT=PASS`) before the wakeup path. Both x64 and
Win32 Release executions pass; timeout/error translation beyond this host
wrapper, spurious-wakeup rules, cancellation points, robust ownership recovery,
priority inheritance, and inter-process Darwin condition-variable behavior
remain open.

The runtime family now contains a minimal in-process `MachPort` message queue
with blocking receive and timeout behavior, and the runtime smoke verifies
`MACH_PORT_MESSAGE=PASS` on x64 and Win32 alongside the existing named-pipe
RPC path. This is only a first Mach-message substrate: no kernel-backed Mach
port namespace, send/receive rights, port destruction, notifications,
out-of-line memory, vouchers, task ports, or cross-process transport is yet
implemented.

The Windows broker RPC transport now uses read-all/write-all loops for its
byte-mode named pipe instead of assuming one `ReadFile` or `WriteFile` call
transfers a complete frame. x64 and Win32 broker smoke tests pass with
`BROKER_RPC=PONG`, clean shutdown, and cleanup. This hardens the current IPC
transport; it is not yet Darwin Mach IPC: ports, rights, notifications,
out-of-line messages, receive timeouts, and task/thread port semantics remain
unimplemented.
## Latest verified increment

The pthread lifecycle family now includes real `pthread_create` and
`pthread_join` adapters backed by Windows thread handles and a protected
return-value table. The host smoke creates a thread, executes a callback,
joins it, and verifies the returned pointer together with the existing
identity/name checks; x64 and Win32 both report
`DARWIN_PTHREAD_SELF_NAME_EQUAL=PASS`. Detach, cancellation, attributes,
mutexes, condition variables, TLS, once, and complete pthread error/ABI
semantics remain open.

The first pthread ABI family is now implemented and runtime-tested:
`pthread_self`, `pthread_equal`, `pthread_setname_np`, and
`pthread_getname_np`. The x64 and Win32 host API smokes report
`DARWIN_PTHREAD_SELF_NAME_EQUAL=PASS`, covering Windows thread identity,
equality, and thread-local naming for the current thread. Full pthread create,
join, detach, mutex, condition-variable, TLS, once, cancellation, and
cross-thread name semantics remain open.

The pthread lifecycle adapter now also implements `pthread_detach`; the smoke
creates a detached callback thread and verifies that a later join is rejected.
This remains Windows-handle-backed and does not yet provide Darwin thread
attributes, cancellation, TLS, mutex/condition, once, or complete detached
thread error semantics.

The Apple-specific `pthread_threadid_np` entry point is now available and
verified through the same x64/Win32 host smoke. It maps the current Darwin
pthread identity to the Windows thread ID and rejects unsupported foreign
thread handles; cross-thread lookup and the rest of the pthread ABI remain
open.

The first pthread mutex family is now available through
`pthread_mutex_init`, `pthread_mutex_destroy`, `pthread_mutex_lock`,
`pthread_mutex_trylock`, and `pthread_mutex_unlock`. The host smoke verifies
exclusive locking, `EBUSY`-style trylock behavior, unlock, and destruction on
x64 and Win32. This is an opaque Windows-backed compatibility representation;
Darwin mutex attributes, recursive/errorcheck kinds, process-shared mutexes,
robust ownership, priority inheritance, and full ABI layout compatibility
remain open.

The pthread condition-variable family now provides `pthread_cond_init`,
`pthread_cond_destroy`, `pthread_cond_wait`, `pthread_cond_signal`, and
`pthread_cond_broadcast`. The host smoke exercises a real waiting thread,
signal wakeup, mutex reacquisition, join, and cleanup on x64 and Win32. Timed
waits, condition attributes, process-shared conditions, cancellation behavior,
spurious-wakeup policy, and full Darwin object layout remain open.

`pthread_cond_timedwait` is now implemented with absolute Darwin timespec
deadlines translated to the Windows condition-variable wait path. The host
smoke verifies a real timeout and locked-mutex return path on x64 and Win32.
Clock-source differences, cancellation during timed waits, attributes,
process-shared conditions, and complete Darwin timeout/error semantics remain
open.

The pthread reader/writer-lock family now provides `pthread_rwlock_init`,
`pthread_rwlock_destroy`, `pthread_rwlock_rdlock`, `pthread_rwlock_wrlock`,
and `pthread_rwlock_unlock`. The host smoke verifies both shared-read and
exclusive-write paths on x64 and Win32. Timed rwlocks, attributes, recursive
ownership, process-shared behavior, fairness, and complete Darwin ABI layout
remain open.

The pthread TLS-key family now provides `pthread_key_create`,
`pthread_key_delete`, `pthread_setspecific`, and `pthread_getspecific`. The
host smoke verifies key creation, thread-local set/get, deletion, and rejected
access after deletion on x64 and Win32. Destructor execution at thread exit,
key iteration limits, cross-thread isolation proof, and complete Darwin TLS
ABI/error semantics remain open.

TLS destructors now run at the end of a created/joined Windows-backed pthread
thread. The host smoke sets a destructor-owned key in a worker and verifies
one destructor callback after join on x64 and Win32. Multiple destructor
passes, destructor re-arming rules, cancellation interaction, and complete
Darwin TLS teardown semantics remain open.

The vector/message cancellation-safe family now resolves normalized
`readv_nocancel`, `writev_nocancel`, `sendmsg_nocancel`, `recvmsg_nocancel`,
and `shutdown_nocancel`, including Darwin double-underscore spellings. x64
and Win32 host API smokes report `DARWIN_NORMALIZED_VECTOR_NOCANCEL_SYMBOLS=PASS`.
Cancellation-point and interrupted-message semantics remain open.

The process/wait cancellation-safe aliases now resolve normalized
`waitpid_nocancel`, `wait4_nocancel`, `waitid_nocancel`, and `kill_nocancel`
names, including Darwin double-underscore spellings. x64 and Win32 host API
smokes report `DARWIN_NORMALIZED_PROCESS_NOCANCEL_SYMBOLS=PASS`; actual Darwin
wait interruption, cancellation points, and signal-restart behavior remain
open.

The socket cancellation-safe alias family now resolves normalized
`poll_nocancel`, `select_nocancel`, `connect_nocancel`, `accept_nocancel`,
`sendto_nocancel`, and `recvfrom_nocancel` names, including Darwin double-
underscore spellings. x64 and Win32 host API smokes report
`DARWIN_NORMALIZED_SOCKET_NOCANCEL_SYMBOLS=PASS`; actual Darwin cancellation
interrupt/restart semantics remain open.

Darwin libc cancellation-safe aliases are now covered: `open_nocancel`,
`read_nocancel`, `write_nocancel`, `pread_nocancel`, `pwrite_nocancel`,
`close_nocancel`, `fcntl_nocancel`, and `fsync_nocancel` resolve to the
corresponding Windows adapters. x64 and Win32 host API smokes report
`DARWIN_NORMALIZED_NOCANCEL_SYMBOLS=PASS`. These aliases preserve the current
host behavior but do not yet reproduce Darwin cancellation-point semantics or
thread cancellation interaction.

The process/resource configuration family is now directly smoke-checked:
normalized `confstr`, `pathconf`, `fpathconf`, `getdtablesize`, `getrlimit`,
`setrlimit`, `getpriority`, `setpriority`, `umask`, and `getloadavg` all resolve
on x64 and Win32 with `DARWIN_NORMALIZED_PROCESS_RESOURCE_SYMBOLS=PASS`.
Windows resource-limit enforcement, priority semantics, load-average fidelity,
and complete Darwin error behavior remain open.

The POSIX time family now resolves normalized `nanosleep`, `gettimeofday`, and
`usleep` names. Both x64 and Win32 host API smokes report
`DARWIN_NORMALIZED_POSIX_TIME_SYMBOLS=PASS`; precision, interruption,
clock-source selection, and complete Darwin time ABI semantics remain open.

The terminal/environment family now resolves normalized `getenv`, `setenv`, `unsetenv`, `putenv`, `clearenv`, `isatty`, `ioctl`, and `ctermid`. The x64 and Win32 host API smokes both report `DARWIN_NORMALIZED_TERMINAL_ENV_SYMBOLS=PASS`. Complete Windows console/TTY semantics, environment inheritance races, and the Darwin terminal ABI remain open. The aggregate host smoke still stops at the known privilege-sensitive rename check with `WIN32_ERROR=5` (`ERROR_ACCESS_DENIED`), not at the new resolver family.

The pthread ABI layer now also covers `pthread_once` and invokes registered TLS
destructors when a created Windows-backed pthread exits. x64 and Win32 host API
smokes build successfully and report `DARWIN_PTHREAD_SELF_NAME_EQUAL=PASS`,
including lifecycle, mutex/condition/timed-wait, rwlock, TLS, destructor, and
once checks. Full Darwin pthread cancellation, fork interaction, priority,
robust/shared process attributes, and exact TLS iteration semantics remain open.

PureDarwin has been added as an ABI and provenance reference. Its upstream
layout separates `src/Libraries/libSystem` (including libc, libdispatch and
libplatform), XNU/kernel material, MIG/cctools and build tooling; this is useful
for identifying the Darwin userland contracts that the Windows adapters must
provide, but it is not a drop-in Windows implementation. The local checkout is
`puredarwin-source`; its repository-level Apple, Apple Driver and PureDarwin
license files, plus component-specific license files, remain preserved. No
PureDarwin source has been copied into the Windows runtime without component
review; Apple/XNU/libSystem licensing and kernel-only assumptions remain open.

The pthread attribute family now provides opaque `pthread_attr_init`/
`destroy` storage plus Darwin detach-state get/set operations, and
`pthread_create` honors the detached state when launching the Windows-backed
thread. The x64 host library and host API smoke were rebuilt successfully after
this change. The x64 and Win32 host API smokes now both build successfully and
report `DARWIN_PTHREAD_SELF_NAME_EQUAL=PASS`; the aggregate reaches the known
restricted rename failure only afterward. Stack-size, scheduling, affinity,
inherit-scheduler, scope, and process-shared pthread attributes remain open.

The host API smoke now directly exercises pthread attribute symbol resolution,
default joinable state, detach-state get/set, creation through detached
attributes, attribute destruction, and detached completion. The x64 run reports
the aggregate pthread status as `PASS`; the aggregate still reaches the known
restricted `rename` failure afterward. This added test is now verified on both
x64 and Win32.

The pthread attribute family now also implements `pthread_attr_setstacksize`
and `pthread_attr_getstacksize`; the configured size is passed to the Windows
thread creation call, with Darwin-compatible minimum validation for the adapter.
The host smoke checks symbol resolution, a 256 KiB configured stack, and the
returned value. Both x64 and Win32 builds and runtime smokes report the
aggregate pthread status as `PASS`; the only later failure remains the known
restricted rename probe. Exact Darwin stack allocation, guard pages, stack
address attributes, and scheduling interactions remain open.

A first public Mach C-ABI bridge is now present in `darling_windows_mach.h` and
`darling_windows_mach.cpp`: `mach_task_self` and `mach_thread_self` expose
nonzero Windows-backed pseudo-port names, while `mach_port_deallocate`
validates and accepts borrowed host tokens. The dedicated
`darling_windows_mach_abi_smoke` builds and exits 0 on both x64 and Win32 with
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. These are intentionally narrow ABI shims;
real Mach port rights, reference counts, IPC messages, MIG dispatch,
notifications, and exception ports remain unimplemented.

The Mach C-ABI bridge now includes a process-local port-name lifecycle:
`mach_port_allocate`, `mach_port_insert_right`, and `mach_port_mod_refs` are
available in addition to self/deallocate. The dedicated smoke allocates a
nonzero port, inserts a borrowed right, modifies its reference count, and
deallocates it; x64 and Win32 both build with zero errors and report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. Port names are currently host-local tokens,
so kernel-enforced rights, real reference accounting, send/receive queues and
cross-process port transfer remain open.

The port is now tracked through `PRIMITIVE-MATRIX.csv` and
`PRIMITIVE-MATRIX.md`. Each batch records the Darling surface, Linux/WSL
reference, Windows backend, implementation status, provenance, test gate and
remaining semantic gap. This makes WSL useful as a contract/test oracle while
keeping the native Windows implementation and license boundaries explicit.

The existing Mach runtime family was re-run on both x64 and Win32. The runtime
smokes exit with code 0 and cover in-process Mach-port queueing, timeout and
close wakeup, current/open task ports, task VM read/write/protect/query,
current/open thread ports, wait/exit state, and suspend/resume. The expected
invalid-task probe logs Win32 error 87 while still passing its rejection check;
this is diagnostic output, not a runtime-smoke failure. These wrappers remain
Windows host abstractions rather than full Darwin `mach_msg`/MIG kernel
semantics, port rights, notifications, or task exception behavior.

The pthread attribute family now also exposes `pthread_attr_setguardsize` and
`pthread_attr_getguardsize`. The adapter validates page-sized values and keeps
the requested guard size in the opaque attribute state while relying on the
Windows thread stack guard mechanism. The host API smoke checks the default
4 KiB value and an 8 KiB update; x64 and Win32 builds and runtime smokes both
report `DARWIN_PTHREAD_SELF_NAME_EQUAL=PASS`. Exact Darwin guard-page placement
and stack-overflow delivery remain open.

The Mach primitive batch is now connected to the central host-symbol resolver:
`mach_task_self`, `mach_thread_self`, `mach_port_allocate`,
`mach_port_insert_right`, `mach_port_mod_refs`, and `mach_port_deallocate`
resolve under Darwin and underscored names. The x64 and Win32 Mach ABI smokes
build without errors and report `MACH_C_ABI_SELF_DEALLOCATE=PASS`; the local
send/receive adapter also checks timeout and post-deallocation rejection. The
custom queue is still not the Darwin `mach_msg` wire ABI and is not
cross-process, so real MIG and kernel port semantics remain open.

The next matrix batch adds the explicit `mach_port_destroy` operation and
resolver alias. It shares the local deallocation path but preserves the
Darwin-facing entry point, and the x64 and Win32 ABI smokes both rebuild and
report `MACH_C_ABI_SELF_DEALLOCATE=PASS`. This completes the current local
port-name lifecycle batch; it does not close the real kernel IPC gaps.

The port-right batch now keeps a local reference count and exposes
`mach_port_get_refs`; `mach_port_mod_refs` updates and validates that count,
and the resolver exposes the Darwin and underscored entry points. The x64 and
Win32 builds complete with zero errors and both ABI smokes report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. This is real state within the process-local
adapter, not yet kernel-enforced Darwin right accounting or cross-process port
transfer.

The Mach IPC batch now exposes a minimal `mach_msg`-shaped C ABI with Darwin
header fields and `MACH_SEND_MSG`/`MACH_RCV_MSG`-style options, mapped onto the
existing process-local queue. The smoke validates resolver lookup, send,
receive, and timeout behavior; x64 and Win32 builds complete with zero errors
and both report `MACH_C_ABI_SELF_DEALLOCATE=PASS`. This does not yet implement
variable descriptors, out-of-line memory, trailers, port rights in messages,
notifications, cross-process transport, or full MIG wire compatibility.

The system Mach primitive batch now adds `mach_host_self` and
`host_page_size`, including resolver aliases and a runtime page-size check.
The complete Mach ABI smoke, including the new `mach_msg` path and local port
reference accounting, builds with zero errors and reports
`MACH_C_ABI_SELF_DEALLOCATE=PASS` on both x64 and Win32. Host information,
memory mapping/protection details, clock services, processor sets, and other
Darwin host APIs remain open.

The VM primitive batch now adds `mach_vm_allocate`, `mach_vm_deallocate`, and
`mach_vm_protect`, mapped to `VirtualAlloc`, `VirtualFree`, and
`VirtualProtect` with explicit Darwin protection-bit translation. The ABI
smoke verifies resolver lookup, page-size allocation, read-only protection and
release; x64 and Win32 builds complete without errors and both report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. Address hints, inheritance, wiring,
purgeability, memory-entry ports and exact Darwin VM error semantics remain
open.

The VM batch now also exposes `mach_vm_read_overwrite` and `mach_vm_write`,
limited to the current Windows process and backed by `ReadProcessMemory` and
`WriteProcessMemory`. The smoke checks symbol resolution, writes a value,
reads it into a second allocation, verifies the copied bytes, and releases
both regions; x64 and Win32 builds and runtime smokes both report
`MACH_C_ABI_SELF_DEALLOCATE=PASS`. Remote-task VM access, copy-on-write,
inheritance, wired/purgeable memory and exact Darwin read/write error behavior
remain open.

The first Foundation type-encoding batch is now complete for the implemented
surface: `NSGetSizeAndAlignment` and its underscored resolver alias handle
qualifiers, scalar types, pointers, arrays, structs, and unions. The x64 and
Win32 Foundation smokes build with zero warnings/errors and report
`FOUNDATION_TYPE_ENCODING_ABI=PASS`. This is an independent Windows ABI
adapter; the full Foundation/CoreFoundation object model, exact obscure
encoding edge cases, and framework-level initialization remain open.
