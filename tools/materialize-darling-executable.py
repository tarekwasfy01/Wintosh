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


def overlay_path_for_guest(overlay_root: Path, guest_path: str) -> Path:
    if not guest_path.startswith("/"):
        raise ValueError("guest executable path must be absolute")
    parts = guest_path.split("/")
    if any(part in {".", ".."} for part in parts):
        raise ValueError("guest executable path cannot contain dot segments")
    return overlay_root / Path(DARLING_PREFIX, *guest_path.lstrip("/").split("/"))


def target_guest_path(link_path: str, target: str) -> str:
    if target.startswith("/"):
        resolved = posixpath.normpath(target)
    else:
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(link_path), target))
    if not resolved.startswith("/") or resolved == "/.." or resolved.startswith("/../"):
        raise ValueError(f"symlink target escapes the Darling root: {target}")
    return resolved


def resolve_regular_target(
    zf: zipfile.ZipFile,
    overlay_root: Path,
    guest_path: str,
    visited: set[str],
) -> tuple[str, Path | zipfile.ZipInfo, int]:
    if guest_path in visited or len(visited) >= 40:
        raise ValueError(f"symlink loop or too many levels resolving {guest_path}")
    visited.add(guest_path)

    overlay_path = overlay_path_for_guest(overlay_root, guest_path)
    if overlay_path.is_symlink():
        target = os.readlink(overlay_path)
        return resolve_regular_target(zf, overlay_root, target_guest_path(guest_path, target), visited)
    if overlay_path.is_file():
        return "file", overlay_path, overlay_path.stat().st_mode
    if overlay_path.exists():
        raise ValueError(f"executable symlink resolves to a non-file: {guest_path}")

    archive_entry = f"{DARLING_PREFIX}/{guest_path.lstrip('/')}"
    try:
        info = zf.getinfo(archive_entry)
    except KeyError as exc:
        raise ValueError(f"symlink target does not exist in the overlay or Darling zip: {guest_path}") from exc

    mode = info.external_attr >> 16
    if stat.S_IFMT(mode) == stat.S_IFLNK:
        target = zf.read(info).decode("utf-8")
        return resolve_regular_target(zf, overlay_root, target_guest_path(guest_path, target), visited)
    if stat.S_ISDIR(mode) or info.is_dir():
        raise ValueError(f"executable symlink resolves to a directory: {guest_path}")
    return "zip", info, mode


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: materialize-darling-executable.py <darling.zip> <overlay-root> <guest-path>", file=sys.stderr)
        return 64

    zip_path = Path(sys.argv[1])
    overlay_root = Path(sys.argv[2])
    guest_path = sys.argv[3]
    try:
        destination = overlay_path_for_guest(overlay_root, guest_path)
        if destination.exists() and not destination.is_symlink():
            return 0
        with zipfile.ZipFile(zip_path) as zf:
            initial_overlay_is_link = destination.is_symlink()
            if not initial_overlay_is_link:
                try:
                    initial_info = zf.getinfo(f"{DARLING_PREFIX}/{guest_path.lstrip('/')}")
                except KeyError:
                    return 0
                if stat.S_IFMT(initial_info.external_attr >> 16) != stat.S_IFLNK:
                    return 0
                initial_target = zf.read(initial_info).decode("utf-8")
            else:
                initial_target = os.readlink(destination)

            resolved_guest_path = target_guest_path(guest_path, initial_target)
            source_kind, source, source_mode = resolve_regular_target(
                zf, overlay_root, resolved_guest_path, {guest_path}
            )
            destination.parent.mkdir(parents=True, exist_ok=True)
            file_descriptor, temporary_name = tempfile.mkstemp(prefix=".wintosh-exe-", dir=destination.parent)
            try:
                with os.fdopen(file_descriptor, "wb") as output:
                    if source_kind == "file":
                        with source.open("rb") as input_file:
                            shutil.copyfileobj(input_file, output)
                    else:
                        with zf.open(source) as input_file:
                            shutil.copyfileobj(input_file, output)
                mode = stat.S_IMODE(source_mode) or 0o644
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

    print(f"DARLING_EXECUTABLE_SYMLINK_MATERIALIZED={guest_path} <- {resolved_guest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
