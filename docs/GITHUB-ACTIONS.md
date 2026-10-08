# GitHub Actions build and attestation

The workflow at `.github/workflows/wintosh-release.yml` builds the Wintosh
implementation on Windows runners for x64 and Win32. Each matrix job:

1. Configures the standalone `wintosh-windows/CMakeLists.txt` project.
2. Builds all Wintosh targets.
3. Runs the Foundation ABI smoke test.
4. Packages `wintosh-windows/`, `docs/`, `licenses/`, and the Wintosh license.
5. Creates a GitHub artifact and a build-provenance attestation.
6. Publishes the archive as a workflow artifact for manual attachment to a
   GitHub Release.

The workflow uses GitHub's official `actions/attest-build-provenance@v2` action.
It deliberately has `contents: read` only and does not modify Releases or
repository contents automatically.
The repository workflow permissions include `id-token: write` and
`attestations: write`, which are required for the attestation. The resulting
attestation is provenance evidence for the archive and build workflow; it is
not a claim of complete Darling compatibility.

## Running it for the current preview

From the repository's Actions page, start **Wintosh Windows build and
attestation** with:

`release_tag = v0.1.0-dev`

The workflow can also run automatically for future `v*` tags. Both x64 and
Win32 jobs must pass before treating the release archives as verified. Download
the two workflow artifacts and attach them manually to the intended Release;
the attestations remain associated with the generated artifacts.
