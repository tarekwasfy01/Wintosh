# Smoke tests

Each `*_smoke.cpp` file is a focused runtime gate for one Wintosh family. A
passing smoke test proves only the covered adapter behavior; it does not prove
full Darling compatibility or arbitrary macOS application execution.

The Clang-only `wintosh_corefoundation_runloop_e2e` target compiles and links
the original `fixtures/corefoundation-runloop/runloop.c` as an x86_64 Mach-O
and runs it through `wintosh.exe`. The guest registers repeating and one-shot
CoreFoundation observers; their callback functions execute from the loaded
guest image. The fixture checks four activity callbacks and one-shot
invalidation, exact repeating-observer context forwarding, and a non-null
one-shot context. It also creates, registers, and fires a one-shot
`CFRunLoopTimer` callback from the guest image, checks its tolerance APIs and
nominal fire date, then verifies a repeating timer fires more than once. It
then registers a version-0 source, signals it twice, and confirms one guest
perform callback with the original guest context. It also checks the
`kCFRunLoopRunHandledSource` result with `returnAfterSource` enabled, plus a
source order query. A second source checks its context retain/release callbacks
and `GetContext` pointer round trip. The host CoreFoundation smoke covers native
schedule/cancel callbacks on removal and invalidation; guest callback execution
remains unverified. Arbitrary framework startup and AppKit events remain
uncovered.

`arm64_import_e2e.ps1` builds two ARM64 Mach-O guests with Clang/ld64.lld and
runs them through the optional Dynarmic backend. It covers `_strlen` and
guest SVC calls for `fcntl` descriptor flag read/write, descriptor duplication,
`getrlimit`/`setrlimit` structure copies, invalid-pointer `EFAULT`, unsupported
`ioctl` `ENOTTY`, and Darwin's syscall carry flag. It also covers
Objective-C integer/double arguments and returns, object identity, pointer
dereference, a stack-passed seventh integer, two-/four-double HFA returns, and
a 24-byte indirect structure return through `objc_msgSend`. A guest method also
performs nested `sel_registerName`, `objc_msgSend`, `_strlen`, `_malloc`, and
`_realloc`/`_free` imports, verifying guest heap write/read/resize/free. It is
not a general Objective-C or macOS application test.

The Objective-C guest also compiles and invokes a global Clang block, copies
and invokes a scalar-capture stack block, copies an object-capture block, and
copies/invokes a `__block` scalar capture. These exercise `NSConcrete` binding,
guest-address registration for copied blocks, and nested guest copy/dispose and
Byref callbacks. They do not establish full libclosure behavior, weak captures,
arbitrary block signatures, or asynchronous block lifetime.

The same guest calls imported `_mmap`, `_mprotect`, and `_munmap`: it creates a
three-page anonymous private mapping, writes and reads it, changes it to
read-only, unmaps the middle page, verifies that the bridge rejects a repeated
unmap of the hole, then unmaps the left and right pages. The first and last
releases use raw Darwin `svc #0x80` syscall 73; the middle release and repeated
unmap use the imported `_munmap` bridge, checking that both paths share the
same ownership intervals. It remaps the middle hole at the same address with
raw Darwin `SYS_mmap` and frees it through imported `_munmap`. While the
remaining mapping is `PROT_NONE`, raw Darwin
`svc #0x80` syscall 197 replaces its middle page with `MAP_FIXED`; the imported
`_mmap` bridge then replaces the first page. The fixture writes through both
replacements and restores protection across the full mapping. This covers
partial anonymous mappings owned by the ARM64 guest bridge. The file fixture
also maps a page-offset `MAP_SHARED` view with page-granular `MAP_FIXED` into a
free address after releasing its seed view, then verifies the mapped byte and
unmaps it. This exercises the placeholder path around Windows' allocation
granularity. On older fallback paths file-backed views still require a
whole-view unmap. The runtime currently also routes `MAP_FIXED` over a
subrange of one tracked placeholder-backed file view through partial unmap and
placeholder replacement. The host smoke replaces the middle page of a
three-page view with a different file offset and checks both retained neighbors;
the ARM64 guest fixture repeats the same replacement through its Darwin mmap
bridge. Both paths also replace that file-backed page with zero-filled anonymous
memory and then unmap it. The reverse anonymous-to-file replacement is covered
for a complete tracked anonymous reservation in both the host smoke and ARM64
guest fixture. Partial anonymous-to-file, cross-allocation, and image
replacements remain unsupported.

The guest fixture also creates a file with `open`, writes seed bytes, maps it
with `MAP_SHARED`, changes bytes on both sides of a removed middle page, and
confirms the retained intervals and persisted file bytes. It then maps the
three-page file with `MAP_PRIVATE`, changes retained pages, removes the middle,
and confirms the private copy-on-write bytes survive while the file remains
unchanged. Descriptor inheritance from the runner is not covered.

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
the image closes. That client dylib loads a provider through `@rpath` using a
bare `@loader_path` runpath. The provider loads a third dylib through the
inherited runpath without declaring one itself; the fixture checks leaf-to-root
initialization and reverse termination order. It opens the client twice,
checks single initialization and shared handle identity, then checks that the
image remains callable after the first `dlclose` and unregisters its Objective-C
class after the final close.
Both guest opens use `@rpath`; the executable carries an `@executable_path`
runpath, covering runtime `LC_RPATH` lookup at the `dlopen` boundary.
While one provider method runs, it opens a plugin in a second subdirectory
using `@rpath`, resolves and calls its export, and closes it. The executable
declares the runpath and the provider declares none, so this checks inherited
runpath context at the runtime `dlopen` boundary.
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
