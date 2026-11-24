# Optional Dynarmic A64 backend build

The ARM64 guest backend on x64 Windows uses Dynarmic commit
`9d4582339990d4eae53f1dc7160686920fc2075c` from `yuzu-mirror/dynarmic`.
`vcpkg.json` pins the Boost dependency baseline used by the local build probe.
The build compiles Dynarmic's bundled fmt, mcl, oaknut, robin-map, Xbyak,
Zydis, and Zycore sources; their notices and Boost's notice are in
`../../licenses/dynarmic/`.

Run `tools/Build-Dynarmic.ps1` from a Visual Studio Developer PowerShell with
CMake, Ninja, Git, and the x64 MSVC compiler available. Standard mode also
requires vcpkg and network access. It checks out
the exact commit into `build-dynarmic/source`, changes `/Zi` to `/Z7` and
removes `/Zo` from the two compiled MSVC flag lists in Dynarmic and mcl, then
builds a Release A64 library in `build-dynarmic/build`. Existing source
checkouts are accepted only when `HEAD` matches the pinned commit. The script never rewrites
or deletes a mismatched checkout.

Pass the reported paths when configuring Wintosh:

```powershell
cmake -S wintosh-windows -B build-windows-clang `
  -DWINTOSH_DYNARMIC_SOURCE_DIR=build-dynarmic/source `
  -DWINTOSH_DYNARMIC_BUILD_DIR=build-dynarmic/build
```

For an offline build, pass `-Offline` and `-VcpkgInstalledDirectory` pointing
to an existing x64-windows vcpkg package tree containing Boost headers. This
mode skips manifest installation and is intended for local validation. The
standard manifest-mode install has not been verified in a network-enabled
environment.

The integration remains opt-in. This build creates a CPU backend; it does not
provide Darwin syscalls, Mach services, dyld parity, or Apple frameworks by
itself. No Dynarmic or dependency source is copied into the Wintosh tree.
