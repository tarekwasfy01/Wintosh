# Smoke tests

Each `*_smoke.cpp` file is a focused runtime gate for one Wintosh family. A
passing smoke test proves only the covered adapter behavior; it does not prove
full Darling compatibility or arbitrary macOS application execution.

`arm64_import_e2e.ps1` builds two ARM64 Mach-O guests with Clang/ld64.lld and
runs them through the optional Dynarmic backend. It covers `_strlen` and
Objective-C integer/double arguments and returns, object identity, pointer
dereference, a stack-passed seventh integer, two-/four-double HFA returns, and
a 24-byte indirect structure return through `objc_msgSend`. A guest method also
performs nested `sel_registerName`, `objc_msgSend`, `_strlen`, `_malloc`, and
`_realloc`/`_free` imports, verifying guest heap write/read/resize/free. It is
not a general Objective-C or macOS application test.

The ARM64 Objective-C fixture also verifies class inheritance by invoking a
class method implemented on its guest superclass.
It also compiles a real Objective-C category into `__objc_catlist` and calls
its class method through the guest Objective-C dispatch path.

`objc_smoke` registers synthetic Mach-O class records with the subclass before
the superclass and checks that the runtime still links the inheritance chain.
