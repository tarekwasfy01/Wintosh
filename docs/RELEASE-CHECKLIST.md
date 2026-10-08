# GitHub release checklist

Target repository: [tarekwasfy01/Wintosh](https://github.com/tarekwasfy01/Wintosh)

## Before the first commit

- Review `licenses/SOURCE-LICENSE-INVENTORY.csv` and resolve every
  `review-required` entry that will be redistributed.
- Keep Darling's GPL-3.0 license, corresponding source, Debian copyright data,
  and component notices together with the port.
- Confirm every newly added file has a clear provenance and license boundary.
- Run the x64 and Win32 smoke families and record their output.
- Re-check that ignored build output is not being added accidentally.
- Inspect files larger than GitHub's normal upload limit; use Git LFS only for
  intentionally tracked binary assets, not for source or license files.

## Current release boundary

The current tree is a source snapshot and engineering baseline, not a complete
Darling replacement and not a claim that arbitrary macOS applications run on
Windows. The local x64 Release configuration has a complete **38/38 CTest**
smoke result and a regenerated `Wintosh-0.1.1-AMD64.zip`; the GitHub workflow
now runs the complete CTest suite per architecture. A clean, reproducible
Win32 workflow run and the actual GitHub-hosted attestation are still release
evidence to collect, not claims established by this local checkout.

The workflow uploads attested archives but does not silently create or publish
a GitHub Release from its tag input. Creating the release and attaching assets
remains an explicit repository-owner action.

## Suggested first commit groups

1. Documentation, license bundle, matrices, and notices.
2. Windows-native source and smoke tests.
3. Corresponding upstream/reference source trees, after license review.
4. Build instructions and reproducible test evidence.
