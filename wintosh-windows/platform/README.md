# Platform adapters

Win32-backed implementations for files, memory, sockets, synchronization,
threads, signals, time, syscalls, paths, terminal I/O and standard I/O.

`DarwinPaths::AbsolutePath` resolves relative paths with
`GetFullPathNameW`. `DarwinPaths::CanonicalPath` additionally asks Windows
for the normalized final path of an existing file or directory through a
handle, which is the preferred path for reparse-point and symlink-aware
resolution. Some sandboxed or protected directories reject the final-name
query even when the path can be opened; in that case the adapter explicitly
falls back to `AbsolutePath`. This fallback is recorded in the primitive
matrix and is not a claim of complete Darwin `realpath` or mount semantics.

The filesystem/syscall layer also covers Windows-backed `stat`/`lstat`,
nanosecond timestamp conversion, `utimensat`/`futimens`, permissions,
`pread`/`pwrite` and vector I/O, `fsync`/`fdatasync`, advisory locks,
descriptor duplication, symlink/readlink/realpath, hard links, directory
creation/removal, `getfsstat`, and directory enumeration. The smoke coverage
still does not establish Darwin ACLs, xattrs, resource forks, mount namespaces,
or every reparse-point type.

The `sysctl` subset now reports the actual host guest-ABI architecture instead
of treating every 64-bit Windows host as x86_64. `hw.cputype`,
`hw.cpusubtype`, logical CPU counts, physical core counts, and the basic
`CTL_KERN` OS identity selectors are available through numeric MIB queries.
The name-based interface also exposes `hw.byteorder`, `hw.pagesize`,
`hw.cachelinesize`, `hw.l1icachesize`, `hw.l1dcachesize`,
`hw.l2cachesize`, `hw.l3cachesize`, `hw.machine_arch`, and `kern.version`.
The `hw.optional.*` subset reports x86_64 SSE2/SSE3/AVX2/AVX-512F and ARM64
NEON, ARMv8.1 atomics, CRC32, and crypto capability flags when those names
match the compiled host architecture. These flags come from Windows processor
feature queries; the ARM AES/SHA keys share Windows' aggregate ARMv8 crypto
flag, so they do not claim independent per-instruction detection. Unknown or
other-architecture feature names remain unregistered.
The compatibility profile
defaults to macOS 14.0.0 / Darwin 23.0.0 / build 23A344; callers can override
the product version, kernel release, build version, and model identifier in
the guest environment (including the runner's `--env` option) with
`WINTOSH_MACOS_VERSION`, `WINTOSH_DARWIN_OSRELEASE`,
`WINTOSH_DARWIN_OSVERSION`, `WINTOSH_DARWIN_MODEL`, and
`WINTOSH_DARWIN_PAGESIZE` / `WINTOSH_DARWIN_CACHELINESIZE`. These values describe
the selected guest profile, not the Windows host. The CPU and OS key inventory
follows Apple's [XNU sysctl headers](https://github.com/apple-oss-distributions/xnu/blob/main/libkern/libkern/sysctl.h)
and [system capability guidance](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities).
Guest page-size queries share a 4 KiB x86_64 / 16 KiB ARM64 default; the
`WINTOSH_DARWIN_PAGESIZE` profile value can select either. `getpagesize`,
`sysconf`, `hw.pagesize`, Mach `host_page_size`, and mapping validation use that
value. Windows allocation granularity still constrains some fixed file maps.
`hw.cachelinesize` uses the largest Windows level-one data/unified cache-line
size, or the power-of-two value configured by `WINTOSH_DARWIN_CACHELINESIZE`.
Cache-size queries report the smallest matching Windows cache descriptor for
each requested level; performance-level-specific Apple topology is not modeled.
