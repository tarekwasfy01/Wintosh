# Third-party notices

Wintosh contains or documents source from several independent projects. Their
licenses remain attached to their source trees and are catalogued in
`licenses/SOURCE-LICENSE-INVENTORY.csv`.

The most important boundary is:

- [Darling](https://github.com/darlinghq/darling) — GPL-3.0 and component
  licenses; corresponding source and notices must remain available.
- [Microsoft WSL](https://github.com/microsoft/WSL) — MIT source license plus
  the included third-party notices; used here primarily as reference material.
- PureDarwin, touchHLE, IPASim, Wine/Darwine, Cocotron, Darwin Computa,
  macho-ld, Classix, VST-ace, DirectHW, and SherlockElf — reference checkouts
  with project-specific license terms; inspect each original license before
  copying or redistributing code.
- [Dynarmic](https://github.com/yuzu-mirror/dynarmic) — optional AArch64
  backend, pinned at `9d4582339990d4eae53f1dc7160686920fc2075c`. Its 0BSD
  notice and the Boost, fmt, mcl, oaknut, robin-map, Xbyak, Zydis, and Zycore
  dependency notices are in `../licenses/dynarmic/`; see the provenance CSV
  for exact bundled tree IDs and the vcpkg baseline.

No source tree is automatically relicensed by the Wintosh-original MIT
notice. The inventory intentionally keeps unresolved or review-required
components visible.
