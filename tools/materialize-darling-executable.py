#!/usr/bin/env python3
"""Materialize a symlinked Darling executable from its rootfs zip overlay."""

from __future__ import annotations

import os
import posixpath
import shutil
import stat
import sys
import tempfile
import zipfile
from pathlib import Path


DARLING_PREFIX = "usr/libexec/darling"


def archive_entry_for_guest_path(guest_path: str) -> str:
    if not guest_path.startswith("/"):
        raise ValueError("guest executable path must be absolute")
    parts = guest_path.split("/")
    if any(part in {".", ".."} for part in parts):
        raise ValueError("guest executable path cannot contain dot segments")
    relative = guest_path.lstrip("/")
    return f"{DARLING_PREFIX}/{relative}"


def resolve_symlink(zf: zipfile.ZipFile, entry: str) -> tuple[zipfile.ZipInfo, str] | None:
    try:
        info = zf.getinfo(entry)
    except KeyError:
        return None
    mode = info.external_attr >> 16
    if stat.S_IFMT(mode) != stat.S_IFLNK:
        return None

    current = entry
    for _ in range(40):
        info = zf.getinfo(current)
        mode = info.external_attr >> 16
        if stat.S_IFMT(mode) != stat.S_IFLNK:
            if stat.S_ISDIR(mode):
                raise ValueError(f"executable symlink resolves to a directory: {current}")
            return info, current

        target = zf.read(info).decode("utf-8")
        if target.startswith("/"):
            current = f"{DARLING_PREFIX}/{target.lstrip('/')}"
        else:
            current = posixpath.normpath(posixpath.join(posixpath.dirname(current), target))
        if current != DARLING_PREFIX and not current.startswith(DARLING_PREFIX + "/"):
            raise ValueError(f"symlink target escapes the Darling root: {target}")

    raise ValueError(f"too many symlink levels resolving {entry}")


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: materialize-darling-executable.py <darling.zip> <overlay-root> <guest-path>", file=sys.stderr)
        return 64

    zip_path = Path(sys.argv[1])
    overlay_root = Path(sys.argv[2])
    guest_path = sys.argv[3]
    try:
        entry = archive_entry_for_guest_path(guest_path)
        with zipfile.ZipFile(zip_path) as zf:
            resolved = resolve_symlink(zf, entry)
            if resolved is None:
                return 0
            info, target_entry = resolved
            destination = overlay_root / Path(*entry.split("/"))
            if destination.exists() and not destination.is_symlink():
                return 0
            destination.parent.mkdir(parents=True, exist_ok=True)
            file_descriptor, temporary_name = tempfile.mkstemp(prefix=".wintosh-exe-", dir=destination.parent)
            try:
                with os.fdopen(file_descriptor, "wb") as output, zf.open(info) as source:
                    shutil.copyfileobj(source, output)
                mode = stat.S_IMODE(info.external_attr >> 16) or 0o644
                os.chmod(temporary_name, mode | 0o111)
                os.replace(temporary_name, destination)
            except BaseException:
                try:
                    os.unlink(temporary_name)
                except FileNotFoundError:
                    pass
                raise
    except (OSError, UnicodeError, ValueError, zipfile.BadZipFile, KeyError) as exc:
        print(f"could not materialize Darling executable symlink: {exc}", file=sys.stderr)
        return 65

    print(f"DARLING_EXECUTABLE_SYMLINK_MATERIALIZED={guest_path} <- /{target_entry[len(DARLING_PREFIX) + 1:]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
