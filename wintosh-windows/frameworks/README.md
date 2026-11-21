# Framework ABI

Foundation-compatible ABI primitives are implemented incrementally and remain
independent of the full Apple Foundation/CoreFoundation object model. The
current CoreFoundation subset includes typed values, collections, strings,
data, URLs, a run-loop/notification subset, XML and limited binary property
lists, and CFBundle main/create plus bundle, executable, resources-directory,
and resource URL lookup, executable architecture queries, cached global Info.plist dictionary access, key lookup,
bundle identifier access, and localized resource/`.strings` lookup using the
Windows UI language preference with development-region and Base fallbacks.
Info-dictionary
key lookup also checks string values in `InfoPlist.strings`. CFBundle executable
status, preflight, load/unload, and code/data symbol lookup now delegate to
Wintoshs Mach-O dynamic loader. Bundle lookup covers app `Contents` paths and
versioned framework `Versions/Current` or concrete version directories for the
executable, Info.plist, and resource roots. Standard plug-in, private/shared
framework, and shared-support URLs are exposed, as is bounded discovery of
immediate child bundles by optional filename extension. Weakly canonicalized
bundle paths (with an absolute lexical fallback) are uniqued in a process-
lifetime catalog exposed by
`CFBundleGetBundleWithIdentifier` and `CFBundleGetAllBundles`; explicit unload
controls loaded code because catalog entries remain alive. Discovery inspects
at most 65,536 entries and returns at most 4,096 child bundles. The surface
remains partial:
when a transfer omits the `Current` symlink, Wintosh uses the greatest version
directory name as a deterministic fallback, which cannot prove that version
was the source system's selected version.
preflight performs a side-effect-free Mach-O structure and host-architecture
check but does not populate `CFError` or prove all dependencies can link. `CFBundleCopyExecutableArchitectures`
and `CFBundleCopyExecutableArchitecturesForURL` return retained CFArray values
of CFNumbers containing raw Mach-O CPU type IDs from thin, fat32, or fat64
images. The URL form accepts either a bundle directory or a standalone
executable file; missing executables, malformed Mach-O data, and unsupported
URLs return `NULL`. These APIs mirror Apple's
[bundle](https://developer.apple.com/documentation/corefoundation/cfbundlecopyexecutablearchitectures%28_%3A%29)
and [URL](https://developer.apple.com/documentation/corefoundation/cfbundlecopyexecutablearchitecturesforurl%28_%3A%29)
architecture queries. The
localized info dictionary and complete plist type coverage are absent,
framework resource localization and plug-in discovery are incomplete.
The Objective-C host now provides a selected `NSBundle` bridge over this
CFBundle layer. These new loader exports
still need a dedicated guest fixture. The versioned path rules follow Apple's
[bundle layout guide](https://developer.apple.com/documentation/bundleresources/placing-content-in-a-bundle);
Apple describes CFBundle preflight as checking whether an executable appears
loadable, with dependency-link failures potentially deferred to load
([CFBundlePreflightExecutable](https://developer.apple.com/documentation/corefoundation/cfbundlepreflightexecutable%28_%3A_%3A%29?language=objc)).
Apple also warns Cocoa apps to use NSBundle for code loading because CFBundle
does not natively integrate with the Objective-C runtime
([CFBundle and NSBundle](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/LoadingCode/Concepts/CFNSBundle.html)).
The directory API follows Apple's immediate-directory bundle discovery model
and optional filename-extension filter
([CFBundleCreateBundlesFromDirectory](https://developer.apple.com/documentation/corefoundation/cfbundlecreatebundlesfromdirectory%28_%3A_%3A_%3A%29)).

### AppKit entry-point bootstrap

The Windows host resolves `NSApplicationMain`, reads `NSPrincipalClass` from
the main bundle's `Info.plist` when present (falling back to `NSApplication`),
and creates that class through `sharedApplication`. A partial classic NIB path
reads `NSMainNibFile`, resolves a flat NIB file or package `keyedobjects.nib`,
and decodes selected `NSIBObjectData` records before the application event
loop. `+[NSBundle loadNibNamed:owner:]` also loads a named NIB from the main
bundle with an explicit File's Owner; the instance method with
`topLevelObjects:` returns objects inferred from the classic archive's OID
and parent maps. `NSNib` supports `initWithNibNamed:bundle:`,
`initWithNibData:bundle:`, and `instantiateWithOwner:topLevelObjects:` through
the same decoder. The named initializer preloads NIB data from the bundle; the
data initializer retains caller-supplied archive data. For the
modern APIs, the returned array retains the whole decoded graph and releases
it when the array dies; Cocoa normally relies on strong references from
top-level objects, so this still retains too much. Legacy and main-nib loads
keep decoded graphs for the process lifetime. Storyboards and full NIB
compatibility are not implemented. It registers
minimal `NSObject`,
`NSResponder`,
`NSString`, `NSConstantString`, `NSMutableString`, `NSArray`, `NSMutableArray`,
`NSDictionary`, `NSMutableDictionary`, `NSSet`, `NSMutableSet`, `NSData`,
`NSMutableData`, `NSNumber`, `NSDate`, `NSNull`, `NSBundle`,
`NSAutoreleasePool`, `NSProcessInfo`, `NSApplication`, `NSMenu`, `NSMenuItem`,
`NSAlert`, `NSURL`, `NSSavePanel`, `NSOpenPanel`, `NSError`, `NSFileManager`,
`NSFileHandle`,
`NSWindow`,
`NSImage`, `NSImageRep`, `NSBitmapImageRep`, `NSImageView`, `NSView`,
`NSControl`, `NSButton`, `NSPopUpButton`,
`NSTextField`, `NSTextView`, `NSProgressIndicator`, `NSSlider`, `NSStepper`,
`NSScrollView`, and `NSClipView` runtime classes, exposes their class symbols and
`NSApp`, and provides once-only launch notifications/delegate callbacks,
`finishLaunching`, `activateIgnoringOtherApps:`, `isActive`, `windows`,
`keyWindow`, and `mainWindow`. `NSWindow` tracks and exposes key/main state.
`hide:`, `unhide:`, `unhideWithoutActivation`, and `isHidden` manage visible
application windows.
Hide and unhide now post their matching will/did notifications and invoke the
optional application delegate callbacks. `unhide:` restores windows without
activation first, then requests foreground activation; the asynchronous Win32
activation message drives the separate active-state notifications.
The will/did order follows Apple's [`hide:`](https://developer.apple.com/documentation/appkit/nsapplication/hide%28_%3A%29)
and [`unhideWithoutActivation`](https://developer.apple.com/documentation/appkit/nsapplication/unhidewithoutactivation%28%29) contracts.
`NSWindow` delegates can veto closing with `windowShouldClose:` and receive
resize, close, and key/main transition notifications and callbacks.
Key/main transitions update the tracked `keyWindow`/`mainWindow` state before
callbacks and are emitted only when that state changes, so losing-window
`WM_ACTIVATE` and next-window `WM_ACTIVATE` cannot report the same resignation
twice.
Window minimum/maximum sizes constrain native resizing; `center`,
`miniaturize:`, `deminiaturize:`, and `zoom:` map to Win32 operations.
`NSApplication.mainMenu` binds `NSMenu`/`NSMenuItem` models to native Win32 menu
bars. `sendAction:to:from:` sends to an explicit target or follows the key
window's responder chain and delegate, then the distinct main window's chain
and delegate, application, and application delegate. `targetForAction:` and
`targetForAction:to:from:` expose explicit-target validation and key-window
responder lookup. `NSResponder.tryToPerform:with:` searches from the receiver
up its responder chain and then checks a window or application delegate.
Native control and menu actions use the same action dispatch.
It reports whether an action target was found; selector invocations use the
existing guest Objective-C ABI bridge. The nil-target search order follows
Apple's [`sendAction:to:from:` contract](https://developer.apple.com/documentation/appkit/nsapplication/sendaction%28_%3Ato%3Afrom%3A%29?language=objc),
and responder forwarding follows Apple's
[`tryToPerform:with:` contract](https://developer.apple.com/documentation/appkit/nsresponder/trytoperform%28_%3Awith%3A%29).
Menu titles, nested submenus,
separators, enabled/check states, and target/action dispatch are supported.
Key equivalents use the active Windows
keyboard layout, including Unicode characters and selected Cocoa special-key
codes. Cocoa Command/Control map to Windows Ctrl, Option to Alt, and Shift to Shift.
`autoenablesItems` invokes `validateMenuItem:` as native menus open. Contextual
popups from `popUpContextMenu:withEvent:forView:` display at the current pointer
and dispatch the selected action. `NSView.menu`/`setMenu:` bind context menus;
right-clicks on native controls and hit-tested views show them at the event
coordinates. Keyboard invocation uses the current pointer. Nonblocking
tracking, grapheme/multi-character shortcuts, exact Command-vs-Control
modifier distinctions, and full Cocoa validation timing remain outside this subset.
`NSAlert` supports `init`, message and informative text, alert style, up to
three titled buttons, and blocking `runModal`. It maps the alert to a native
Windows Task Dialog and returns AppKit's first/second/third button response;
`MessageBoxW` provides a one-button fallback. Sheet presentation, suppression
controls, accessory views, and completion-handler callbacks are not implemented.
`NSURL` supports file URL construction, `path`, `absoluteString`, `isFileURL`,
path appending/removal, `lastPathComponent`, `pathExtension`, and
`standardizedFileURL`. `NSOpenPanel` uses the native Windows open-file dialog and supports
single or multiple file selection; `NSSavePanel` uses the Windows save-file
dialog. Both expose `URL`, `URLs`, and the legacy filename accessors. Panel
titles and the save panel's suggested name are passed through. A panel set to
directory-only selection uses the Windows folder browser; mixed file/folder
selection is unavailable. `allowedFileTypes` maps extension strings to a
Windows file filter. `allowedContentTypes` accepts Objective-C `UTType`
instances and uses their preferred extension; the virtual
`UniformTypeIdentifiers` provider implements `typeWithIdentifier:`,
`typeWithFilenameExtension:`, `identifier`, and `preferredFilenameExtension`
for a finite set of common types. It does not read custom UTI declarations from
`Info.plist`, implement the complete conformance graph, or provide Swift's
`UTType` value-type ABI and static type constants. Multiple directory
selection, security-scoped access, modal sheets, and async completion handlers
are not implemented.
`NSFileManager.defaultManager` provides path existence and directory checks,
directory listing, file reads, directory creation, file creation, removal,
copying, and moving through the Windows filesystem. It maps user Documents,
Desktop, Downloads, Movies, Music, Pictures, Caches, and Application Support
directories to Windows shell folders; `URLsForDirectory:inDomains:` and
`URLForDirectory:inDomain:appropriateForURL:create:error:` return file URLs.
URL-based directory enumeration, directory creation, removal, copying, and
moving are also mapped to the same host operations; resource-key requests and
enumeration options are currently ignored.
`attributesOfItemAtPath:error:`, its URL variant, and
`attributesOfFileSystemForPath:error:` return type, size, modification and
creation dates, approximate permissions, and volume capacity/free space.
Failures populate a basic `NSError` with `NSCocoaErrorDomain`, the host error
code, and a localized description. Coordinated access, exact POSIX ownership
and permissions, and the full Cocoa error taxonomy are not implemented.
`NSFileHandle` opens existing files for reading, writing, or updating, and can
wrap Wintosh Darwin file descriptors or the process standard handles. It implements
bounded reads, available data, writes, seeking, truncation, flushing, and close.
It does not reproduce Cocoa's I/O exception model, asynchronous notifications,
file coordination, or secure coding.
NSObject supports common class/instance queries, allocation, equality, hash,
description, and autorelease messages. `NSApplication`, `NSWindow`, and `NSView`
inherit from the host `NSResponder` class, which provides
`-nextResponder`/`-setNextResponder:`. The Mach-O loader
registers x86_64 `__cfstring` records before running image initializers. These
constant strings support `-UTF8String` and `-length`; retain and release leave
their read-only literal storage unchanged. UTF-16 literals are converted to
UTF-8 on demand. Dynamically allocated UTF-8 strings support construction
from UTF-8 or another NSString, `-length`, `-UTF8String`,
`-isEqualToString:`, `-isEqual:`, content-based `-hash`,
`-stringByAppendingString:`, `-hasPrefix:`,
`-hasSuffix:`, `-substringWithRange:`, `-substringFromIndex:`,
`-substringToIndex:`, and immutable `-copyWithZone:`.
`NSAutoreleasePool` maps `init`/`new` and `drain` to the Objective-C runtime's
nested autorelease-pool stack. `NSProcessInfo.processInfo` returns the process
singleton; `arguments` comes from guest `argc/argv`, while `environment`,
`processName`, `hostName`, `processIdentifier`, processor counts, and physical
memory are backed by the Windows process and host. `processorCount` reports
maximum logical processors across processor groups, while
`activeProcessorCount` reports currently active processors. `systemUptime`
uses sleep-excluding Windows uptime where available. `userName` and
`fullUserName` use the guest environment and home path (with optional
`WINTOSH_FULL_USER_NAME` override), not the Windows account. `environment` reads
the launch-scoped guest `environ` vector, so runner `--env` values and Darwin
defaults are visible without leaking host-only variables. The OS-version
surface includes `operatingSystemVersion`, `operatingSystemVersionString`,
`operatingSystem`, `operatingSystemName`, and
`isOperatingSystemAtLeastVersion:`. Its version struct uses the configurable
`WINTOSH_MACOS_VERSION` profile and 24-byte Darwin aggregate ABI. Thermal and
power APIs and wider Darwin process state remain absent.
`NSUserDefaults.standardUserDefaults` and suite initialization bridge to the
prefix-relative preference store. Object/string/integer/double/boolean lookup,
string/integer/double/boolean writes, `removeObjectForKey:`, `synchronize`, and scalar
`registerDefaults:` values are supported, with command arguments and managed
preferences taking precedence over the writable application domain. Persisted
arrays, dictionaries, data, dates, volatile domains,
change notifications, and full domain-search behavior are not yet implemented.
`NSMutableString` adds `+string`, UTF-8/string constructors, `-appendString:`,
`-setString:`, `-deleteCharactersInRange:`, `-replaceCharactersInRange:withString:`,
and `-insertString:atIndex:`. Range operations and indices use UTF-16 code units
and reject boundaries that split a surrogate pair. Its UTF-8 storage is
released with the Objective-C object's final release. General string encodings,
formatting, localization, and complete Foundation behavior remain unsupported.
`NSArray` supports `+array`, `+arrayWithObject:`, compiler literal construction
through `+arrayWithObjects:count:`, `-initWithObjects:count:`, `-count`, `-objectAtIndex:`, and
`-containsObject:` (using `isEqual:`). `NSMutableArray` inherits the factories
and adds `-addObject:`, `-insertObject:atIndex:`, `-removeObjectAtIndex:`,
`-replaceObjectAtIndex:withObject:`, and `-removeAllObjects`. Elements are
retained for the array's lifetime and released by its Objective-C finalizer.
Remaining collection selectors, exception-compatible bounds failures, and
full Foundation collection behavior remain unsupported.
`NSSet` supports `+set`, `+setWithObject:`, compiler literal construction
through `+setWithObjects:count:`, `-count`, `-containsObject:`, `-member:`,
and `-allObjects`. `NSMutableSet` adds `-addObject:`, `-removeObject:`, and
`-removeAllObjects`. Set elements are unique by `isEqual:` and use the same
retained collection storage as arrays.
`NSDictionary` supports `+dictionary`, compiler literal construction through
`+dictionaryWithObjects:forKeys:count:`, `-initWithObjects:forKeys:count:`,
`-count`, `-objectForKey:`, and
`-allKeys`, and `-allValues`. `NSMutableDictionary` adds `-setObject:forKey:`,
`-removeObjectForKey:`, and `-removeAllObjects`. Keys and values are retained
and released by an Objective-C finalizer; key lookup uses `isEqual:`. Hashing and the remaining
Foundation dictionary behavior are not implemented yet.
`NSData` supports `dataWithBytes:length:`, `initWithBytes:length:`, `bytes`,
`length`, `getBytes:length:`, and `subdataWithRange:`. `NSMutableData` adds
empty and zero-filled factories, `appendBytes:length:`, `setLength:`,
`mutableBytes`, and `replaceBytesInRange:withBytes:length:`. Byte storage is owned by the object;
the pointer returned from `bytes` is invalidated by a later mutable operation.
Data encodings, no-copy ownership, and the remaining data APIs are unsupported.
`NSNumber` supports integer, unsigned integer, double, and boolean factories;
matching scalar getters, `stringValue`, and basic numeric `isEqual:`. Integer
plists preserve signed or unsigned integral values when the CFNumber fits the
supported 64-bit conversion. Decimal formatting, the rest of NSNumber's
factory/conversion surface, and full Foundation numeric comparison semantics
remain unsupported.
`NSDate` supports `date`, `distantPast`, `distantFuture`, reference-date/
Unix-time/relative-to-now factories and initializers, date-relative factories,
the matching interval queries, `compare:`, `earlierDate:`, `laterDate:`,
interval addition, and equality. The Unix-to-Cocoa epoch conversion uses the
fixed 978307200-second offset. `NSNull` provides its
process-wide singleton. Bundle plist conversion maps CFDate and CFNull values
to these Foundation objects. Calendar and formatter APIs remain unsupported.
`NSKeyedUnarchiver +unarchiveObjectWithData:` decodes a subset of binary
`NSKeyedArchiver` archives into Foundation strings, numbers, data, dates,
arrays, dictionaries, and null values. UID references are resolved and repeated
references reuse the decoded object. Array/dictionary and custom-class
placeholders are cached before recursive decoding, so supported cycles preserve
identity. Custom `initWithCoder:` classes can read object, class-checked object,
bool, signed/unsigned 32- and 64-bit integer, float, and double keys through a
temporary decoder context. Contexts work only during that initializer call;
secure-coding policy, NSCoder superclass/conditional decoding, decode delegate
callbacks, and complete NIB decoding remain unsupported. The partial AppKit
launch path can read a classic NIB archive through `NSMainNibFile` and
materialize selected `NSIBObjectData` values. Strongly connected object graphs
retain their members just as ordinary reference-counted containers do.
The host `NSView -initWithCoder:` path reads `NSFrame`, optional `NSBounds`,
`NSAutoresizingMask`, `NSSubviews`, hidden state from `NSvFlags`, and `NSNextKeyView`. Its rectangle
reader accepts NSString geometry values. This does not replace the remaining
macOS NIB placeholder and launch lifecycle work.

`NSView.autoresizingMask` and `setAutoresizingMask:` expose the six flexible
margin/size bits. When a view's bounds size changes, child origins and sizes
distribute the delta proportionally across the flexible margins and size, using
their previous extents as weights. Window content-view resizing uses the same
path, keyed view decoding restores `NSAutoresizingMask`, and subclass overrides
of `resizeSubviewsWithOldSize:` and `resizeWithOldSuperviewSize:` are dispatched
with their Darwin `NSSize` ABI.
Auto Layout
constraints, springs-and-struts interactions beyond this distribution rule,
and a guest NIB runtime fixture remain unsupported/unverified.

The Foundation surface also includes a basic target/selector `NSTimer` path:
`timerWithTimeInterval:target:selector:userInfo:repeats:`, its scheduled
counterpart, `fire`, `invalidate`, `isValid`, `timeInterval`, `fireDate`,
`setFireDate:`, and `userInfo`. Repeating timers advance from their scheduled
fire dates and skip missed intervals. Scheduled callbacks are queued onto the
AppKit application thread and wake its message loop. Run-loop modes,
invocation-based timers, and delivery when no AppKit loop runs are not modeled.
The `NSRunLoop` subset exposes current/main loop wrappers, underlying
`CFRunLoop` access, default/common mode values, run-until-date and one-mode
turns, plus ordered `performSelector:target:argument:order:modes:` scheduling.
The CoreFoundation callback queue preserves work for inactive modes and
recognizes the default mode as a member of common modes. On the AppKit main
thread, queued selector work wakes and is drained by `NSApplication.run`.
Version-0 `CFRunLoopSource` supports context ownership, order queries,
add/remove/contains by mode, signal delivery, and invalidation. Signaling queues
the source's ordered perform callback on each associated loop; the first
eligible loop consumes the signal. Version-1 sources query their Mach port and
poll it in 10 ms slices while active; one received message is passed to the
version-1 perform callback. Returned replies are sent through the Mach ABI and
deallocated through the callback's allocator. Each live source reserves its
port. Native multi-port wait remains unsupported. `CFMachPort` wraps receive rights, owns rights allocated through
`CFMachPortCreate`, exposes validity/context/invalidation callbacks, and creates
a source-1 run-loop source that forwards messages to the wrapper callout.
`CFMachPortCreateWithPort` leaves the supplied right caller-owned. Guest ABI
callbacks and full Mach right/message ownership still need a Mach-O fixture.
`CFAllocator` supports custom context creation, allocation, reallocation,
deallocation, preferred-size queries, and per-thread default selection.
Default allocations use the guest heap. `CFAllocatorCreate` uses its requested
backing allocator, including the supplied context for `kCFAllocatorUseContext`.
Guest `CFString` (CString/characters and copy/mutable-copy/mutable
constructors), `CFData`, `CFArray`, `CFDictionary`,
`CFSet`, `CFNumber`, `CFDate`, `CFMachPort`, and the guest CFURL string/path
constructors also retain and use their requested allocator for object blocks.
The Mach-O resolver exposes CFURL type/string/path/scheme/authority/port,
query/fragment, username/password, directory, and filesystem-representation
queries; `CFURLGetString` is cached for URL lifetime and query/fragment copies
honor `charactersToLeaveEscaped`.
Their STL-backed payload storage and other CF constructors still use the host
allocator. HFS-style CFURL paths remain unsupported. The default,
system-default, malloc, malloc-zone, null, and use-context constants are
exported; malloc-zone currently uses the system allocator behavior.
CF timers
and observers, native Windows input-source registration, recursive activations, selector cancellation, and
arbitrary-mode `NSTimer` scheduling remain outside this subset. CoreFoundation
timers can be created, registered by mode, queried, rescheduled, invalidated,
and fired on the active loop thread. Repeating timers preserve cadence while
skipping missed intervals. Timer tolerance is not modeled.
`CFRunLoopObserver` can register for entry, before-timer/source,
before/after-wait, and exit activities, with activity, context, order, repeat,
and validity queries. One-shot observers invalidate on their first matching
callback. The AppKit main-thread run loop now waits on and dispatches the
Win32 message queue, including Cocoa window/input messages, so a mode turn can
return after dispatched input. Worker loops keep their condition-variable
wait. These guest ABI paths have not yet been verified with a Mach-O fixture;
arbitrary Windows-handle registration remains absent.
Guest `CFRunLoopRunInMode` returns the CoreFoundation result values for
finished, stopped, timed out, and handled source; `CFRunLoopRun` is also
exported and returns when stopped or when its mode has no work source. This is
build-verified only, without a Mach-O ABI fixture.
`CFRunLoopIsWaiting` reports true only during a host wait and only when queried
from another thread, matching the documented current-thread behavior.
Queued `NSEvent` retrieval checks the requested mode against the main run
loop's common-mode set and leaves ineligible events queued.
The AppKit main loop includes default, event-tracking, and modal-panel modes in
that set, following the documented Cocoa common-mode defaults.
Run-loop introspection also exports `CFRunLoopGetTypeID`,
`CFRunLoopCopyCurrentMode`, and `CFRunLoopCopyAllModes`. The current mode is
null while idle; the copied mode array lists known common, active, and
registered modes. Run loops share the CF object header, have the run-loop type
ID, and are immortal, so generic `CFRetain`/`CFRelease` is safe. These new
guest ABI calls compile and link, but still need
invocation from a Mach-O fixture.
The keyed-archive decoder also recognizes Apple's `NSCustomView` placeholder,
resolves its `NSClassName`, allocates a loaded `NSView` subclass, and calls
`initWithFrame:` using `NSFrame` or `NSFrameSize`. It then restores selected
`NSViewTemplate` and `NSTextViewTemplate` resolve and
validates its archived `NSClassName`, then calls that subclass's
`initWithCoder:` under the active archive context. Inherited `NSView` decoding
restores selected frame, bounds, subviews, hidden, and next-key-view values;
custom coder values remain limited to the decoder's supported key types.
`NSWindowTemplate` maps to
its registered `NSWindow` subclass and restores the archived frame, style,
title, size limits, and `NSWindowView` tree. `NSMenuTemplate` now follows the
same bounded substitution path into its registered `NSMenu` subclass, so the
existing menu coder can materialize its title, items, and main-menu marker;
the private Apple archive field layout still needs a real NIB fixture before
this mapping can be claimed as runtime compatible. Decoded `NSNibOutletConnector`
objects apply a setter or fall back to an object ivar; `NSNibControlConnector`
objects apply selected `NSControl`/`NSMenuItem` target-action pairs. The decoder also
instantiates loaded `NSCustomObject` classes, maps the `NSApplication` proxy
to the shared application, and represents `FirstResponder` as a nil-target
sentinel. Decoded interface objects receive `awakeFromNib` after connections;
File's Owner receives it last when implemented. Apple's OS X NIB docs do not
support custom named placeholders other than File's Owner; UIKit's named proxy
replacement is separate. `NSMenu` and `NSMenuItem` decode their title, item, action, key
equivalent, target, and submenu fields. These partial handlers run after the
reachable archive graph decodes successfully. File's Owner substitution uses
the current load owner; unsupported proxy archive forms, binding connectors,
unsupported placeholder types, and complete NIB loading remain absent. For the supported classic
archive path, connectors are applied before `awakeFromNib`; outlet connectors
use a setter when available, then fall back to an object ivar. Target/action
connectors configure selected controls and menu items. Constructed visible
windows are ordered front. Modern top-level-object arrays retain the decoded
archive graph until the array is released; this is broader than Cocoa's
top-level strong-reference ownership. The legacy and main-nib paths retain
decoded graphs for the application lifetime.
`NSBundle` supports `+mainBundle`, deprecated `+loadNibNamed:owner:`,
`-loadNibNamed:owner:topLevelObjects:`, `+bundleWithPath:`,
`+bundleWithIdentifier:`, `-initWithPath:`, bundle/executable/resource paths,
`+bundleWithURL:` and `-initWithURL:` for file URLs,
`bundleURL`, `executableURL`, and `resourceURL`,
the bundle identifier, `infoDictionary`, `principalClass`, `classNamed:`, and
`objectForInfoDictionaryKey:`. `principalClass` reads `NSPrincipalClass` from
the bundle metadata, and both class queries load the bundle executable before
looking up Objective-C classes. The dynamic lookup is limited to code paths
supported by Wintosh's current `dlopen` adapter. `executableArchitectures`
returns raw Mach-O CPU type values from thin or fat32/fat64 headers. Common
CPU constants for i386, x86_64, ARM64, PPC, and PPC64 are exported. Common plist
strings, numbers, booleans, data, arrays, and dictionaries are converted
recursively. It also supports `pathForResource:ofType:` with an optional directory, localized
resource and string lookup, and `URLForResource:withExtension:` with an optional
subdirectory. Returned URLs are file URLs backed by the same bundle path
resolution used by the path-returning methods. `load`/`isLoaded`/`unload` use
the existing Mach-O loader. A successful first load posts
`NSBundleDidLoadNotification` with `NSLoadedClasses` containing the class names
registered during the load. `preflightAndReturnError:` uses the CFBundle Mach-O
and host-architecture check; `loadAndReturnError:` performs the dynamic load.
Failures return `NSError` values in `NSCocoaErrorDomain`: unreadable or missing
Mach-O executables use `NSExecutableNotLoadableError`, a missing host slice uses
`NSExecutableArchitectureMismatchError`, and dependency/load failures use
`NSExecutableLoadError`. Link errors are not distinguished yet.
`NSBundle` is a separate Objective-C object
retaining a CFBundle; it is not a CFBundle cast. Non-file URLs, security-scoped
URLs and unknown CF types remain unsupported. Dynamic loading still depends on
the current Mach-O adapter's architecture and image support.
The window slice supports `+frameRectForContentRect:styleMask:` and
`+contentRectForFrameRect:styleMask:` using the same Win32 style insets as
window creation, plus `-init`,
`-initWithContentRect:styleMask:backing:defer:`, `-frame`,
`-setFrame:display:`, `-setTitle:`, `-title`,
`-contentSize`, `-setContentSize:`, `-convertRectToScreen:`, and
`-convertRectFromScreen:`. Screen conversion uses the actual Win32 client origin
and the primary display's bottom-left coordinate convention. Resizing updates the Win32 outer frame
using the configured style and resizes the root content view while preserving
the AppKit content origin. `frame` reads the actual Win32 outer rectangle and
converts its screen origin to AppKit's bottom-left coordinates. The display flag
is accepted; `SetWindowPos` performs the redraw and a true flag also calls
`UpdateWindow` immediately.
`-makeKeyAndOrderFront:`, `-orderOut:`, `-close`, and `-isVisible` using native
Win32 windows. Each window owns a default content `NSView`; the view layer
supports `-initWithFrame:`, `-frame`, `-setFrame:`, `-frameOrigin`, `-frameSize`,
`-setFrameOrigin:`, `-setFrameSize:`, `-bounds`, and `-setBounds:`,
`-window`, `-superview`, `-subviews`, `-nextResponder`, `-isDescendantOf:`,
`-isHidden`, `-setHidden:`, and `-isHiddenOrHasHiddenAncestor`,
`-convertPoint:toView:`, `-convertPoint:fromView:`, `-convertRect:toView:`,
`-convertRect:fromView:`, `-addSubview:`, and
`-removeFromSuperview`. Point conversion composes frame origins and subtracts
bounds origins, and applies axis-aligned frame-to-bounds scaling for views
attached to the same window; rectangle conversion maps all four corners and
returns their axis-aligned bounding rectangle. A nil source or target uses
window coordinates.
For view `v`, let `S_v = (frame.width/bounds.width,
frame.height/bounds.height)` and let `F_v` be `isFlipped`; the window coordinate
system is treated as non-flipped. The x map is
`x' = frame.x + Sx * (x - bounds.x)`. The y map is
`y' = frame.y + Sy * (y - bounds.y)` when `F_v` equals its superview's flipped
state, and `y' = frame.y + frame.height - Sy * (y - bounds.y)` otherwise.
The default `NSView -isFlipped` returns false; guest subclass overrides are
queried when composing the transform. Conversion from source `s` to target `t`
composes the transforms along each ancestor chain:
`p_t = T_t^-1(T_s(p_s))`; a nil endpoint uses the identity window transform.
The adapter uses unit scale on an axis whose bounds extent is zero. This
matches Apple's same-window and nil-to-window behavior
([`convertPoint:toView:`](https://developer.apple.com/documentation/appkit/nsview/convert%28_%3Ato%3A%29-6u9ir?changes=_2___8_8&language=objc)).
Hidden state propagates to descendant native controls without changing
descendant `isHidden` values, matching Apple's [hidden-view semantics](https://developer.apple.com/documentation/appkit/nsview/ishidden).
When a view's effective hidden state changes through `setHidden:` or reparenting,
the adapter sends `viewDidHide` or `viewDidUnhide` to the affected views, as
documented by Apple's [hide callback](https://developer.apple.com/documentation/appkit/nsview/viewdidhide%28%29).
Adding a child retains it, sets its
superview/responder link, and detaches it from any previous parent; removal
clears its window association through its descendant tree. The content view's
next responder is its window. Bounds default to a local origin with the frame's
size and remain independently configurable afterward. Apple defines
`superview` as the immediate parent and specifies that `addSubview:` also sets
the child's next responder
([NSView hierarchy](https://developer.apple.com/documentation/appkit/nsview/superview)).
Host-backed views register a runtime finalizer. Releasing the last Objective-C
reference detaches and destroys their native controls, removes view/control and
popup-item mappings, releases retained targets/titles/items and child views, and
clears a first-responder reference into the released subtree.
Win32 button placement composes ancestor frame/bounds translations and scales
to map nested view frames into window content coordinates. Rotation and arbitrary rotated transforms are not implemented.
NSControl` provides target, action, and enabled-state methods inherited by
`NSButton`. This mirrors Apple's `NSView` → `NSControl` → `NSButton` hierarchy
and target/action model
([NSControl](https://developer.apple.com/documentation/appkit/nscontrol)).
`NSButton` maps
to a native Win32 push button and supports title, target, action, and enabled
state. `+buttonWithTitle:target:action:` creates an autoreleased button with
the supplied title, target, and selector; its x86_64 Darwin dispatcher decodes
the object/object/selector argument sequence explicitly. A button click sends
the configured action to its explicit target with the button as sender. When
the target is nil, delivery searches the button's next-responder chain through
its parent views, window, and application, then checks the application delegate;
the first object that implements the selector receives the action. This
button model also exposes `buttonType`, `state`, and `allowsMixedState`.
Checkbox-like button types cycle off/on by default; enabling mixed state adds
the indeterminate value in the off → on → mixed → off sequence. Turning mixed
state off while indeterminate resets the button to off. Native check state is
updated before target/action delivery. Radio selection clears radio siblings
in the same superview.
This
constructor
matches Apple's documented convenience method
([NSButton buttonWithTitle:target:action:](https://developer.apple.com/documentation/appkit/nsbutton/init%28title%3Atarget%3Aaction%3A%29?language=objc)).
`NSTextField` uses a native Win32 edit control and supports `stringValue`,
`setStringValue:`, `placeholderString`, `setPlaceholderString:`, `isEditable`,
and `setEditable:`. User edits update the guest-side NSString value, Return sends
the configured target/action, and the text field participates in focus traversal.
Text formatting, secure entry, validation, field-editor behavior, and delegate
notifications are not implemented.
`NSTextView` is also registered as an `NSView` subclass and maps to a native
multiline Win32 edit control with vertical scrolling. Its `string` and
`setString:` methods share guest NSString state with native edits; it also
supports `selectedRange`/`setSelectedRange:`, `isEditable`, `setEditable:`,
first-responder status, and focus traversal. Selection offsets use Cocoa's
UTF-16 units and synchronize with native edit-control selections. Text
containers/storage, attributed editing, advanced range semantics, layout,
undo, and text-view delegate notifications remain absent.
`NSImage` loads image data or files through ImageIO's Windows Imaging Component
decoder, supports `imageNamed:` lookup in the main bundle for PNG, JPEG, TIFF,
BMP, GIF, and ICNS resources, reports pixel dimensions as its size, and draws
through the active CoreGraphics context. ICNS handling selects the largest
embedded PNG; legacy raw bitmap chunks and JPEG 2000 payloads are skipped.
`NSBitmapImageRep` decodes an `NSData` payload
and exposes pixel dimensions, logical size, `CGImage`, and basic drawing;
`NSImage.representations` exposes decoded archived reps or a synthesized bitmap
rep for file/data-backed images. Keyed `NSImage` archives
can select a decoded bitmap rep from their `NSReps` array or use a direct NSData payload.
`NSImageView` retains an image, decodes its archived image reference, and draws
the image scaled to its view bounds. Multi-frame selection, vector/PDF
decoding, template rendering, full compositing options, and most archived image
representations remain unsupported. Image APIs compile, but no Mach-O image-view
fixture verifies pixels.
`NSScrollView` owns an `NSClipView` content view and a document view. The clip
view clips drawing and native text-control regions to its bounds, constrains
scroll points to the document frame, and reports `documentVisibleRect`.
`scrollToPoint:` and `NSView.scrollPoint:` update the clip bounds, and mouse
vertical and horizontal wheel input scrolls the nearest clip view when its
matching scroller is enabled. Enabled vertical/horizontal
scrollers create native Windows scrollbar controls with line/page, thumb, and
endpoint actions; thumb range and position track document size and clip bounds.
The clip area shrinks for enabled scrollers, and `autohidesScrollers` hides an
axis when its document fits. Scroller visual style and full AppKit scroller
tracking remain different from macOS.
`NSView.visibleRect` intersects the view bounds with ancestor view clipping, and
`scrollRectToVisible:` moves the nearest clip view by the minimum axis-aligned
offset needed to expose the requested rectangle, returning whether it moved.
Rotation, non-axis-aligned transforms, occlusion by overlapping sibling views,
and printing visibility are not modeled.
`NSClipView.documentVisibleRect` converts the visible clip bounds into the
document view's own coordinate space, including its bounds origin and the
supported axis-aligned view transforms. `documentRect` expands a smaller
document frame to at least the clip bounds size. `NSView.enclosingScrollView`
returns the nearest ancestor scroll view, or nil when none exists.
`NSScrollView.documentVisibleRect` forwards that document-space rectangle, and
`reflectScrolledClipView:` refreshes the native scrollbar ranges and thumb
positions for its content clip view.
`NSControl` also exposes `intValue`, `integerValue`, `floatValue`, `doubleValue`,
and `boolValue` with matching setters. Numeric getters parse the leading value
from the control's string; numeric setters replace that string using C runtime
formatting.
`NSProgressIndicator` maps to the Win32 common-controls progress bar and
supports `minValue`, `maxValue`, `doubleValue`, `incrementBy:`,
`isIndeterminate`/`setIndeterminate:`, bar/spinning `style`, and animation
start/stop. Determinate values map proportionally into the native 0–10000 range;
started indeterminate and spinning modes use the Windows marquee animation. The native
theme, animation timing, and accessibility presentation differ from AppKit.
`NSSlider` is registered as an `NSControl` subclass and maps to the native
trackbar. It supports numeric and string values, range, orientation, continuous
mode, and inherited target/action handling; tick mark customization, AppKit knob
appearance, and accessibility semantics remain different or unimplemented.
`NSStepper` maps to the native up-down control and supports `minValue`,
`maxValue`, `increment`, `doubleValue`, and `valueWraps` (default true); native
arrow changes update the Cocoa value and dispatch target/action when the value
changes. `autorepeat` is supported: disabled mode applies a single mouse change
on release; enabled mode uses Windows press-and-repeat timing, which differs
from Apple's 0.5-second delay and 10-per-second rate. Keyboard behavior differs.
`NSPopUpButton` maps to a native noneditable ComboBox and supports titled item
insertion/removal, count and title queries, index/title selection, selected-title
access, and inherited target/action dispatch on user selection. Item-object
queries return stable NSMenuItem objects, and changing an item's title updates
the native list. Item enabled/state behavior does not yet affect native entries.
Pull-down command presentation, images, and popup tracking callbacks remain
unimplemented.
`NSWindow` tracks `firstResponder` and accepts `makeFirstResponder:` requests
for attached, visible views that accept first responder status; native buttons,
text fields, and text views accept focus, and clicking a button updates the responder. Hiding
or detaching the focused view moves focus to the first visible enabled native
control in the content view, or to the window if none exists. This does not yet
implement custom-view focus behavior. `NSView nextKeyView` and
`setNextKeyView:` configure limited native-control traversal: Tab follows
visible enabled button and text-field links when available, then falls back to
view-tree order; Shift+Tab follows a direct predecessor link or reverse order.
When a view is finalized, links in other views that point to it are cleared so
the traversal cannot follow a stale view pointer.
`NSWindow selectNextKeyView:` and `selectPreviousKeyView:` enter the same
traversal and choose the first or last eligible native control if focus is not
currently on one.
The CGRect stack argument and three following integer-register
arguments are decoded for this initializer; content dimensions, primary-screen
position, title bar, close, resize, minimize, and borderless style bits are
mapped to Win32. The backing-store and defer arguments are accepted but ignored.
The initializer signature follows Apple's documented `NSRect`, style mask,
backing type, and defer parameters
([Apple AppKit reference](https://developer.apple.com/documentation/appkit/nswindow/init%28contentrect%3Astylemask%3Abacking%3Adefer%3A?language=objc)).
`NSApplication -run` calls the delegate's will-finish and
did-finish launch selectors with a nil notification placeholder, then pumps the
Win32 message loop. It creates a
blank bootstrap window only when the delegate did not show an `NSWindow`.
Destroying one window keeps the loop alive while another AppKit window still
has a live Win32 handle. Closing the last window leaves the application loop
running by default; if implemented, the delegate's
`applicationShouldTerminateAfterLastWindowClosed:` decides whether to request
termination, which then passes through `applicationShouldTerminate:`.
This follows Apple's documented delegate contract for
[`applicationShouldTerminateAfterLastWindowClosed:`](https://developer.apple.com/documentation/appkit/nsapplicationdelegate/applicationshouldterminateafterlastwindowclosed%28_%3A%29).
`stop:` requests the loop to return after the current Win32 event finishes.
`terminate:` consults the delegate's `applicationShouldTerminate:` when it is
implemented and handles `NSTerminateCancel`, `NSTerminateLater`, and
`NSTerminateNow`; `replyToApplicationShouldTerminate:` completes a deferred
decision. A worker-thread stop request is forwarded to the app thread and
converted there to `PostQuitMessage`. This does not implement nested modal
event-loop termination, run-loop mode filtering, or the full AppKit event-object model.
When termination proceeds immediately or a deferred reply approves it,
Wintosh dispatches `NSApplicationWillTerminateNotification` and calls the
delegate's `applicationWillTerminate:` once on the application thread before
posting quit, including when a deferred reply arrives from a worker thread. A
plain `stop:` remains a run-loop stop request and skips termination callbacks.
`WM_ACTIVATEAPP` is translated into the four AppKit application activation
notifications and optional delegate callbacks. Repeated messages from multiple
top-level windows are coalesced; `isActive` reflects the transition state on
the application thread. Apple specifies that the notifications use `NSApp` as
their object and are posted on the main actor
([will become active](https://developer.apple.com/documentation/appkit/nsapplication/willbecomeactivenotification?language=objc),
[did become active](https://developer.apple.com/documentation/appkit/nsapplication/didbecomeactivenotification?language=objc)).

Window `WM_PAINT` now creates a bitmap-backed CoreGraphics context, exposes it
through `NSGraphicsContext currentContext` and `CGContext`, then traverses the
visible `NSView` tree. The traversal composes the existing axis-aligned
frame/bounds and flipped-y transforms, clips each view to its bounds, and
passes the damaged portion in local coordinates to `drawRect:`.
`setNeedsDisplay:` and `setNeedsDisplayInRect:` invalidate the corresponding
Win32 window area. The BGRA bitmap is copied through the paint DC's damage
clip. The base `NSView` implementation is a no-op. Frame/bounds, hierarchy,
visibility, and window-size changes invalidate the full window; old/new exposed
rectangles are not tracked separately. Text rendering and other Quartz/AppKit
drawing APIs remain absent, and no guest AppKit drawing fixture has verified
pixels.

`NSColor` provides common named RGB colors, calibrated/device/sRGB
four-component and calibrated/device/neutral-white constructors, calibrated/device
HSB constructors, a device CMYK constructor and `getCyan:magenta:yellow:black:alpha:`,
and `getHue:saturation:brightness:alpha:`, plus `set`,
`setFill`, `setStroke`, `colorWithAlphaComponent:`, and
`blendedColorWithFraction:ofColor:`. The alpha method returns a new color and
preserves its RGB and grayscale classification. Blending interpolates stored
RGBA values when both colors are present in Wintosh's RGB color table and
preserves grayscale classification only when both inputs are grayscale.
The drawing methods update the active bitmap `CGContext`; colors created
outside a draw callback can still be retained and applied during drawing. The
Objective-C dispatcher handles the two-, four-, and five-double
class-factory signatures. Calibrated/device/sRGB RGB constructors, white
constructors, HSB constructors, CMYK constructors, and alpha replacement clamp
their components to `[0, 1]` to match the documented ranges.
`redComponent`, `greenComponent`, `blueComponent`, and `alphaComponent` expose
stored RGBA values; grayscale-only `whiteComponent` exposes the gray value.
`getRed:green:blue:alpha:` writes RGBA through guest pointers and returns a
success boolean. `hueComponent`, `saturationComponent`, and
`brightnessComponent` expose values computed from stored RGB.
`getHue:saturation:brightness:alpha:` converts the stored RGB components to
HSB and, like the RGB getter, accepts null output pointers.
`numberOfComponents` and `getComponents:` expose two values (white, alpha) for
tracked grayscale colors and four values (red, green, blue, alpha) otherwise.
The device CMYK factory clamps finite inputs and converts with
`RGB=(1-CMY)(1-K)`; its getter derives CMYK from the RGB values and supports
independently null output pointers. This is an RGB-backed approximation: device
profiles and true CMYK color preservation are not implemented.
Calibrated and device HSB constructors clamp each finite input component to
`[0, 1]` and convert it into the adapter's stored RGB representation; their
color-space distinction is not modeled. `getWhite:alpha:` returns `false` for non-grayscale colors
without changing its output pointers. Calling `whiteComponent` on a
non-grayscale color returns zero; Apple's color-space exception behavior is
not implemented. Color-space conversion, pattern colors, and dynamic/system
colors are not implemented.

`NSString` implements the basic `drawAtPoint:` path during an AppKit view draw.
It converts the string to UTF-16, transforms the baseline point with the active
CoreGraphics CTM, and draws through GDI into the same bitmap context used for
view painting. It uses the stock Windows GUI font and the last NSColor fill
color (black by default), while preserving bitmap alpha. `drawAtPoint:withAttributes:`
reads `NSFontAttributeName` and `NSForegroundColorAttributeName` from
NSDictionary; the exported NSString keys are `NSFont` and `NSColor`. This stays
a single-line primitive without paragraph styling, wrapping, text measurement,
or full Cocoa baseline/typography behavior. `NSFont` provides
`systemFontOfSize:`, `boldSystemFontOfSize:`, `fontWithName:size:`, and `set`.
Requested font names are passed to Windows GDI and can fall back to an installed
substitute, so this is not a font-file or metrics compatibility layer. No
Mach-O guest drawing fixture currently verifies the pixels.

`NSAttributedString` now supports immutable construction with a string and an
attributes dictionary, the `string` and `length` queries, and `drawAtPoint:`.
Its draw method forwards the retained attributes to the NSString renderer.
Only font and foreground color currently affect pixels. `NSMutableAttributedString`
supports mutable backing text, range replacement, and whole-string
`setAttributes:range:`; partial attribute ranges are ignored. Paragraph layout
and text measurement are not implemented.

`NSBezierPath` stores Move/Line/Cubic-Curve/Close segments and rectangle/oval
subpaths, with class factories for empty, rectangular, oval, and rounded
rectangular paths. Rounded rectangles use clamped per-axis radii and cubic
Bezier corner approximations; negative rectangle dimensions are standardized.
They can also be appended to an existing path. The Objective-C dispatcher
decodes their stack-passed `CGRect` and XMM-passed radius doubles from the guest
SysV ABI. Paths can append other paths, clear their points, set/read line
width, and fill, even-odd fill, or stroke into the active `CGContext`. The
CGPoint triple used by cubic curves is decoded from the guest SysV XMM
argument registers.
`bounds` computes cubic curve extrema; `controlPointBounds` includes the
control points, and `isEmpty` reports whether the path has any segments.
The path stores `lineCapStyle` (`butt`, `round`, or `square`) and
`lineJoinStyle` (`miter`, `round`, or `bevel`). Quartz saves and restores both
styles with the graphics state; miter joins fall back to bevel when they exceed
the default ten-half-width limit. Element inspection, dash patterns, and many
additional path operations remain unimplemented.

The Objective-C resolver routes no-argument CGRect getters through the
`objc_msgSend_stret` family, including legacy and Super2 variants, which write
the expected 32-byte x86_64 result into the guest's hidden return buffer.
Other aggregate signatures and
`initWithContentRect:styleMask:backing:defer:screen:` remain unsupported.
The host provides `NSNotification` values and a singleton `NSNotificationCenter`
with synchronous selector observers, weak observer/object references, optional
name/object filters, removal, and name- or object-based posting. Application
launch posts will-finish and did-finish notifications and passes those objects to
the corresponding delegate callbacks. Block observers, operation queues,
delivery queue policies, and most AppKit event notifications remain absent.
Basic Win32 mouse, keyboard, and wheel messages become `NSEvent` objects.
`NSApplication -nextEventMatchingMask:untilDate:inMode:dequeue:` pumps the Win32
queue and returns matching events; with `dequeue:NO`, the event remains queued.
`postEvent:atStart:` inserts a guest NSEvent at the front or back of the same
queue and wakes the regular `run` loop when it is active.
`discardEventsMatchingMask:beforeEvent:` removes earlier queued events whose
types match the mask.
Calling this API switches native input from immediate `sendEvent:` delivery to
queued delivery, so the caller can pass the returned object to `sendEvent:`.
`NSApp.currentEvent` is set for the full sendEvent callback, including overridden
application implementations, and is nil outside event delivery. Timeouts use
`NSDate -timeIntervalSinceNow`; long deadlines are waited in bounded Win32
intervals until the Cocoa date expires. The run-loop mode argument is
accepted but does not filter events yet. Without polling, input continues to
pass directly through `NSApplication.sendEvent:`. Key-down events
first call the key window's `performKeyEquivalent:`; the default window/view
implementations descend into the content view and subviews before app-menu key
equivalents run, followed by `keyDown:`. Unhandled keys fall through to native
Windows controls. `NSButton` implements `keyEquivalent`,
`keyEquivalentModifierMask`, `performKeyEquivalent:`, and `performClick:`.
Matching enabled, visible buttons use the same click, state-update, and
target/action path as pointer activation. The default modifier mask is Command; on Windows physical
Ctrl also satisfies a Command shortcut, while the Windows key remains available
as Command. Classic keyed NIB decoding reads `NSKeyEquiv` and
`NSKeyEquivModMask`; storyboard decoding and complete Cocoa text key bindings
are not implemented. IME
composition, dead-key fidelity, tracking areas, pointer enter/exit, and tablet
input are not implemented. `type`, `locationInWindow`, `modifierFlags`,
`timestamp`, `window`, button/click data, `characters`,
`charactersIgnoringModifiers`, `keyCode`, and `isARepeat` are exposed for these
mouse and keyboard events. Hardware `keyCode` values come from PC scan
positions when known, with virtual-key fallback for less common keys.
Vertical and horizontal wheel messages reach a custom `scrollWheel:` override
as non-precise scroll events. Their deltas currently use the native scroll
adapter's 40-point-per-notch step; unhandled wheels continue to `NSScrollView`.
Additional controls, nib and storyboard loading, rotated coordinate
transforms, full keyboard and input-method
translation, mouse tracking areas and enter/exit events, full AppKit key-view-loop
policy, and broader application delegate behavior remain open. Native controls
currently cover only the button and text-field subsets described above. Window
placement uses the primary screen and frame metrics approximate AppKit with
the mapped Win32 window style. Consequently, this does not
establish that ordinary Cocoa applications launch. Foundation, AppKit,
CoreFoundation, CoreGraphics, and ImageIO names are recognized by dyld as
virtual providers when their macOS dylibs are not staged. Every import still
needs a real Wintosh host export, so unsupported strong imports fail. If a
matching dylib is present, normal graph loading still applies. The
bootstrap is in `darling_windows_appkit.cpp` and uses Win32 `user32`. MSVC and
Clang host runner builds link, but no Mach-O guest fixture proves this entry
point, the launch callback path, or window lifecycle at runtime.
