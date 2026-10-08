# Contributing to Wintosh

Wintosh is developed in the open as an experimental native Windows
compatibility project. Contributions should be small, reviewable, and tied to
one family in `../PRIMITIVE-MATRIX.csv`.

## Maintainer and contributor identity

The project maintainer and primary GitHub contributor is
[@tarekwasfy01](https://github.com/tarekwasfy01), Tarek Wasfy.

Please preserve existing copyright and license notices. New code must state
whether it is Wintosh-original, derived from Darling, or based on another
third-party project. Do not copy upstream code without recording its exact
source, license, and required notices.

## Pull requests

For each implementation change:

1. Keep the code in the appropriate `wintosh-windows/` family directory.
2. Add or update the corresponding matrix row and documentation.
3. Add a focused smoke test under `wintosh-windows/tests/` where practical.
4. Build x64 and Win32 when the toolchain is available.
5. Report the exact test output and remaining semantic limitations.

The project distinguishes source presence, compilation, smoke execution, and
real application compatibility. Do not describe one as another.
