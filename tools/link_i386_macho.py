#!/usr/bin/env python3
"""Link a bounded set of i386 Mach-O text objects into a minimal MH_EXECUTE.

This bring-up linker handles only one __TEXT,__text section per input object,
32-bit PC-relative GENERIC_RELOC_VANILLA relocations, and symbols defined in
those text sections. It does not link a Darwin CRT, dylibs, data, unwind tables,
or other Mach-O relocation kinds.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
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
N_UNDF = 0x00
N_SECT = 0x0E
N_EXT = 0x01
GENERIC_RELOC_VANILLA = 0
PAGE_SIZE = 0x1000
TEXT_FILE_OFFSET = 0x200


class FormatError(ValueError):
    pass


@dataclass(frozen=True)
class Relocation:
    offset: int
    external_symbol: str | None


@dataclass(frozen=True)
class MachObject:
    path: Path
    text: bytes
    text_addr: int
    text_ordinal: int
    text_alignment: int
    cpu_subtype: int
    definitions: dict[str, int]
    undefined_symbols: tuple[str, ...]
    relocations: tuple[Relocation, ...]


def require_range(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FormatError(f"{what} lies outside the input file")
    return data[offset : offset + size]


def c_name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="strict")


def parse_object(path: Path, data: bytes) -> MachObject:
    if len(data) < 28:
        raise FormatError(f"{path}: smaller than a 32-bit Mach-O header")

    magic, cpu_type, cpu_subtype, file_type, ncmds, sizeofcmds, _flags = (
        struct.unpack_from("<IiiIIII", data, 0)
    )
    if magic != MH_MAGIC:
        raise FormatError(f"{path}: not a little-endian 32-bit Mach-O file")
    if cpu_type != CPU_TYPE_I386:
        raise FormatError(f"{path}: expected i386 cputype 7, got {cpu_type}")
    if file_type != MH_OBJECT:
        raise FormatError(f"{path}: expected MH_OBJECT filetype 1, got {file_type}")

    commands_end = 28 + sizeofcmds
    require_range(data, 28, sizeofcmds, f"{path}: load-command table")
    cursor = 28
    text_section: tuple[int, int, int, int, int, int, int] | None = None
    symtab: tuple[int, int, int, int] | None = None
    section_ordinal = 0

    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from(
            "<II", require_range(data, cursor, 8, f"{path}: load command")
        )
        if cmdsize < 8 or cursor + cmdsize > commands_end:
            raise FormatError(f"{path}: invalid Mach-O load-command size")
        command = require_range(data, cursor, cmdsize, f"{path}: load command")

        if cmd == LC_SEGMENT:
            if cmdsize < 56:
                raise FormatError(f"{path}: short LC_SEGMENT command")
            (_cmd, _size, _segname, _vmaddr, _vmsize, _fileoff, _filesize,
             _maxprot, _initprot, nsects, _segflags) = struct.unpack_from(
                "<II16sIIIIiiII", command, 0
            )
            if 56 + nsects * 68 > cmdsize:
                raise FormatError(f"{path}: LC_SEGMENT sections exceed command size")
            for index in range(nsects):
                section = struct.unpack_from("<16s16sIIIIIIIII", command, 56 + index * 68)
                sectname, segname = c_name(section[0]), c_name(section[1])
                addr, size, offset, align_exp, reloff, nreloc, _flags, _r1, _r2 = section[2:]
                section_ordinal += 1
                if (sectname, segname) == ("__text", "__TEXT"):
                    if text_section is not None:
                        raise FormatError(f"{path}: multiple __TEXT,__text sections")
                    if align_exp > 12:
                        raise FormatError(f"{path}: __text alignment is too large")
                    require_range(data, offset, size, f"{path}: __text contents")
                    require_range(data, reloff, nreloc * 8, f"{path}: __text relocations")
                    text_section = (offset, size, addr, align_exp, reloff, nreloc, section_ordinal)

        elif cmd == LC_SYMTAB:
            if cmdsize != 24:
                raise FormatError(f"{path}: invalid LC_SYMTAB command size")
            _cmd, _size, symoff, nsyms, stroff, strsize = struct.unpack_from(
                "<IIIIII", command
            )
            symtab = (symoff, nsyms, stroff, strsize)

        elif cmd == LC_LINKER_OPTION and cmdsize < 12:
            raise FormatError(f"{path}: short LC_LINKER_OPTION command")

        cursor += cmdsize

    if cursor != commands_end:
        raise FormatError(f"{path}: load-command sizes do not match sizeofcmds")
    if text_section is None:
        raise FormatError(f"{path}: missing __TEXT,__text")
    if symtab is None:
        raise FormatError(f"{path}: missing LC_SYMTAB")

    symoff, nsyms, stroff, strsize = symtab
    require_range(data, symoff, nsyms * 12, f"{path}: symbol table")
    strings = require_range(data, stroff, strsize, f"{path}: string table")
    text_offset, text_size, text_addr, align_exp, reloff, nreloc, text_ordinal = text_section

    symbols: list[tuple[str, int, int, int]] = []
    definitions: dict[str, int] = {}
    undefined: list[str] = []
    for index in range(nsyms):
        strx, n_type, n_sect, _desc, value = struct.unpack_from(
            "<IBBHI", data, symoff + index * 12
        )
        if strx >= len(strings):
            raise FormatError(f"{path}: symbol name index outside string table")
        end = strings.find(b"\0", strx)
        if end < 0:
            raise FormatError(f"{path}: unterminated symbol name")
        name = strings[strx:end].decode("utf-8", errors="strict")
        symbols.append((name, n_type, n_sect, value))
        if n_type & N_STAB:
            continue
        symbol_type = n_type & N_TYPE
        if (n_type & N_EXT) and symbol_type == N_UNDF:
            if value != 0:
                raise FormatError(f"{path}: common symbol {name} is unsupported")
            if name:
                undefined.append(name)
        elif (n_type & N_EXT) and symbol_type == N_SECT:
            if n_sect != text_ordinal:
                raise FormatError(f"{path}: external definition {name} is outside __text")
            symbol_offset = value - text_addr
            if symbol_offset < 0 or symbol_offset > text_size:
                raise FormatError(f"{path}: external definition {name} lies outside __text")
            if name in definitions:
                raise FormatError(f"{path}: duplicate external definition {name}")
            definitions[name] = symbol_offset

    relocations: list[Relocation] = []
    relocation_sites: set[int] = set()
    for reloc_index in range(nreloc):
        address, info = struct.unpack_from("<iI", data, reloff + reloc_index * 8)
        scattered = (address & 0x80000000) != 0
        symbolnum = info & 0x00FFFFFF
        pcrel = (info >> 24) & 1
        length = (info >> 25) & 3
        external = (info >> 27) & 1
        reloc_type = (info >> 28) & 0xF
        if (scattered or address < 0 or address + 4 > text_size or
                not pcrel or length != 2 or reloc_type != GENERIC_RELOC_VANILLA):
            raise FormatError(f"{path}: unsupported __text relocation at {address:#x}")
        if any(site < address + 4 and address < site + 4 for site in relocation_sites):
            raise FormatError(f"{path}: overlapping __text relocation fields")
        relocation_sites.add(address)

        if external:
            if symbolnum >= len(symbols):
                raise FormatError(f"{path}: external relocation symbol index is invalid")
            symbol_name, symbol_type, _symbol_section, _symbol_value = symbols[symbolnum]
            if not symbol_name or not (symbol_type & N_EXT):
                raise FormatError(f"{path}: external relocation does not name an external symbol")
            relocations.append(Relocation(address, symbol_name))
        else:
            if symbolnum != text_ordinal:
                raise FormatError(f"{path}: local relocation targets a discarded section")
            # The relocation is already section-relative. Uniformly moving the
            # complete section preserves its encoded PC-relative displacement.
            relocations.append(Relocation(address, None))

    return MachObject(
        path=path,
        text=data[text_offset : text_offset + text_size],
        text_addr=text_addr,
        text_ordinal=text_ordinal,
        text_alignment=1 << align_exp,
        cpu_subtype=cpu_subtype,
        definitions=definitions,
        undefined_symbols=tuple(undefined),
        relocations=tuple(relocations),
    )


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def link_objects(objects: list[MachObject], entry_name: str) -> tuple[bytes, int, int]:
    if not objects:
        raise FormatError("at least one input object is required")
    cpu_subtype = objects[0].cpu_subtype
    if any(obj.cpu_subtype != cpu_subtype for obj in objects):
        raise FormatError("input objects have different i386 CPU subtypes")

    offsets: list[int] = []
    combined_size = 0
    for obj in objects:
        combined_size = align_up(combined_size, obj.text_alignment)
        offsets.append(combined_size)
        combined_size += len(obj.text)
    if combined_size == 0 or combined_size > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text exceeds the one-page prototype image limit")

    definitions: dict[str, tuple[int, int]] = {}
    undefined: set[str] = set()
    for index, obj in enumerate(objects):
        undefined.update(obj.undefined_symbols)
        for name, value in obj.definitions.items():
            if name in definitions:
                raise FormatError(f"duplicate external definition {name}")
            definitions[name] = (index, value)
    missing = sorted(undefined - definitions.keys())
    if missing:
        raise FormatError("unresolved external symbols: " + ", ".join(missing))
    if entry_name not in definitions:
        raise FormatError(f"entry symbol {entry_name} was not found in input objects")

    text = bytearray(combined_size)
    for index, obj in enumerate(objects):
        base = offsets[index]
        text[base : base + len(obj.text)] = obj.text
        for relocation in obj.relocations:
            if relocation.external_symbol is None:
                continue
            target = definitions.get(relocation.external_symbol)
            if target is None:
                raise FormatError(f"unresolved relocation symbol {relocation.external_symbol}")
            target_index, target_value = target
            target_offset = offsets[target_index] + target_value
            site = base + relocation.offset
            addend = struct.unpack_from("<i", text, site)[0]
            # Mach-O i386 external PC-relative fields from Clang contain the
            # location-based addend (-P-4). Add the target offset and remove
            # the source section's placement to obtain S-(P+4) after layout.
            linked_value = addend + target_offset - base
            if not -(1 << 31) <= linked_value < (1 << 31):
                raise FormatError(f"PC-relative relocation to {relocation.external_symbol} overflows i386")
            struct.pack_into("<i", text, site, linked_value)

    entry_index, entry_value = definitions[entry_name]
    return bytes(text), offsets[entry_index] + entry_value, cpu_subtype


def make_executable(text: bytes, entry_offset: int, cpu_subtype: int) -> bytes:
    if not text:
        raise FormatError("combined __text is empty")
    if entry_offset < 0 or entry_offset >= len(text):
        raise FormatError("entry symbol lies outside combined __text")
    if len(text) > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text exceeds the one-page prototype image limit")

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
    parser.add_argument("inputs", nargs="+", type=Path, help="i386 Mach-O object files")
    parser.add_argument("-o", "--output", required=True, type=Path, help="output i386 MH_EXECUTE")
    parser.add_argument("--entry", default="_main", help="external entry symbol (default: _main)")
    args = parser.parse_args()

    try:
        objects = [parse_object(path, path.read_bytes()) for path in args.inputs]
        text, entry_offset, cpu_subtype = link_objects(objects, args.entry)
        image = make_executable(text, entry_offset, cpu_subtype)
        args.output.write_bytes(image)
    except (OSError, FormatError, struct.error, UnicodeError) as error:
        print(f"i386 Mach-O prototype linker: {error}", file=sys.stderr)
        return 2

    entryoff = TEXT_FILE_OFFSET + entry_offset
    print(f"wrote {args.output} ({len(image)} bytes, entry file offset 0x{entryoff:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
