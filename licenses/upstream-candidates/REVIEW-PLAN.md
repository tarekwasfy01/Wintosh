# Candidate review priority

The automated scan found 38 upstream candidate files. It detected 34
copyright/notice markers, 11 GPL markers, 8 LGPL markers, 4 BSD markers,
3 Apache markers, 2 MIT markers, and 1 MPL marker; counts overlap because a
single file may contain several licenses.

The first manual-review group is the 17 files with multiple license markers:
`compiler-rt-LICENSE.TXT`, `corecrypto-LICENSE`, `corefoundation-LICENSE`,
`darling-dmg-LICENSE`, `dbuskit-COPYING`, `fmdb-LICENSE.txt`,
`foundation-LICENSE`, `icu-LICENSE`, `installer-LICENSE`,
`JavaScriptCore-COPYING.LIB`, `keymgr-COPYING`, `libdispatch-LICENSE`,
`libnetwork-LICENSE`, `libplatform-LICENSE`, `mDNSResponder-LICENSE`,
`openjdk-LICENSE`, and `xcbuild-LICENSE`.

No candidate is considered approved until its source tree, nested notices,
copyright holders, and compatibility with the Wintosh license are checked.

Initial text-level triage found these useful leads, not approvals: compiler-rt
states an Illinois BSD-like/MIT dual license; FMDB states MIT; ICU states its
ICU permission terms; mDNSResponder describes Apache-2.0 and three-clause BSD
areas; and xcbuild contains a BSD grant. CoreCrypto, Darling-DMG, Installer,
Keymgr, and OpenJDK contain GPL text; DBusKit, Foundation, and JavaScriptCore
contain LGPL text. The exact source-file scope and nested notices still need
manual confirmation before any inventory status changes.
