#!/usr/bin/env python3
"""Package a single relocation-free i386 Mach-O __text section as MH_EXECUTE.

This is a deliberately narrow bridge for early Wintosh bring-up, not a general
Mach-O linker. It preserves only __TEXT,__text and rejects external symbols or
text relocations; unwind/debug metadata and linker options are not linked.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


MH_MAGIC = 0xFEEDFACE
MH_OBJECT = 1
MH_EXECUTE = 2
CPU_TYPE_I386 = 7
LC_SEGMENT = 0x1
LC_SYMTAB = 0x2
LC_MAIN = 0x80000028
LC_LINKER_OPTION = 0x2D
N_STAB = 0xE0
N_TYPE = 0x0E
N_SECT = 0x0E
N_EXT = 0x01
PAGE_SIZE = 0x1000
TEXT_FILE_OFFSET = 0x200


class FormatError(ValueError):
    pass


def require_range(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FormatError(f"{what} lies outside the input file")
    return data[offset : offset + size]


def c_name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="strict")


def parse_object(data: bytes, entry_name: str) -> tuple[bytes, int, int]:
    if len(data) < 28:
        raise FormatError("input is smaller than a 32-bit Mach-O header")

    magic, cpu_type, cpu_subtype, file_type, ncmds, sizeofcmds, _flags = (
        struct.unpack_from("<IiiIIII", data, 0)
    )
    if magic != MH_MAGIC:
        raise FormatError("input is not a little-endian 32-bit Mach-O file")
    if cpu_type != CPU_TYPE_I386:
        raise FormatError(f"expected i386 cputype 7, got {cpu_type}")
    if file_type != MH_OBJECT:
        raise FormatError(f"expected MH_OBJECT filetype 1, got {file_type}")

    commands_end = 28 + sizeofcmds
    require_range(data, 28, sizeofcmds, "load-command table")
    cursor = 28
    text_section: tuple[int, int, int, int, int, int] | None = None
    symtab: tuple[int, int, int, int] | None = None
    section_ordinal = 0

    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", require_range(data, cursor, 8, "load command"))
        if cmdsize < 8 or cursor + cmdsize > commands_end:
            raise FormatError("invalid Mach-O load-command size")
        command = require_range(data, cursor, cmdsize, "load command")

        if cmd == LC_SEGMENT:
            if cmdsize < 56:
                raise FormatError("short LC_SEGMENT command")
            (_cmd, _size, _segname, _vmaddr, _vmsize, _fileoff, _filesize,
             _maxprot, _initprot, nsects, _segflags) = struct.unpack_from(
                "<II16sIIIIiiII", command, 0
            )
            if 56 + nsects * 68 > cmdsize:
                raise FormatError("LC_SEGMENT section array exceeds command size")
            for index in range(nsects):
                section = struct.unpack_from("<16s16sIIIIIIIII", command, 56 + index * 68)
                sectname, segname = c_name(section[0]), c_name(section[1])
                addr, size, offset, _align, reloff, nreloc, _flags, _r1, _r2 = section[2:]
                section_ordinal += 1
                if (sectname, segname) == ("__text", "__TEXT"):
                    if text_section is not None:
                        raise FormatError("multiple __TEXT,__text sections are unsupported")
                    require_range(data, offset, size, "__text contents")
                    require_range(data, reloff, nreloc * 8, "__text relocation table")
                    relocation_sites: set[int] = set()
                    for reloc_index in range(nreloc):
                        address, info = struct.unpack_from("<iI", data, reloff + reloc_index * 8)
                        scattered = (address & 0x80000000) != 0
                        symbolnum = info & 0x00FFFFFF
                        pcrel = (info >> 24) & 1
                        length = (info >> 25) & 3
                        external = (info >> 27) & 1
                        reloc_type = (info >> 28) & 0xF
                        if (scattered or address < 0 or address + 4 > size or
                                symbolnum != section_ordinal or not pcrel or length != 2 or
                                external or reloc_type != 0):
                            raise FormatError(
                                "__text relocation is not a 32-bit PC-relative reference "
                                "to the same section"
                            )
                        if any(site < address + 4 and address < site + 4 for site in relocation_sites):
                            raise FormatError("overlapping __text relocation fields")
                        relocation_sites.add(address)
                    text_section = (offset, size, reloff, nreloc, addr, section_ordinal)

        elif cmd == LC_SYMTAB:
            if cmdsize != 24:
                raise FormatError("invalid LC_SYMTAB command size")
            _cmd, _size, symoff, nsyms, stroff, strsize = struct.unpack_from("<IIIIII", command)
            symtab = (symoff, nsyms, stroff, strsize)

        elif cmd == LC_LINKER_OPTION:
            if cmdsize < 12:
                raise FormatError("short LC_LINKER_OPTION command")
            # Accepted syntactically; this prototype does not perform any
            # library lookup. Undefined external symbols are rejected below.

        cursor += cmdsize

    if cursor != commands_end:
        raise FormatError("load-command sizes do not match sizeofcmds")
    if text_section is None:
        raise FormatError("input has no __TEXT,__text section")
    if symtab is None:
        raise FormatError("input has no LC_SYMTAB")

    symoff, nsyms, stroff, strsize = symtab
    require_range(data, symoff, nsyms * 12, "symbol table")
    strings = require_range(data, stroff, strsize, "string table")
    text_offset, text_size, _reloff, _nreloc, text_addr, text_ordinal = text_section
    entry_value: int | None = None
    entry_section_index: int | None = None
    undefined_external_symbols: list[str] = []

    for index in range(nsyms):
        strx, n_type, n_sect, _desc, value = struct.unpack_from("<IBBHI", data, symoff + index * 12)
        if strx >= len(strings):
            raise FormatError("symbol name index is outside the string table")
        end = strings.find(b"\0", strx)
        if end < 0:
            raise FormatError("unterminated symbol name")
        name = strings[strx:end].decode("utf-8", errors="strict")
        if (n_type & N_STAB) == 0 and (n_type & N_TYPE) == 0 and (n_type & N_EXT):
            undefined_external_symbols.append(name or "<anonymous>")
        if name == entry_name:
            if n_type & N_STAB or (n_type & N_TYPE) != N_SECT or not (n_type & N_EXT):
                raise FormatError(f"{entry_name} is not an external section symbol")
            entry_value = value
            entry_section_index = n_sect

    if entry_value is None:
        raise FormatError(f"entry symbol {entry_name} was not found")
    if undefined_external_symbols:
        symbols = ", ".join(undefined_external_symbols)
        raise FormatError(f"external symbols require a full linker: {symbols}")
    # Same-section PC-relative references are translation-invariant:
    # (S + slide) - (P + 4 + slide) == S - (P + 4). Their encoded fields
    # therefore remain valid when the complete section moves as one block.
    # This packer emits only the selected text bytes, so its entry symbol must
    # resolve inside that section; other sections' data is discarded.
    if entry_section_index != text_ordinal:
        raise FormatError(f"{entry_name} is not in __TEXT,__text")
    entry_offset = entry_value - text_addr
    if entry_offset < 0 or entry_offset >= text_size:
        raise FormatError(f"{entry_name} lies outside the selected __text bytes")
    return data[text_offset : text_offset + text_size], entry_offset, cpu_subtype


def make_executable(text: bytes, entry_offset: int, cpu_subtype: int) -> bytes:
    if not text:
        raise FormatError("__text is empty")
    if len(text) > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("__text exceeds the one-page prototype image limit")

    # 32-bit mach_header, one LC_SEGMENT (without sections), and LC_MAIN.
    header = struct.pack(
        "<IiiIIII", MH_MAGIC, CPU_TYPE_I386, cpu_subtype, MH_EXECUTE, 2, 80, 0
    )
    segment = struct.pack(
        "<II16sIIIIiiII",
        LC_SEGMENT, 56, b"__TEXT", 0x1000, PAGE_SIZE, 0, PAGE_SIZE,
        7, 5, 0, 0,
    )
    main = struct.pack("<IIQQ", LC_MAIN, 24, TEXT_FILE_OFFSET + entry_offset, 0)
    image = bytearray(PAGE_SIZE)
    image[:28] = header
    image[28:84] = segment
    image[84:108] = main
    image[TEXT_FILE_OFFSET : TEXT_FILE_OFFSET + len(text)] = text
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="i386 Mach-O relocatable object")
    parser.add_argument("output", type=Path, help="output i386 MH_EXECUTE image")
    parser.add_argument("--entry", default="_main", help="external entry symbol (default: _main)")
    args = parser.parse_args()

    try:
        obj = args.input.read_bytes()
        text, entry_offset, cpu_subtype = parse_object(obj, args.entry)
        image = make_executable(text, entry_offset, cpu_subtype)
        args.output.write_bytes(image)
    except (OSError, FormatError, struct.error, UnicodeError) as error:
        print(f"i386 Mach-O prototype packer: {error}", file=sys.stderr)
        return 2

    print(f"wrote {args.output} ({len(image)} bytes, entry file offset 0x{TEXT_FILE_OFFSET + entry_offset:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
