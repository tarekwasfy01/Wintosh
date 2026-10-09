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
