# Darwin_Computa comparison and extracted porting data

The complete repository is stored in `darwin-computa-source/` at commit
`ff9b96934a2577e158a8c6f516534d6425fc8a69`.

## What it actually does

Darwin_Computa is not a native Windows rewrite of Darling. It is a fork of
Boxedwine64 that emulates an x86/x86-64 Linux userspace kernel and runs the
real Darling userland inside that guest. The repository explicitly separates
the two layers with `BOXEDWINE_DARWIN` and keeps the upstream Wine path gated
and independent.

The important architecture is:

`Mach-O -> mldr -> dyld -> libSystem/frameworks -> darlingserver -> emulated Linux kernel -> host`

Modern Darling's `darlingserver` is treated as the user-space Darwin kernel.
The guest communicates with it through AF_UNIX, `SCM_RIGHTS`, RPC, kqueue/Mach
channels and shared memory. This is materially different from implementing a
small host-side Mach C ABI: the real Darling binaries and real Darling/XNU
user-space server remain intact.

## Data extracted for this Windows port

### Highest-value architectural findings

1. Prefer running the real Darling Linux userland under a Linux compatibility
   substrate when the goal is maximum Darling compatibility.
2. The current Darling path uses `darlingserver`; the historical `/dev/mach`
   kernel-module path is fallback/reference material, not the main modern path.
3. The Linux substrate must cover AF_UNIX, `SCM_RIGHTS`, epoll, eventfd,
   timerfd, socketpair, clone/futex, fork/exec/wait, process VM read/write,
   pidfd, procfs maps/fd, ptrace, `/dev/shm`, kqueue-like channels and commpage
   behavior.
4. A trace-driven loop is used: run a real binary, implement the next missing
   opcode/syscall/trap, add a regression, and rerun the complete self-test.
5. The first useful milestone is headless CLI execution; GUI and WASM are
   later layers.

### Directly reusable process for this workspace

- Keep native Windows primitive adapters for applications that need direct
  Windows hosting.
- In parallel, add an optional WSL/boxed Linux backend that can run the real
  Darling rootfs and `darlingserver` unchanged.
- Treat the current native Mach ABI family as compatibility scaffolding, not
  as proof of full modern Darling compatibility.
- Add a backend matrix column distinguishing `native-win32` from
  `emulated-linux/WSL`.
- Use `mldr` and `darlingserver` traces as the authoritative missing-syscall
  list instead of guessing from the complete XNU surface.

### Reported upstream milestones (not independently verified here)

The checked-in README reports real CLI examples such as `sw_vers`, `uname`,
`arch`, `hostname` and `date`, plus a `234/234` Boxedwine self-test and a
`12/12` Darwin self-test. These are upstream claims and must not be presented
as local Windows runtime evidence until reproduced in this workspace.

The README also identifies the current frontier: passing arguments to apps,
launchd service bootstrap, multiple applications, interactive shells and
windowed GUI applications.

## License and provenance boundary

`darwin-computa-source/license.txt` is GPLv2. The README identifies the tree
as a Boxedwine/Boxedwine64 fork and says it runs Darling's actual binaries
without reimplementing macOS functionality. The repository therefore carries
multiple upstream boundaries: Darwin_Computa changes, Boxedwine code, Darling
userland, and the rootfs/dependency payloads. No Darwin_Computa source or
binary payload is copied into the native Windows adapter in this batch.

Any future reuse must retain the GPLv2 text, preserve Boxedwine and Darling
attribution, and inventory the actual rootfs/dependency licenses separately.
