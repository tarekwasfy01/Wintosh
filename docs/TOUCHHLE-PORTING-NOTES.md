# touchHLE as a porting reference

The repository is stored in `touchhle-source/` at commit
`d34530b52174e8a4f1fe43178cc4fb0c99506f9f`.

## Technical relevance

touchHLE uses high-level emulation: it replaces selected iOS frameworks with
its own implementations instead of booting iOS. Its README explicitly
describes Foundation, UIKit, OpenGL ES and OpenAL as framework surfaces and
states that completeness is driven by supported applications. This is useful
for the Darling-for-Windows roadmap as a reference for application-driven
framework shims, API-family prioritization, and Mach-O/iOS ABI boundary tests.

It is not a drop-in Darling implementation and does not provide macOS kernel,
launchd, dyld, XNU Mach IPC, or general macOS framework compatibility. Its
emulated CPU and iOS-specific framework model must not be copied into the
Windows Darwin adapter as if it were a macOS ABI implementation.

## License and provenance boundary

The upstream README states that touchHLE's own source is MPL-2.0, while its
binaries are GPL-3-or-later due to dependency compatibility. It also points
to dependency-specific licenses and the runtime `--copyright` report. The
checkout is therefore catalogued as reference material; no touchHLE source is
copied into the Windows adapter in this batch. Dependency and bundled asset
licenses remain component-specific and are not cleared by the MPL-2.0 label.

## Actionable mapping

1. Use touchHLE's framework/API coverage approach to prioritize a minimal
   Foundation-style compatibility layer after the current ABI families.
2. Reuse only independently implemented Windows code and preserve Darling's
   GPL boundary; compare behavior and tests, not source text.
3. Add framework rows to `PRIMITIVE-MATRIX.csv` only when a Windows adapter and
   x64/Win32 runtime evidence exist. The current framework row remains
   `missing`.
