#!/usr/bin/env python3
"""Print CFBundleExecutable from an XML or binary Apple Info.plist."""

from __future__ import annotations

import plistlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: read-bundle-executable.py <Info.plist>", file=sys.stderr)
        return 64

    try:
        with open(sys.argv[1], "rb") as stream:
            plist = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as exc:
        print(f"could not read Info.plist: {exc}", file=sys.stderr)
        return 65

    executable = plist.get("CFBundleExecutable") if isinstance(plist, dict) else None
    if not isinstance(executable, str) or not executable:
        print("Info.plist has no string CFBundleExecutable", file=sys.stderr)
        return 66
    if "/" in executable or "\\" in executable:
        print("CFBundleExecutable must be a filename", file=sys.stderr)
        return 67

    print(executable)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
