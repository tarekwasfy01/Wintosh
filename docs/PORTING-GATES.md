# Wintosh Porting Gates

This document is the completion checklist for the Windows port. A gate is
complete only when its implementation, focused tests, end-to-end evidence,
and remaining-gap documentation all agree.

| Gate | Scope | Current status | Required proof |
|---|---|---|---|
| 1 | Darwin startup and `libSystem` initialization | partial | Guest entry observes initialized `argc`/`argv`/`envp`, TLS, `errno`, `atexit`, and ordered startup/shutdown callbacks |
| 2 | Frameworks and `.app` bundles | partial | Minimal bundle loads its executable and real framework-shaped dependencies through the prefix resolver |
| 3 | dyld semantics | partial | Weak bindings, re-exports, initializer ordering, Objective-C registration, and dependent-image end-to-end execution |
| 4 | ARM64 Mach-O execution | partial | Native ARM64 Windows execution fixture, or an explicit architecture-specific backend boundary with a separate build target |
| 5 | Objective-C and calling conventions | partial | ABI-distinguishing tests for variadic, floating-point, aggregate, block, and exception calls |
| 6 | Foundation/CoreGraphics/AppKit families | partial | Documented API-family coverage plus executable integration tests for each supported family |
| 7 | Mach IPC/thread/exception semantics | partial | Cross-thread, port, timeout, exception, and cancellation tests with Darwin-visible result layouts |
| 8 | Licensing and fixtures | partial | Provenance inventory, applicable notices, and only redistributable real binaries or original replacement fixtures |

## Current baseline

The repository currently proves a controlled x86_64 Mach-O loader/bootstrap,
selected dyld fixups, host/Foundation/CoreGraphics/Objective-C/Mach adapters,
and several synthetic fixtures. These are baseline evidence, not completion of
any gate above. ARM64 execution, complete `libSystem` startup, broad Apple
framework compatibility, and arbitrary Apple binaries remain outside the
verified scope.

On 2026-10-12 the focused family checks `wintosh_foundation_smoke`,
`wintosh_corefoundation_smoke`, `wintosh_coregraphics_smoke`,
`wintosh_filesystem_smoke`, and `wintosh_paths_smoke` all passed in the local
Clang build. This confirms the currently implemented adapter families only;
it is not evidence for full Foundation, CoreGraphics, or AppKit compatibility.

The Mach/exception subset was also rechecked on 2026-10-12: all eleven
`wintosh_mach_*_smoke` tests passed, together with process, process-group,
pthread synchronization, thread, signal, and Objective-C exception smokes.
This proves the implemented message/trailer/ownership and host-thread
contracts, not complete Darwin Mach IPC or exception semantics.

After isolating the host-policy-sensitive tests (`host_api`, socket,
`socketpair`, and timing-sensitive sync), the remaining 65-test Clang/Ninja
suite passed 65/65 on 2026-10-12. This includes all Mach-O fixture E2E tests.
The excluded tests remain separately tracked failures or long-running host
policy checks and are not silently counted as passing.

## Gate procedure

For each gate, record the implementation files, test command and exit status,
fixture provenance, and remaining gaps in `docs/CURRENT-STATE.md`. Do not use a
parser-only or metadata-only test as proof of executable compatibility.

## Latest Gate 1 evidence

On the current Windows Clang recovery build, `wintosh_macho_exit_smoke.exe`
and `wintosh_dyld_smoke.exe` both returned exit code 0. These tests prove the
controlled startup/exit and dyld preparation paths. Gate 1 remains `partial`:
there is still no guest fixture that registers `atexit`/`__cxa_atexit` from
inside its Mach-O entry, and no complete `libSystem` initialization contract.

On 2026-10-12 the same build also rebuilt and ran
`wintosh_atexit_smoke.exe`; all three focused targets returned exit code 0.
The dyld run additionally reported passing framework shorthand, bundle
framework, rpath, weak-bind, re-export, chained-fixup, lazy-bind, and
dependent-entry checks. This is regression evidence for the existing loader
path; it does not promote Gate 1 beyond `partial`.

The new `wintosh_guest_atexit_e2e` fixture now supplies the missing narrow
guest-side proof: a synthetic Mach-O calls `_atexit`, returns normally, and
the finalizer invokes a guest callback which calls `_puts`. It passed locally
with `wintosh_hello_e2e` in the Clang build. This proves the selected callback
ABI bridge and finalizer transfer, not complete Darwin `libSystem` startup.

The companion `wintosh_guest_cxa_atexit_e2e` fixture independently proves the
three-argument `__cxa_atexit` registration path and guest callback ABI. Both
fixtures are original synthetic source; no Apple binary or SDK is bundled.

The source-level fixture ladder is now present under
`wintosh-windows/tests/fixtures/fixture-ladder/`: `argv-env`, `memory`,
`qsort-callback`, `file-io`, `pthread-tlv`, and `crash`. Its
`build-fixtures.ps1` check passed all six sources on 2026-10-12 and is
registered as CTest `wintosh_fixture_source_ladder` (test 64 in the refreshed
Clang build). This is a
provenance- and syntax gate; when an output directory is supplied it also
emits and validates five source-derived x86_64 Mach-O object files. These are
compile artifacts, not executable proof by themselves. The LLVM `ld64.lld`
path can additionally link these objects with unresolved Darwin exports left
for Wintosh binding. The `argv-env` fixture is now executed end-to-end by
`wintosh_argv_env_fixture_e2e`; on 2026-10-12 it printed `fixture argv-env` and
returned exit code 0 through `wintosh.exe`. The same ABI bridge now executes
the `memory`, `qsort-callback`, and `file-io` fixtures with exit code 0 through
their dedicated CTest E2E cases. The `crash` fixture now runs through the
runner's scoped Windows vectored-exception boundary, emits
`DARLING_GUEST_EXCEPTION=0xC0000005`, and returns the defined exit code 139;
its dedicated E2E test verifies this fail-closed behavior. The `memory` object
is additionally built with
`-fstack-protector-strong` and must reference Darwin `___stack_chk_guard`, which
is the first fixture-level check of the data-export bridge. The TLV source is
syntax-checked with the host Clang target because
the installed Darwin target rejects `__thread` during syntax-only checking.
The host-side pthread/TLV evidence remains green after the guest-ABI export
change: `wintosh_pthread_sync_smoke`, `wintosh_thread_smoke`, and
`wintosh_tlv_smoke` all passed on 2026-10-12. This does not upgrade the
compiler-generated `__thread` fixture to executable status.

The first Clang-only hello milestone is checked in as original source material
under `wintosh-windows/tests/fixtures/hello/`: `hello.c`, a minimal original
`libSystem.tbd` export manifest, and `build-hello.ps1`. The new
`wintosh_hello_e2e` CTest is registered only for Clang builds and the GitHub
Actions workflow has a Windows `clang-cl` job. On 2026-10-12 a fresh local
Clang/Ninja build completed, and both the fixture script and
`ctest --test-dir build-windows-clang-final4 -R '^wintosh_hello_e2e$'
--output-on-failure` passed. The E2E observed `hello from guest`, exit 0 with
the program name only, and exit 7 with one extra argument. Public CI evidence
is still pending because no workflow run has been started from this worktree.

The release workflow now keeps the Clang path distinct from the MSVC matrix:
it builds and tests `clang-cl` on `windows-latest`, packages the Clang binary
and its source/docs/licenses into `Wintosh-clang-x64.zip`, attests that archive,
and uploads it to the same release alongside the MSVC archives. This workflow
change is committed locally; no remote release has been created from it yet.

The ABI bridge smoke now includes two explicit SysV aggregate cases: a
16-byte integer/floating-point pair and a 24-byte triple exercising the
large-aggregate return path.  The Clang build reports
`ABI_BRIDGE_AGGREGATE=PASS` alongside the existing variadic cases.  This is
evidence for these bridge signatures only; full Darwin aggregate conventions
across all sizes, variadic vector arguments, blocks, and native Objective-C
compiler output remain outside the verified scope.

## 2026-10-14 verification update

The native startup trampoline test directly validates `argc`, `argv[0]`,
`argv[1]), and `envp[0]` from the supplied Darwin initial stack;
`wintosh_startup_trampoline_smoke` passed 1/1. The loader's normal and lazy
dyld bind paths additionally reject targets outside the file-backed portion of
a segment, preventing invalid pointers into unbacked VM space. The focused
dyld/Mach-O regression passed 4/4, and the complete Clang/Ninja suite
subsequently passed **70/70** in 69.41 seconds.

The direct Mach-O DarwinStart `argv/envp` fixture remains intentionally
unclaimed. Two original hand-built variants exposed unresolved fixture-level
bind/call-layout issues and were removed rather than counted as evidence. The
repository therefore proves the startup trampoline ABI separately, while the
full loader-to-DarwinStart `argv/envp` transfer still requires a clean
fixture with independently laid-out text, bind, and metadata regions.

The synthetic `__mod_init_func` fixture now also calls Darwin `getenv`
inside the initializer and observes the launch-owned
`WINTOSH_INIT_ENV=ready` vector. `wintosh_dyld_smoke` passed 1/1 after this
ordering assertion; it remains synthetic evidence, not complete libSystem
initialization.

The loader now also asserts that the owned `argv`, `envp`, and Apple
parameter vectors are null-terminated before either compatible-main or
DarwinStart transfer. The startup trampoline, hello E2E, and DarwinStart E2E
regression passed 3/3 after this change.

The loader-to-DarwinStart stack proof is now also end-to-end: the original
fixture `wintosh_darwin_start_argv_env_e2e` checks `argc == 1),
`argv[1] == nullptr), and `envp[0] != nullptr) inside the mapped Mach-O
entry before calling the bound `_exit). It passed in the complete Clang
suite, which now contains **71/71** passing tests in 96.47 seconds.

## Host-dependent socket evidence

The current recovery host permits loopback bind but denies loopback connect
with Windows `WSAEACCES`/errno 13. An independent native .NET
`TcpListener`/`TcpClient` probe reproduces the same bind-success/connect-denied
behavior, so the failing `socket_smoke` and socketpair portion of
`syscalls_smoke` are not presently evidence of a Wintosh-only socket bug.
They remain explicitly unclaimed until the suite is rerun on a host that
permits local TCP connections; no network test is silently converted into a
pass or omitted from the gate inventory.

## Broker pipe lifecycle note (2026-10-14)

The earlier 69/70 run exposed an invalid fixture assumption: the cross-client
test attempted to receive from a port owned by the first session, so the
broker correctly rejected the request before it could become cancellable. The
fixture now allocates a session-unbound compatibility port for that scenario
and destroys it afterward. The broker test passes in isolation, and the
complete Clang/Ninja suite passes **70/70** in 97.08 seconds.
