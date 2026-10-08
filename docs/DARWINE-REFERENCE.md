# Darwine reference

The historical Darwine download page was reviewed on 2026-10-08. It lists
preliminary Darwine 0.9.27 DP PowerPC/x86 binaries and a universal SDK for
Mac OS X Panther, and explicitly describes the SDK as an early-development
toolchain around Wine. The page also points developers to Wine, WineHelper and
qemu-darwin-user sources.

## Relevance to this port

Darwine is useful as historical evidence that a Darwin-facing Wine layer can
be organized around a host compatibility layer and a separate SDK/template.
It is not a current Windows implementation and its stated target is obsolete
Mac OS X, not native Windows. Its old package format and 2005-era assumptions
are not used as source code in this port.

The practical lesson for Darling-for-Windows is architectural only: keep the
Darwin ABI adapter, executable loader, and optional application SDK separate.
The current native Windows adapter remains independently implemented and
tested through the primitive matrix.

## Provenance and license boundary

The download page does not itself provide a complete component license
inventory; it refers to Wine and other source trees separately. Therefore no
Darwine binary or source is copied into the adapter, and no license clearance
is inferred from the page. Any future source retrieval must inventory Wine,
QEMU and helper components individually.
