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
Its guest `+initialize` methods record base-class then subclass order, and the
fixture reads the final value through `objc_msgSend` on x64 Windows/Dynarmic.
Its ARM64 startup fixture also checks one `__mod_init_func` constructor,
superclass-first class `+load`, and category `+load`, each exactly once.
On process return, it checks that guest `__cxa_atexit`, `atexit`, and
`__mod_term_func` callbacks execute through the ARM64 guest dispatcher in that
order and emit their markers. It also opens an ARM64 `.dylib` from guest code,
checks its constructor, resolves and calls an exported function, then closes it
and checks its DSO-scoped C++ exit callback and `__mod_term_func` before process
shutdown. The same dylib registers an Objective-C class and category, checks
class-before-category `+load`, and answers a method send through each before
the image closes. That client dylib also loads a provider through `@rpath`
using a bare `@loader_path` runpath; the fixture checks provider-before-client
initialization and reverse termination order.
It also compiles a real Objective-C category into `__objc_catlist` and calls
its class method through the guest Objective-C dispatch path. `objc_smoke`
registers category methods/protocols/properties under an image token and checks
that they are removed when that image is unregistered. It also checks that a
category override becomes active and that unloading restores the original
method implementation.
It also loads two overlapping category images, unloads the older image first,
and verifies that the newer override stays active before the base method is
restored after the second unload. It also checks shared protocol conformance
and replacement/restoration of a category property across the same unload
order.

`objc_smoke` registers synthetic Mach-O class records with the subclass before
the superclass and checks that the runtime still links the inheritance chain.
It also runs class `+load` callbacks superclass-first despite reversed class
list order, then confirms that category `+load` follows all class callbacks.
The lifecycle is one-shot per image token.
The same fixture verifies `+initialize` superclass order, same-thread reentrancy,
and one-time invocation through class-method dispatch.
