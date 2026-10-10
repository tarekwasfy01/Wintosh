# GitHub Actions build and attestation

The workflow at `.github/workflows/wintosh-release.yml` builds the Wintosh
implementation on Windows runners for x64 and Win32. Each matrix job:

1. Configures the standalone `wintosh-windows/CMakeLists.txt` project.
2. Builds all Wintosh targets.
3. Runs the complete CTest smoke suite.
4. Packages the main `bin/wintosh.exe` CLI and `bin/wintosh_broker.exe` under
   `bin/`, with diagnostic smoke executables separated under `tests/`, together
   with `wintosh-windows/`, `docs/`, `licenses/`, and the Wintosh license.
5. Creates a GitHub artifact and a build-provenance attestation.
6. Publishes the archive as a workflow artifact and, after both architecture
   jobs pass, attaches it to the requested GitHub Release.

The workflow activates the runner's Microsoft C++ toolchain and uses the
portable Ninja generator, avoiding assumptions about the installed Visual
Studio generator version. It uses GitHub's official
`actions/attest-build-provenance@v2` action.
The release job uses `contents: write` only to create/update the requested
Release and upload the two verified archives.
The repository workflow permissions include `id-token: write` and
`attestations: write`, which are required for the attestation. The resulting
attestation is provenance evidence for the archive and build workflow; it is
not a claim of complete Darling compatibility.

## Running it for the current preview

From the repository's Actions page, start **Wintosh Windows build and
attestation** with:

`release_tag = v0.2.0`

The workflow can also run automatically for future `v*` tags. Both x64 and
Win32 jobs must pass before treating the release archives as verified. Download
the release job attaches both archives automatically; the attestations remain
associated with the generated artifacts.
