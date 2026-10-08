# Wintosh documentation index

This file is the navigation map for the repository. The machine-readable
matrices and the provenance inventories are authoritative for their respective
data; prose documents explain how to interpret them.

## Start here

1. [README.md](README.md) — project purpose, current capabilities, build entry
   points, and a concise outlook.
2. [STATUS.md](STATUS.md) — one-page evidence-based status with explicit
   verified, partial, missing, and blocked boundaries.
3. [RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md) — checks required before a
   GitHub commit or redistribution.
4. [CURRENT-STATE.md](CURRENT-STATE.md) — confirmed contents of this complete
   workspace snapshot.

## Porting and implementation

- [PRIMITIVE-MATRIX.csv](PRIMITIVE-MATRIX.csv) — canonical machine-readable
  family matrix.
- [PRIMITIVE-MATRIX.md](PRIMITIVE-MATRIX.md) — matrix policy, interpretation,
  status summary, and test-gate rules.
- [PORTING-INVENTORY.md](PORTING-INVENTORY.md) — chronological engineering
  record and remaining gaps.
- [SOURCE-EXTRACTION-MATRIX.md](SOURCE-EXTRACTION-MATRIX.md) — what was
  learned from each checked-out project and what was not copied.
- [SOURCE-CHECKOUT-MANIFEST.md](SOURCE-CHECKOUT-MANIFEST.md) — every checked
  out reference tree and its license boundary.
- [BUILD-AND-TEST.md](BUILD-AND-TEST.md) — reproducible build and smoke-test
  commands with evidence boundaries.

## Reference-project analysis

- [DARLING-WSL-BACKEND.md](DARLING-WSL-BACKEND.md)
- [DARWIN-COMPUTA-ANALYSIS.md](DARWIN-COMPUTA-ANALYSIS.md)
- [IPASIM-PORTING-NOTES.md](IPASIM-PORTING-NOTES.md)
- [TOUCHHLE-PORTING-NOTES.md](TOUCHHLE-PORTING-NOTES.md)
- [ADDITIONAL-REFERENCES-ANALYSIS.md](ADDITIONAL-REFERENCES-ANALYSIS.md)
- [DARWINE-REFERENCE.md](DARWINE-REFERENCE.md)
- [DIRECTHW-REFERENCE.md](DIRECTHW-REFERENCE.md)
- [COCOTRON-REFERENCE.md](COCOTRON-REFERENCE.md)

## Legal and source provenance

- [licenses/README.md](licenses/README.md) — interpretation rules.
- [licenses/SOURCE-LICENSE-INVENTORY.csv](licenses/SOURCE-LICENSE-INVENTORY.csv)
  — recognized license files and unresolved entries.
- [licenses/EXTERNAL-COMPONENT-PROVENANCE.csv](licenses/EXTERNAL-COMPONENT-PROVENANCE.csv)
  — 150 Darling external components with checkout provenance.
- [licenses/EXTERNAL-COMPONENT-REVIEW.md](licenses/EXTERNAL-COMPONENT-REVIEW.md)
  — explanation of the 147 remaining component reviews.
- [licenses/SOURCE-BUNDLE.md](licenses/SOURCE-BUNDLE.md) — corresponding-source
  boundary and known NTFS checkout limitation.
- [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) — human-readable notice.
- [LICENSE-WINTOSH-ORIGINAL-MIT.txt](LICENSE-WINTOSH-ORIGINAL-MIT.txt) — only
  for clearly identified Wintosh-original code.

## Evidence convention

`present` means source or a license file was found; `implemented` means code
exists; `verified` requires a build and runtime smoke result on the stated
targets; `partial` means only a subset or local adapter is proven; `missing`
means no Windows implementation exists; `reference-only` means analysis has
been recorded without claiming copied or runnable implementation.
