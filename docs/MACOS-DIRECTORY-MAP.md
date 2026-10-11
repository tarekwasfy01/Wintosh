# macOS directory mapping

Wintosh uses an explicit, relocatable prefix instead of writing Darwin paths
directly into the Windows host filesystem. The prefix is created by
`wintosh.exe --init-prefix <directory>` and is also used by the native runner
when it resolves `/System`, `/Library`, `/usr`, and `/private` paths.

The layout follows the roles described in Apple's *File System Programming
Guide*: `Library/Application Support` is intended for app-owned support data,
`Library/Caches` for regenerable cache data, and `Library/Preferences` is
reserved for the preferences API rather than arbitrary direct file writes.
See the [Apple directory reference](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/MacOSXDirectories/MacOSXDirectories.html).

| Darwin path in the prefix | Current Wintosh role | Status |
| --- | --- | --- |
| `/System/Library/Frameworks` | System framework search root for the dyld resolver | Layout and resolver path only |
| `/Library/Frameworks` | Local framework search root | Layout only |
| `/usr/lib` | Native provider/dylib search root | Used by synthetic provider fixtures |
| `/Library/Application Support` | App-specific persistent data root | Layout only; bundle-ID subdirectories are created by callers |
| `/Library/Caches` | Regenerable cache root | Layout only; no cache policy is claimed |
| `/Library/Preferences` | Preferences storage root | Existing minimal layout; full `NSUserDefaults` compatibility is not claimed |
| `/Library/Logs` | Per-user/runtime log root | Layout only; no Console.app integration is claimed |
| `/Library/Saved Application State` | Application state root | Layout only; state restoration semantics are not implemented |
| `/Users/Default/Library/*` | Relocatable Wintosh user domain | Layout only; callers must opt into this prefix-relative home |

The prefix-relative user domain is intentionally `Users/Default/Library`.
Wintosh does not silently replace the Windows user's real profile directory;
this keeps a test or application prefix self-contained and removable. The
`Containers` directory is created only in this user domain, matching Apple's
user-domain restriction. The runner now defaults the guest `HOME` variable to
`/Users/Default`; an explicit `--env HOME=...` overrides that default.
It also defaults `TMPDIR` to `/private/tmp` and `CFFIXED_USER_HOME` to
`/Users/Default`; each can be overridden with `--env`.

For host-backed POSIX file calls, the runner exports the internal
`WINTOSH_GUEST_PREFIX` bridge. Absolute guest paths such as
`/Users/Default/Library/Caches` are then resolved below the selected prefix;
relative paths retain the existing working-directory behavior. This is a
Wintosh path-translation contract, not a claim that Windows exposes a native
Darwin filesystem namespace.
| `/private/etc` | Runtime configuration root | Minimal runtime layout |
| `/private/var/run` | Runtime/socket state root | Minimal runtime layout |
| `/private/tmp` | Temporary-file root | Used as the Darwin-side temporary root |

This change does **not** bundle Apple frameworks, copy macOS system files, or
make arbitrary `.app` bundles runnable. The framework directory itself is only
a controlled lookup location; framework ABI coverage remains limited to the
implemented Wintosh families and repository-owned test providers.

## Framework lookup boundary

The dyld-compatible resolver checks app-bundle `Contents/Frameworks` and
`Contents/PlugIns` paths, then the configured prefix's system and local
framework roots. The [The Apple Wiki framework index](https://theapplewiki.com/wiki/Filesystem:/System/Library/Frameworks)
is useful as a reference inventory, but it is not a source of redistributable
Apple binaries and is not copied into this repository.

## Verification

`runtime_smoke` now checks the required prefix directories after
`Prefix::Create`. This proves layout creation only; it does not prove that a
framework or a real Apple application can execute.
