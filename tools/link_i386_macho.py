#!/usr/bin/env python3
"""Link a bounded set of i386 Mach-O text objects into a minimal MH_EXECUTE.

This bring-up linker handles one __TEXT,__text and optional initialized and
zero-fill __DATA sections per input object. It supports 32-bit
GENERIC_RELOC_VANILLA relocations to symbols in those sections, including
dyld-rebased absolute addresses. It does not link a Darwin CRT, dylibs, unwind
tables, or other Mach-O relocation kinds.
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
LC_DYLD_INFO_ONLY = 0x80000022
LC_MAIN = 0x80000028
LC_LINKER_OPTION = 0x2D
MH_DYLDLINK = 0x4
N_STAB = 0xE0
N_TYPE = 0x0E
N_UNDF = 0x00
N_SECT = 0x0E
N_EXT = 0x01
GENERIC_RELOC_VANILLA = 0
PAGE_SIZE = 0x1000
TEXT_FILE_OFFSET = 0x200
DATA_FILE_OFFSET = PAGE_SIZE
TEXT_VM_BASE = 0x1200
DATA_VM_BASE = 0x2000
S_ATTR_PURE_INSTRUCTIONS = 0x80000000
S_ATTR_SOME_INSTRUCTIONS = 0x00000400
S_ZEROFILL = 0x1


class FormatError(ValueError):
    pass


@dataclass(frozen=True)
class Relocation:
    source_kind: str
    offset: int
    external_symbol: str | None
    target_kind: str | None
    pc_relative: bool


@dataclass(frozen=True)
class ObjectSection:
    kind: str
    contents: bytes
    vm_address: int
    ordinal: int
    alignment: int
    memory_size: int


@dataclass(frozen=True)
class MachObject:
    path: Path
    text: ObjectSection
    data: ObjectSection | None
    bss: ObjectSection | None
    cpu_subtype: int
    definitions: dict[str, tuple[str, int]]
    common_symbols: dict[str, tuple[int, int]]
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
    input_sections: dict[int, tuple[str, int, int, int, int, int, int, bool]] = {}
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
                kind = None
                if (sectname, segname) == ("__text", "__TEXT"):
                    kind = "text"
                elif (sectname, segname) == ("__data", "__DATA"):
                    kind = "data"
                elif (sectname, segname) in (("__bss", "__DATA"), ("__common", "__DATA")):
                    kind = "bss"
                if kind is not None:
                    if any(existing[0] == kind for existing in input_sections.values()):
                        raise FormatError(f"{path}: multiple __{kind} sections")
                    if align_exp > 12:
                        raise FormatError(f"{path}: __{kind} alignment is too large")
                    is_zerofill = (_flags := section[8]) & 0xFF == S_ZEROFILL
                    if kind == "bss" and not is_zerofill:
                        raise FormatError(f"{path}: __bss is not marked S_ZEROFILL")
                    if kind != "bss" and is_zerofill:
                        raise FormatError(f"{path}: __{kind} unexpectedly uses S_ZEROFILL")
                    if is_zerofill and nreloc:
                        raise FormatError(f"{path}: relocations in zerofill sections are unsupported")
                    if not is_zerofill:
                        require_range(data, offset, size, f"{path}: __{kind} contents")
                    require_range(data, reloff, nreloc * 8, f"{path}: __{kind} relocations")
                    input_sections[section_ordinal] = (
                        kind, offset, size, addr, align_exp, reloff, nreloc, is_zerofill
                    )

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
    text_record = next((record for record in input_sections.values() if record[0] == "text"), None)
    data_record = next((record for record in input_sections.values() if record[0] == "data"), None)
    bss_record = next((record for record in input_sections.values() if record[0] == "bss"), None)
    if text_record is None:
        raise FormatError(f"{path}: missing __TEXT,__text")
    if symtab is None:
        raise FormatError(f"{path}: missing LC_SYMTAB")

    symoff, nsyms, stroff, strsize = symtab
    require_range(data, symoff, nsyms * 12, f"{path}: symbol table")
    strings = require_range(data, stroff, strsize, f"{path}: string table")
    symbols: list[tuple[str, int, int, int]] = []
    definitions: dict[str, tuple[str, int]] = {}
    common_symbols: dict[str, tuple[int, int]] = {}
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
                if not name:
                    raise FormatError(f"{path}: anonymous common symbol is unsupported")
                alignment = 1 << ((_desc >> 8) & 0x0F)
                previous = common_symbols.get(name, (0, 1))
                common_symbols[name] = (max(previous[0], value), max(previous[1], alignment))
            elif name:
                undefined.append(name)
        elif (n_type & N_EXT) and symbol_type == N_SECT:
            section_record = input_sections.get(n_sect)
            if section_record is None:
                raise FormatError(f"{path}: external definition {name} is outside __text/__data")
            kind, _offset, size, section_addr, _align, _reloff, _nreloc, _zerofill = section_record
            symbol_offset = value - section_addr
            if symbol_offset < 0 or symbol_offset > size:
                raise FormatError(f"{path}: external definition {name} lies outside __{kind}")
            if name in definitions:
                raise FormatError(f"{path}: duplicate external definition {name}")
            definitions[name] = (kind, symbol_offset)

    relocations: list[Relocation] = []
    for source_ordinal, source_record in input_sections.items():
        source_kind, _source_offset, source_size, _source_addr, _source_align, reloff, nreloc, is_zerofill = source_record
        if is_zerofill and nreloc:
            raise FormatError(f"{path}: relocations in zerofill sections are unsupported")
        relocation_sites: set[int] = set()
        for reloc_index in range(nreloc):
            address, info = struct.unpack_from("<iI", data, reloff + reloc_index * 8)
            scattered = (address & 0x80000000) != 0
            symbolnum = info & 0x00FFFFFF
            pcrel = (info >> 24) & 1
            length = (info >> 25) & 3
            external = (info >> 27) & 1
            reloc_type = (info >> 28) & 0xF
            if (scattered or address < 0 or address + 4 > source_size or
                    length != 2 or reloc_type != GENERIC_RELOC_VANILLA):
                raise FormatError(f"{path}: unsupported __{source_kind} relocation at {address:#x}")
            if any(site < address + 4 and address < site + 4 for site in relocation_sites):
                raise FormatError(f"{path}: overlapping __{source_kind} relocation fields")
            relocation_sites.add(address)

            if external:
                if symbolnum >= len(symbols):
                    raise FormatError(f"{path}: external relocation symbol index is invalid")
                symbol_name, symbol_type, _symbol_section, _symbol_value = symbols[symbolnum]
                if not symbol_name or not (symbol_type & N_EXT):
                    raise FormatError(f"{path}: external relocation does not name an external symbol")
                relocations.append(Relocation(source_kind, address, symbol_name, None, bool(pcrel)))
            else:
                target_record = input_sections.get(symbolnum)
                if target_record is None:
                    raise FormatError(f"{path}: local relocation targets an unsupported section")
                target_kind = target_record[0]
                if not pcrel and (target_record[2] == 0):
                    raise FormatError(f"{path}: absolute relocation targets an empty section")
                relocations.append(Relocation(source_kind, address, None, target_kind, bool(pcrel)))

    def make_section(record: tuple[str, int, int, int, int, int, int]) -> ObjectSection:
        kind, offset, size, addr, align_exp, _reloff, _nreloc, is_zerofill = record
        ordinal = next(number for number, value in input_sections.items() if value is record)
        contents = b"" if is_zerofill else data[offset : offset + size]
        return ObjectSection(kind, contents, addr, ordinal, 1 << align_exp, size)

    text_section = make_section(text_record)
    data_section = make_section(data_record) if data_record is not None else None
    bss_section = make_section(bss_record) if bss_record is not None else None

    return MachObject(
        path=path,
        text=text_section,
        data=data_section,
        bss=bss_section,
        cpu_subtype=cpu_subtype,
        definitions=definitions,
        common_symbols=common_symbols,
        undefined_symbols=tuple(undefined),
        relocations=tuple(relocations),
    )


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def link_objects(
    objects: list[MachObject], entry_name: str
    ) -> tuple[bytes, bytes, int, int, int, int, list[tuple[int, int]]]:
    if not objects:
        raise FormatError("at least one input object is required")
    cpu_subtype = objects[0].cpu_subtype
    if any(obj.cpu_subtype != cpu_subtype for obj in objects):
        raise FormatError("input objects have different i386 CPU subtypes")

    text_offsets: list[int] = []
    data_offsets: list[int] = []
    bss_offsets: list[int] = []
    text_size = 0
    data_size = 0
    bss_size = 0
    text_align_exp = 0
    data_align_exp = 0
    bss_align_exp = 0
    for obj in objects:
        text_size = align_up(text_size, obj.text.alignment)
        text_offsets.append(text_size)
        text_size += len(obj.text.contents)
        text_align_exp = max(text_align_exp, obj.text.alignment.bit_length() - 1)
        if obj.data is None:
            data_offsets.append(0)
        else:
            data_size = align_up(data_size, obj.data.alignment)
            data_offsets.append(data_size)
            data_size += len(obj.data.contents)
            data_align_exp = max(data_align_exp, obj.data.alignment.bit_length() - 1)
        if obj.bss is None:
            bss_offsets.append(0)
        else:
            bss_size = align_up(bss_size, obj.bss.alignment)
            bss_offsets.append(bss_size)
            bss_size += obj.bss.memory_size
            bss_align_exp = max(bss_align_exp, obj.bss.alignment.bit_length() - 1)
            data_align_exp = max(data_align_exp, obj.bss.alignment.bit_length() - 1)
    if text_size == 0 or text_size > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text exceeds the one-page prototype image limit")
    bss_start = align_up(data_size, 1 << bss_align_exp) if bss_size else data_size
    combined_data_size = bss_start + bss_size
    if combined_data_size > PAGE_SIZE:
        raise FormatError("combined __data and zero-fill storage exceed one page")
    if text_align_exp > 9:
        raise FormatError("combined __text alignment exceeds the fixed 0x200 file offset")

    definitions: dict[str, tuple[int, str, int]] = {}
    undefined: set[str] = set()
    for index, obj in enumerate(objects):
        undefined.update(obj.undefined_symbols)
        for name, (kind, value) in obj.definitions.items():
            if name in definitions:
                raise FormatError(f"duplicate external definition {name}")
            definitions[name] = (index, kind, value)
    common_offsets: dict[str, int] = {}
    common_sizes: dict[str, int] = {}
    common_alignments: dict[str, int] = {}
    for obj in objects:
        for name, (size, alignment) in sorted(obj.common_symbols.items()):
            if name in definitions:
                continue
            common_sizes[name] = max(common_sizes.get(name, 0), size)
            common_alignments[name] = max(common_alignments.get(name, 1), alignment)
    for name in sorted(common_sizes):
        bss_size = align_up(bss_size, common_alignments[name])
        common_offsets[name] = bss_size
        bss_size += common_sizes[name]
    for name, offset in common_offsets.items():
        definitions[name] = (0, "bss", offset)
    bss_start = align_up(data_size, 1 << max(bss_align_exp, 0)) if bss_size else data_size
    combined_data_size = bss_start + bss_size
    if combined_data_size > PAGE_SIZE:
        raise FormatError("combined __data and common zero-fill storage exceed one page")
    missing = sorted(undefined - definitions.keys())
    if missing:
        raise FormatError("unresolved external symbols: " + ", ".join(missing))
    if entry_name not in definitions:
        raise FormatError(f"entry symbol {entry_name} was not found in input objects")

    text = bytearray(text_size)
    data = bytearray(combined_data_size)
    preferred_vm = {"text": TEXT_VM_BASE, "data": DATA_VM_BASE}
    section_placements = {"text": text_offsets, "data": data_offsets, "bss": bss_offsets}
    section_sizes = {"text": text_size, "data": data_size, "bss": bss_size}
    output_sections = {"text": text, "data": data}
    rebase_sites: set[tuple[int, int]] = set()

    for index, obj in enumerate(objects):
        text_base = text_offsets[index]
        text[text_base : text_base + len(obj.text.contents)] = obj.text.contents
        if obj.data is not None:
            data_base = data_offsets[index]
            data[data_base : data_base + len(obj.data.contents)] = obj.data.contents

        for relocation in obj.relocations:
            source_base = section_placements[relocation.source_kind][index]
            source_section = obj.text if relocation.source_kind == "text" else obj.data
            if source_section is None:
                raise FormatError("relocation source section is missing")
            source_output_vm = preferred_vm[relocation.source_kind] + source_base
            source_site = source_base + relocation.offset
            source_bytes = output_sections[relocation.source_kind]

            if relocation.external_symbol is not None:
                target = definitions.get(relocation.external_symbol)
                if target is None:
                    raise FormatError(f"unresolved relocation symbol {relocation.external_symbol}")
                target_index, target_kind, target_value = target
                target_base = section_placements[target_kind][target_index]
                target_storage_base = bss_start if target_kind == "bss" else 0
                target_vm = preferred_vm["data"] + target_storage_base + target_base + target_value if target_kind == "bss" else preferred_vm[target_kind] + target_base + target_value
                if target_value >= section_sizes[target_kind]:
                    raise FormatError(f"relocation target {relocation.external_symbol} is outside its section")
                if relocation.pc_relative:
                    addend = struct.unpack_from("<i", source_bytes, source_site)[0]
                    linked_value = addend + target_vm - source_output_vm
                    if not -(1 << 31) <= linked_value < (1 << 31):
                        raise FormatError(
                            f"PC-relative relocation to {relocation.external_symbol} overflows i386"
                        )
                    struct.pack_into("<i", source_bytes, source_site, linked_value)
                else:
                    addend = struct.unpack_from("<i", source_bytes, source_site)[0]
                    linked_value = target_vm + addend
                    if not 0 <= linked_value <= 0xFFFFFFFF:
                        raise FormatError(
                            f"absolute relocation to {relocation.external_symbol} overflows i386"
                        )
                    struct.pack_into("<I", source_bytes, source_site, linked_value)
                    segment_index = 0 if relocation.source_kind == "text" else 1
                    segment_offset = (
                        (TEXT_FILE_OFFSET if relocation.source_kind == "text" else 0) +
                        source_site
                    )
                    rebase_sites.add((segment_index, segment_offset))
            else:
                target_kind = relocation.target_kind
                if target_kind is None:
                    raise FormatError("local relocation has no target section")
                target_section = (
                    obj.text if target_kind == "text" else
                    obj.bss if target_kind == "bss" else obj.data
                )
                if target_section is None:
                    raise FormatError("local relocation targets a missing section")
                target_base = section_placements[target_kind][index]
                target_output_vm = (
                    preferred_vm["data"] + bss_start + target_base
                    if target_kind == "bss" else preferred_vm[target_kind] + target_base
                )
                if relocation.pc_relative:
                    addend = struct.unpack_from("<i", source_bytes, source_site)[0]
                    target_delta = target_output_vm - target_section.vm_address
                    source_delta = source_output_vm - source_section.vm_address
                    linked_value = addend + target_delta - source_delta
                    if not -(1 << 31) <= linked_value < (1 << 31):
                        raise FormatError("local PC-relative relocation overflows i386")
                    struct.pack_into("<i", source_bytes, source_site, linked_value)
                else:
                    old_value = struct.unpack_from("<I", source_bytes, source_site)[0]
                    addend = old_value - target_section.vm_address
                    if addend >= (1 << 31):
                        addend -= 1 << 32
                    linked_value = target_output_vm + addend
                    if not 0 <= linked_value <= 0xFFFFFFFF:
                        raise FormatError("local absolute relocation overflows i386")
                    struct.pack_into("<I", source_bytes, source_site, linked_value)
                    segment_index = 0 if relocation.source_kind == "text" else 1
                    segment_offset = (
                        (TEXT_FILE_OFFSET if relocation.source_kind == "text" else 0) +
                        source_site
                    )
                    rebase_sites.add((segment_index, segment_offset))

    entry_index, entry_kind, entry_value = definitions[entry_name]
    if entry_kind != "text":
        raise FormatError(f"entry symbol {entry_name} is not in __text")
    return (
        bytes(text), bytes(data), text_offsets[entry_index] + entry_value,
        cpu_subtype, text_align_exp, data_align_exp, sorted(rebase_sites),
    )


def encode_uleb(value: int) -> bytes:
    result = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        result.append(byte | (0x80 if value else 0))
        if value == 0:
            return bytes(result)


def make_segment_command(
    name: bytes, vm_address: int, file_offset: int, file_size: int,
    max_protection: int, initial_protection: int,
    section_name: bytes, section_segment: bytes, section_vm_address: int,
    section_size: int, section_file_offset: int, section_alignment: int,
    section_flags: int = 0,
) -> bytes:
    command_size = 56 + 68
    command = struct.pack(
        "<II16sIIIIiiII", LC_SEGMENT, command_size, name, vm_address,
        PAGE_SIZE, file_offset, file_size, max_protection,
        initial_protection, 1, 0,
    )
    section = struct.pack(
        "<16s16sIIIIIIIII", section_name, section_segment, section_vm_address,
        section_size, section_file_offset, section_alignment, 0, 0,
        section_flags, 0, 0,
    )
    return command + section


def make_executable(
    text: bytes, data: bytes, entry_offset: int, cpu_subtype: int,
    text_align_exp: int, data_align_exp: int,
    rebase_sites: list[tuple[int, int]],
) -> bytes:
    if not text:
        raise FormatError("combined __text is empty")
    if entry_offset < 0 or entry_offset >= len(text):
        raise FormatError("entry symbol lies outside combined __text")
    if len(text) > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text exceeds the one-page prototype image limit")
    if len(data) > PAGE_SIZE:
        raise FormatError("combined __data exceeds the one-page prototype image limit")

    commands = [make_segment_command(
        b"__TEXT", 0x1000, 0, PAGE_SIZE, 7, 5,
        b"__text", b"__TEXT", TEXT_VM_BASE, len(text), TEXT_FILE_OFFSET,
        text_align_exp, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
    )]
    if data:
        commands.append(make_segment_command(
            b"__DATA", DATA_VM_BASE, DATA_FILE_OFFSET, PAGE_SIZE, 7, 3,
            b"__data", b"__DATA", DATA_VM_BASE, len(data), DATA_FILE_OFFSET,
            data_align_exp,
        ))

    rebase_stream = bytearray()
    if rebase_sites:
        for segment_index, segment_offset in rebase_sites:
            rebase_stream.extend((0x12, 0x20 | segment_index))
            rebase_stream.extend(encode_uleb(segment_offset))
            rebase_stream.append(0x51)
        rebase_stream.append(0)
        rebase_offset = (DATA_FILE_OFFSET + PAGE_SIZE) if data else PAGE_SIZE
        dyld_info = struct.pack(
            "<12I", LC_DYLD_INFO_ONLY, 48, rebase_offset, len(rebase_stream),
            0, 0, 0, 0, 0, 0, 0, 0,
        )
        commands.append(dyld_info)

    main = struct.pack("<IIQQ", LC_MAIN, 24, TEXT_FILE_OFFSET + entry_offset, 0)
    commands.append(main)
    sizeofcmds = sum(map(len, commands))
    flags = MH_DYLDLINK if rebase_sites else 0
    header = struct.pack(
        "<IiiIIII", MH_MAGIC, CPU_TYPE_I386, cpu_subtype, MH_EXECUTE,
        len(commands), sizeofcmds, flags,
    )

    image_size = (DATA_FILE_OFFSET + PAGE_SIZE) if data else PAGE_SIZE
    if rebase_sites:
        image_size = max(image_size, rebase_offset + len(rebase_stream))
    image = bytearray(image_size)
    image[:28] = header
    cursor = 28
    for command in commands:
        image[cursor : cursor + len(command)] = command
        cursor += len(command)
    if cursor > TEXT_FILE_OFFSET:
        raise FormatError("Mach-O load commands overlap the __text file offset")
    image[TEXT_FILE_OFFSET : TEXT_FILE_OFFSET + len(text)] = text
    if data:
        image[DATA_FILE_OFFSET : DATA_FILE_OFFSET + len(data)] = data
    if rebase_sites:
        image[rebase_offset : rebase_offset + len(rebase_stream)] = rebase_stream
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="i386 Mach-O object files")
    parser.add_argument("-o", "--output", required=True, type=Path, help="output i386 MH_EXECUTE")
    parser.add_argument("--entry", default="_main", help="external entry symbol (default: _main)")
    args = parser.parse_args()

    try:
        objects = [parse_object(path, path.read_bytes()) for path in args.inputs]
        (text, data, entry_offset, cpu_subtype, text_align_exp,
         data_align_exp, rebase_sites) = link_objects(objects, args.entry)
        image = make_executable(
            text, data, entry_offset, cpu_subtype, text_align_exp,
            data_align_exp, rebase_sites,
        )
        args.output.write_bytes(image)
    except (OSError, FormatError, struct.error, UnicodeError) as error:
        print(f"i386 Mach-O prototype linker: {error}", file=sys.stderr)
        return 2

    entryoff = TEXT_FILE_OFFSET + entry_offset
    print(f"wrote {args.output} ({len(image)} bytes, entry file offset 0x{entryoff:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
