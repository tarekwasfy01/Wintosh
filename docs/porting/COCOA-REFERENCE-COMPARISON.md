# Cocoa reference comparison

| Project | What it can inform | Build or runtime boundary | Wintosh consequence |
|---|---|---|---|
| [GNUstep](https://github.com/gnustep) | Portable Objective-C, Foundation and AppKit API families | Helps compile portable source against a replacement API; Apple frameworks, nibs, private APIs, binary ABI and Mach-O dependencies are not thereby supplied | Useful for a separate source-port or recompile target, not a macOS binary loader |
| [Cocotron](https://github.com/cjwl/cocotron) | Historical Cocoa/AppKit/Foundation implementation structure | Its Windows source can offer a build path for compatible Objective-C applications; exact Xcode, API, ABI and project requirements need per-app checks | Compare API coverage and UI architecture; keep Darwin runtime and recompiled Windows applications as separate goals |
| WinObjC | Objective-C runtime and selected iOS API compatibility for Windows | Archived project; iOS API model and Windows projections do not satisfy macOS framework ABI | Historical Objective-C/iOS reference only |
| touchHLE | App-driven high-level implementation of selected early iOS framework behavior | Runs a bounded supported-app set with its own CPU/framework implementation | Tests prioritization method; not general macOS or Cocoa runtime |
| macemu | 68k/PPC classic Macintosh CPU and device emulation | Basilisk II / SheepShaver target classic Mac OS 7–9; MinGW x86 builds documented | Study guest CPU/device boundary; unrelated to modern Darwin syscall and Cocoa compatibility |

The phrase “run a macOS app” has two technical meanings. A source-port path
rebuilds application source against GNUstep/Cocotron and produces a Windows
program. The binary-compatibility path loads the existing Mach-O and supplies
its Darwin ABI, dyld, Objective-C runtime, frameworks, resources, graphics,
and operating-system services. The first can be practical for portable source;
success on that path does not establish the second.

The current [Foundation framework matrix row](../../PRIMITIVE-MATRIX.csv)
remains partial. The next testable Foundation batch is a small application
compiled for Windows against one chosen compatibility framework, tracked as a
separate source-port capability. For existing Mach-O apps, add Foundation
class/selector/ownership/message-send fixture gates to Wintosh itself. Do not
mark the app category complete from API-name overlap alone.

Sources: [GNUstep](https://github.com/gnustep),
[Cocotron](https://github.com/cjwl/cocotron),
[WinObjC](https://github.com/microsoft/winobjc),
[touchHLE](https://github.com/touchHLE/touchHLE),
[macemu](https://github.com/kanjitalk755/macemu).
