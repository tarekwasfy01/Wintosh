# Manual text triage

This file records a text-level reading of selected fetched license files. It
is not a legal opinion and does not replace checking the corresponding source
tree, vendored code, notices, or exact commit.

| Candidate | Text-level result | Remaining check |
|---|---|---|
| `compiler-rt-LICENSE.TXT` | Explicit BSD-like/MIT dual-license choice | Confirm per-file exceptions and third-party runtime pieces |
| `fmdb-LICENSE.txt` | MIT grant and notice/attribution terms | Confirm all bundled dependencies and source scope |
| `icu-LICENSE` | ICU permission notice with attribution conditions | Check ICU third-party notices and bundled data |
| `mDNSResponder-LICENSE` | Apache-2.0 for majority and three-clause BSD areas | Map each source directory to the stated license and retain notices |
| `xcbuild-LICENSE` | BSD-style redistribution conditions | Check nested dependencies and binary attribution requirements |

These five files provide useful licensing evidence, but none is promoted to
`present` in the main provenance inventory until the remaining checks are
complete. GPL/LGPL/MPL and mixed-license candidates remain in the prioritized
manual-review group.

The 11 `APPLE_LICENSE` candidates all contain Apple Public Source License
2.0 text. This is a separate Apple license family, not automatically MIT,
BSD, GPL, or LGPL. Each component still requires source-file mapping,
additional Apple notices, and compatibility review before redistribution.
The inspected APSL text explicitly distinguishes unmodified and modified code,
requires preservation of Apple notices, and imposes source-availability and
modification-notice conditions for externally deployed modifications. Those
conditions must be tracked separately from the Wintosh project license.
