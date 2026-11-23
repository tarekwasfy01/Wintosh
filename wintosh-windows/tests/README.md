# Smoke tests

Each `*_smoke.cpp` file is a focused runtime gate for one Wintosh family. A
passing smoke test proves only the covered adapter behavior; it does not prove
full Darling compatibility or arbitrary macOS application execution.

`arm64_import_e2e.ps1` builds two ARM64 Mach-O guests with Clang/ld64.lld and
runs them through the optional Dynarmic backend. It covers `_strlen` and
Objective-C integer/double arguments and returns, object identity, pointer
dereference, and a stack-passed seventh integer through `objc_msgSend`; it is
not a general Objective-C or macOS application test.
