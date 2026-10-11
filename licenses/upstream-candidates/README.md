# Upstream license candidates

These files were fetched from the `upstream_origin` URLs recorded in
`../EXTERNAL-COMPONENT-PROVENANCE.csv` using the GitHub license API. They are
preserved as corresponding-source review material and are not yet treated as
final license approval. `CANDIDATE-INVENTORY.csv` records the exact download
URL and SHA-256 for each result.

`candidate-found` means that GitHub exposed a repository license file. It does
not by itself prove that the repository has no additional notices, vendored
components, dual licensing, or source-level exceptions. `not-found` means the
API and the fast raw `LICENSE` fallback did not expose a license file in that
run; those components remain `review-required`. The fetch script may be run
again as upstream repositories change; previously fetched files are retained
as historical candidates even when a later API run is incomplete.
