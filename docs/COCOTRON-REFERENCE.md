# Cocotron framework reference

The repository is stored in `cocotron-source/` at commit
`9cda7d719428a8ff940b134e391e511104ee0b5d`.

## Technical relevance

Cocotron contains broad historical Objective-C framework trees, including
Foundation, AppKit, CoreFoundation, CoreGraphics, CoreText, IOKit, OpenGL,
Security and related framework projects. It is therefore a useful reference
for API-family inventory, framework layering and Objective-C surface coverage
while planning the missing Darling framework layer.

It is not a drop-in native-Windows backend: the checkout is structured around
older Apple/Xcode project assumptions and its implementations are not proof of
current Windows behavior. No Cocotron implementation is copied into the
Darling Windows adapter in this batch; new Windows code must preserve the
Darling ABI and be independently tested on x64 and Win32.

## License and provenance

The checked-in `LICENSE.txt` grants permissive MIT-style terms and lists many
copyright holders, including third-party-origin components. The file is kept
with the source checkout and recorded here, but the broad license does not
automatically clear every nested asset or dependency. Component-level review
is required before any source reuse.

## Porting consequence

The next framework milestone should be a minimal Foundation-compatible object
and string layer backed by the existing Windows Objective-C runtime, not a
blind import of Cocotron's full historical framework tree. Until that layer
has Windows implementation and x64/Win32 runtime evidence, the framework row
in `PRIMITIVE-MATRIX.csv` remains `missing`.
