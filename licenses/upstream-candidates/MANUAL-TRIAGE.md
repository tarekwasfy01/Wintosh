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
