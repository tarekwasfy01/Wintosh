# Darwin user-space emulator references

Checked 2026-11-15. These projects illustrate different layers; their claims
are not interchangeable.

| Project | Upstream scope | Transferable lesson | Wintosh status |
|---|---|---|---|
| [darling-lol](https://github.com/lucannez64/darling-lol) | Experimental Darling patches/tools for testing a macOS League client on Linux | GUI games need app-specific graphics, loader and runtime work even atop Linux | Reference only; no patch copied or game tested |
| [Darwin_Computa](https://github.com/andrewnakas/Darwin_Computa) | Darling userland on an emulated Linux kernel, described as portable to Windows/Linux/browser | Linux syscall ABI can itself be emulated; launchd and GUI claims require separate executable evidence | Local historical checkout is catalogued; host WSL2 cannot start here, so upstream claims are not reproduced |
| [XNU mirror](https://github.com/opensource-apple/xnu) | Darwin kernel source and ABI definitions | Authoritative reference for syscall numbers, Mach structures and kernel contracts | Read-only ABI oracle; kernel implementation is not copied wholesale |
| [Sogen](https://github.com/momo5502/sogen) | Userspace emulator for Windows and Linux binaries | Modular CPU, memory, syscall and device boundaries are useful architecture comparisons | Not a Darwin emulator; no direct macOS execution support claimed |

## Matrix interpretation

Let a macOS application require contract vector `r`. A proposed runtime route
has Boolean transition matrices `D` (Darwin contract to modeled Linux), `L`
(Linux to emulated/real WSL kernel), and `W` (backend to Windows services).
The route is viable only when every required contract has a measured path:

`r <= diag(D * L * W)`.

`darling-lol` is evidence that a narrow Linux game target may need custom
graphics and overlay work; it does not fill those Windows edges. Darwin_Computa
is closest to the requested route, but its README is a project statement, not
a local run. XNU supplies definitions against which each emulated contract
can be checked. Sogen supplies a design comparison for userspace emulation,
but models other operating systems. Therefore none of these references
changes a Wintosh capability from unknown to verified without a local fixture.

No external source was copied. For each future port, pin a commit, inventory
its license and dependencies, implement the matching contract, then attach a
fixture result to the exact matrix edge.
