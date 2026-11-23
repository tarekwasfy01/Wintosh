# Smoke tests

Each `*_smoke.cpp` file is a focused runtime gate for one Wintosh family. A
passing smoke test proves only the covered adapter behavior; it does not prove
full Darling compatibility or arbitrary macOS application execution.

`arm64_import_e2e.ps1` builds two ARM64 Mach-O guests with Clang/ld64.lld and
runs them through the optional Dynarmic backend. It covers `_strlen` and
integer/double Objective-C class method arguments and returns dispatched by
`objc_msgSend`; it is not a general Objective-C or macOS application test.
