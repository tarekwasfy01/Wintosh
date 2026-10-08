# `apple-headers` submodule

As the title suggests, this repository is primarily meant as a submodule for the
`IPASimulator` project.

It contains `.h` files analyzed by `HeadersAnalyzer` and used by `objc` library
port. The `iPhoneOS*.sdk` was obtained from a macOS machine with Xcode installed
(in
`/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk`),
but it can also be downloaded from
[airnativeextensions website](http://resources.airnativeextensions.com/ios/).
Similarly the `MacOSX*.sdk`.

The other folders are other Apple libraries obtained from the official
[tarball repository](https://opensource.apple.com/tarballs/).

Not everything from the original SDK and libraries is included, though, see
[the `.gitignore` file](.gitignore) for more information on what's excluded.

## Git tags

We use the following tags as prefixes to commit messages:

- `[dyld]`, `[iOS]`, `[Libc]`, `[closure]`, `[plat]`, `[macOS]`, `[xnu]`: These
  correspond to the root folders.
