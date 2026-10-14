# Wintosh host-export inventory

This file records the native symbols currently resolved by the Wintosh host
adapter. It is an inventory of the implementation in
`wintosh-windows/platform/darling_windows_stdio.cpp`, not a claim of complete
Darwin `libSystem` compatibility. A symbol is listed only when its resolver
branch is present in the current source tree.

## Export families

| Family | Representative Darwin spellings | Current status | Evidence / boundary |
|---|---|---|---|
| Process and startup | `_exit`, `exit`, `_errno`, `___error`, `__error`, `___cxa_atexit`, `___cxa_finalize` | implemented / partial | `___error`/`__error` use a SysV bridge and thread-local storage; `host_api_smoke`, `atexit_smoke`; full `libSystem` startup remains open |
| Standard output | `_puts`, `puts`, `_printf`, `printf` | implemented / partial | `macho_printf_e2e`; variadic ABI is limited to the supported x86_64 bridge |
| Stack protection | `__stack_chk_fail`, `___stack_chk_fail`, `___stack_chk_guard` | implemented / partial | function aliases now use the runtime export table; host API and compiler-generated guard references are covered; exact Darwin guard initialization remains open |
| Dyld support | `dyld_stub_binder`, `_dyld_stub_binder`, `_wintosh_lazy_binding_session_active` | implemented / partial | lazy-binding and active-session smoke coverage; complete dyld semantics remain open |
| TLS and C++ lifetime | `_tlv_atexit`, `___cxa_thread_atexit`, `___cxa_thread_atexit_impl`, `_cxa_thread_atexit_impl`, `___cxa_atexit`, `___cxa_finalize` | implemented / partial | function aliases now use the runtime export table; `tlv_smoke`, `host_api_smoke`; guest `__thread` Mach-O E2E is still missing |
| Memory and allocation | `_malloc`, `_calloc`, `_realloc`, `_reallocarray`, `_reallocarrayf`, `_free`, `_memcpy`, `_memset`, `_qsort` | implemented / partial | aliases now use the memory export table; `_qsort` has a Darwin callback-ABI host test; allocator ownership and Darwin edge semantics remain bounded |
| File I/O | `_open`, `_read`, `_write`, `_close` and their normalized/`nocancel` aliases | implemented / partial | aliases now enter the file-I/O table; Darwin descriptor flags, cancellation ordering, and exact ABI layouts remain bounded |
| Objective-C runtime | `_objc_msgSend`, `_objc_getClass`, `_sel_registerName`, `_sel_getName`, `_objc_retain`, `_objc_release`, `_objc_storeStrong` | implemented / partial | Objective-C runtime smoke tests; full Apple ABI and exception semantics remain open |
| Blocks | `_Block_copy`, `_Block_release`, `_Block_object_assign`, `_Block_object_dispose` | implemented / partial | host adapter coverage; complete block ABI is not claimed |
| Signals | `_sigaction`, `_sigemptyset`, `_sigfillset`, `_sigaddset`, `_sigdelset`, `_sigismember`, `_sigprocmask`, `_pthread_sigmask`, `_sigpending`, `_sigwait`, `_sigtimedwait` | implemented / partial | `signals_smoke`; Darwin delivery and interruption ordering remain bounded |
| File descriptors | `_read`, `_write`, `_pread`, `_pwrite`, `_close`, `_fcntl`, `_dup`, `_dup2`, `_pipe`, `_poll` | implemented / partial | file and descriptor smoke families; exact Darwin struct and flag layouts remain bounded |
| Sockets | `_socket`, `_socketpair`, `_bind`, `_connect`, `_listen`, `_accept`, `_getsockname`, `_getpeername`, `_getaddrinfo`, `_freeaddrinfo` | implemented / partial | socket smoke coverage; full Darwin networking edge semantics remain open |

## Data exports

The resolver also exposes data-address symbols, including
`___stack_chk_guard`, `___stdinp`, `___stdoutp`, and `___stderrp`. Data exports must be treated differently from function
exports: callers may dereference the returned address, so replacing them with
an executable trampoline is not valid. The current resolver returns the
address of each stream-pointer storage and `host_api_smoke` checks both Darwin
spellings for all three stdio streams. Other stdio data symbols remain an
explicit inventory item for later export-table batches.

## Source-of-truth rule

The implementation source remains authoritative. When a family is converted
from the current conditional resolver to a typed export table, the conversion
must preserve all aliases, distinguish function/data exports, and add at least
two ABI-sensitive tests before the old branch is removed. The relevant source
and license provenance is documented in the repository's existing Darling and
Wintosh notices; no Apple SDK or Apple binary is redistributed by this table.

`host_api_smoke` now checks that the migrated aliases resolve to identical
addresses for `_puts`/`puts`, `_printf`/`printf`, `_exit`/`exit`, `_getenv`/`getenv`, the dyld
stub-binder pair, and the C++ exit/finalize pairs. It also continues to check
that the stack-guard data address is present separately from the function
exports.
