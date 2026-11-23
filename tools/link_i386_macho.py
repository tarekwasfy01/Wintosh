#!/usr/bin/env python3
"""Link bounded i386 Mach-O text and data objects into a minimal MH_EXECUTE.

This bring-up linker handles one __TEXT,__text, optional __TEXT,__cstring and
__const, initialized/zero-fill __DATA, indirect pointer section, and
__DATA,__mod_init_func/__mod_term_func sections per object. Clang's
__TEXT,__StaticInit contribution is merged into executable text. Const bytes
are emitted in a read-only __TEXT,__const output section. It supports bounded
32-bit GENERIC_RELOC_VANILLA and paired scattered
GENERIC_RELOC_SECTDIFF/LOCAL_SECTDIFF relocations, plus dyld rebases. It does
not link a Darwin CRT, dylibs, unwind tables, or other relocation kinds.
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
LC_DYSYMTAB = 0xB
LC_MAIN = 0x80000028
LC_LINKER_OPTION = 0x2D
MH_DYLDLINK = 0x4
N_STAB = 0xE0
N_TYPE = 0x0E
N_UNDF = 0x00
N_SECT = 0x0E
N_EXT = 0x01
GENERIC_RELOC_VANILLA = 0
GENERIC_RELOC_PAIR = 1
GENERIC_RELOC_SECTDIFF = 2
GENERIC_RELOC_LOCAL_SECTDIFF = 4
PAGE_SIZE = 0x1000
TEXT_FILE_OFFSET = 0x400
DATA_FILE_OFFSET = PAGE_SIZE
TEXT_VM_BASE = 0x1400
DATA_VM_BASE = 0x2000
S_ATTR_PURE_INSTRUCTIONS = 0x80000000
S_ATTR_SOME_INSTRUCTIONS = 0x00000400
S_ZEROFILL = 0x1
S_NON_LAZY_SYMBOL_POINTERS = 0x6
S_MOD_INIT_FUNC_POINTERS = 0x9
S_MOD_TERM_FUNC_POINTERS = 0xA
S_CSTRING_LITERALS = 0x2


class FormatError(ValueError):
    pass


@dataclass(frozen=True)
class Relocation:
    source_kind: str
    offset: int
    external_symbol: str | None
    target_kind: str | None
    pc_relative: bool
    target_offset: int = 0
    minuend_kind: str | None = None
    minuend_offset: int = 0


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
    static_init: ObjectSection | None
    const: ObjectSection | None
    cstring: ObjectSection | None
    data: ObjectSection | None
    bss: ObjectSection | None
    pointers: ObjectSection | None
    initializers: ObjectSection | None
    terminators: ObjectSection | None
    pointer_symbols: tuple[str, ...]
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
    input_sections: dict[int, tuple[str, int, int, int, int, int, int, int, bool]] = {}
    symtab: tuple[int, int, int, int] | None = None
    dysymtab: tuple[int, int] | None = None
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
                addr, size, offset, align_exp, reloff, nreloc, _flags, reserved1, _r2 = section[2:]
                section_ordinal += 1
                kind = None
                if (sectname, segname) == ("__text", "__TEXT"):
                    kind = "text"
                elif (sectname, segname) == ("__StaticInit", "__TEXT"):
                    kind = "static_init"
                elif (sectname, segname) == ("__cstring", "__TEXT"):
                    kind = "cstring"
                elif (sectname, segname) == ("__const", "__TEXT"):
                    kind = "const"
                elif (sectname, segname) == ("__data", "__DATA"):
                    kind = "data"
                elif (sectname, segname) in (("__bss", "__DATA"), ("__common", "__DATA")):
                    kind = "bss"
                elif (sectname, segname) == ("__pointers", "__IMPORT"):
                    kind = "ptr"
                elif (sectname, segname) == ("__mod_init_func", "__DATA"):
                    kind = "init"
                elif (sectname, segname) == ("__mod_term_func", "__DATA"):
                    kind = "term"
                if kind is not None:
                    if any(existing[0] == kind for existing in input_sections.values()):
                        raise FormatError(f"{path}: multiple __{kind} sections")
                    if align_exp > 12:
                        raise FormatError(f"{path}: __{kind} alignment is too large")
                    is_zerofill = (_flags := section[8]) & 0xFF == S_ZEROFILL
                    if kind == "bss" and not is_zerofill:
                        raise FormatError(f"{path}: __bss is not marked S_ZEROFILL")
                    if kind == "ptr" and (_flags & 0xFF) != S_NON_LAZY_SYMBOL_POINTERS:
                        raise FormatError(f"{path}: __IMPORT,__pointers has an unsupported section type")
                    if kind == "init" and (_flags & 0xFF) != S_MOD_INIT_FUNC_POINTERS:
                        raise FormatError(f"{path}: __DATA,__mod_init_func has an unsupported section type")
                    if kind == "term" and (_flags & 0xFF) != S_MOD_TERM_FUNC_POINTERS:
                        raise FormatError(f"{path}: __DATA,__mod_term_func has an unsupported section type")
                    if kind != "bss" and is_zerofill:
                        raise FormatError(f"{path}: __{kind} unexpectedly uses S_ZEROFILL")
                    if is_zerofill and nreloc:
                        raise FormatError(f"{path}: relocations in zerofill sections are unsupported")
                    if not is_zerofill:
                        require_range(data, offset, size, f"{path}: __{kind} contents")
                    require_range(data, reloff, nreloc * 8, f"{path}: __{kind} relocations")
                    input_sections[section_ordinal] = (
                        kind, offset, size, addr, align_exp, reloff, nreloc,
                        reserved1, is_zerofill
                    )

        elif cmd == LC_SYMTAB:
            if cmdsize != 24:
                raise FormatError(f"{path}: invalid LC_SYMTAB command size")
            _cmd, _size, symoff, nsyms, stroff, strsize = struct.unpack_from(
                "<IIIIII", command
            )
            symtab = (symoff, nsyms, stroff, strsize)

        elif cmd == LC_DYSYMTAB:
            if cmdsize != 80:
                raise FormatError(f"{path}: invalid LC_DYSYMTAB command size")
            fields = struct.unpack_from("<20I", command)
            dysymtab = (fields[14], fields[15])

        elif cmd == LC_LINKER_OPTION and cmdsize < 12:
            raise FormatError(f"{path}: short LC_LINKER_OPTION command")

        cursor += cmdsize

    if cursor != commands_end:
        raise FormatError(f"{path}: load-command sizes do not match sizeofcmds")
    text_record = next((record for record in input_sections.values() if record[0] == "text"), None)
    static_init_record = next((record for record in input_sections.values() if record[0] == "static_init"), None)
    const_record = next((record for record in input_sections.values() if record[0] == "const"), None)
    cstring_record = next((record for record in input_sections.values() if record[0] == "cstring"), None)
    data_record = next((record for record in input_sections.values() if record[0] == "data"), None)
    bss_record = next((record for record in input_sections.values() if record[0] == "bss"), None)
    pointer_record = next((record for record in input_sections.values() if record[0] == "ptr"), None)
    initializer_record = next((record for record in input_sections.values() if record[0] == "init"), None)
    terminator_record = next((record for record in input_sections.values() if record[0] == "term"), None)
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
            kind, _offset, size, section_addr, _align, _reloff, _nreloc, _reserved1, _zerofill = section_record
            symbol_offset = value - section_addr
            if symbol_offset < 0 or symbol_offset > size:
                raise FormatError(f"{path}: external definition {name} lies outside __{kind}")
            if name in definitions:
                raise FormatError(f"{path}: duplicate external definition {name}")
            definitions[name] = (kind, symbol_offset)

    relocations: list[Relocation] = []
    for source_ordinal, source_record in input_sections.items():
        source_kind, _source_offset, source_size, _source_addr, _source_align, reloff, nreloc, _reserved1, is_zerofill = source_record
        if is_zerofill and nreloc:
            raise FormatError(f"{path}: relocations in zerofill sections are unsupported")
        relocation_sites: set[int] = set()
        reloc_index = 0
        while reloc_index < nreloc:
            word0, word1 = struct.unpack_from("<II", data, reloff + reloc_index * 8)
            scattered = (word0 & 0x80000000) != 0
            if scattered:
                address = word0 & 0x00FFFFFF
                pcrel = (word0 >> 30) & 1
                length = (word0 >> 28) & 3
                reloc_type = (word0 >> 24) & 0xF
                info = 0
            else:
                address = struct.unpack_from("<i", data, reloff + reloc_index * 8)[0]
                info = word1
                pcrel = (info >> 24) & 1
                length = (info >> 25) & 3
                reloc_type = (info >> 28) & 0xF
            symbolnum = info & 0x00FFFFFF
            external = (info >> 27) & 1
            if address + 4 > source_size or length != 2:
                raise FormatError(f"{path}: unsupported __{source_kind} relocation at {address:#x}")
            if scattered and reloc_type in (GENERIC_RELOC_SECTDIFF, GENERIC_RELOC_LOCAL_SECTDIFF):
                if reloc_index + 1 >= nreloc:
                    raise FormatError(f"{path}: section-difference relocation is missing its pair")
                pair_word0, pair_value = struct.unpack_from("<II", data, reloff + (reloc_index + 1) * 8)
                pair_scattered = (pair_word0 & 0x80000000) != 0
                pair_type = (pair_word0 >> 24) & 0xF
                pair_length = (pair_word0 >> 28) & 3
                if not pair_scattered or pair_type != GENERIC_RELOC_PAIR or pair_length != 2:
                    raise FormatError(f"{path}: malformed section-difference relocation pair")

                def section_address(value: int) -> tuple[str, int]:
                    for _ordinal, record in input_sections.items():
                        kind, _off, size, vm_address, _align, _ro, _nr, _reserved, _zero = record
                        if vm_address <= value < vm_address + size:
                            return kind, value - vm_address
                    raise FormatError(f"{path}: scattered relocation address {value:#x} is outside supported sections")

                target_kind, target_offset = section_address(word1)
                minuend_kind, minuend_offset = section_address(pair_value)
                if any(site < address + 4 and address < site + 4 for site in relocation_sites):
                    raise FormatError(f"{path}: overlapping __{source_kind} relocation fields")
                relocation_sites.add(address)
                relocations.append(Relocation(
                    source_kind, address, None, target_kind, False,
                    target_offset, minuend_kind, minuend_offset,
                ))
                reloc_index += 2
                continue
            if scattered or reloc_type != GENERIC_RELOC_VANILLA:
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
            reloc_index += 1

    def make_section(record: tuple[str, int, int, int, int, int, int, int, bool]) -> ObjectSection:
        kind, offset, size, addr, align_exp, _reloff, _nreloc, _reserved1, is_zerofill = record
        ordinal = next(number for number, value in input_sections.items() if value is record)
        contents = b"" if is_zerofill else data[offset : offset + size]
        return ObjectSection(kind, contents, addr, ordinal, 1 << align_exp, size)

    text_section = make_section(text_record)
    static_init_section = make_section(static_init_record) if static_init_record is not None else None
    const_section = make_section(const_record) if const_record is not None else None
    cstring_section = make_section(cstring_record) if cstring_record is not None else None
    data_section = make_section(data_record) if data_record is not None else None
    bss_section = make_section(bss_record) if bss_record is not None else None
    pointer_section = make_section(pointer_record) if pointer_record is not None else None
    initializer_section = make_section(initializer_record) if initializer_record is not None else None
    terminator_section = make_section(terminator_record) if terminator_record is not None else None
    if initializer_record is not None and initializer_record[2] % 4 != 0:
        raise FormatError(f"{path}: __DATA,__mod_init_func is not a 32-bit pointer array")
    if terminator_record is not None and terminator_record[2] % 4 != 0:
        raise FormatError(f"{path}: __DATA,__mod_term_func is not a 32-bit pointer array")
    pointer_symbols: tuple[str, ...] = ()
    if pointer_record is not None:
        if dysymtab is None:
            raise FormatError(f"{path}: __IMPORT,__pointers requires LC_DYSYMTAB")
        indirect_offset, indirect_count = dysymtab
        indirect_data = require_range(data, indirect_offset, indirect_count * 4,
                                      f"{path}: indirect symbol table")
        section_entries = pointer_record[2] // 4
        if pointer_record[2] % 4 != 0:
            raise FormatError(f"{path}: pointer section size is not pointer-aligned")
        if pointer_record[7] + section_entries > indirect_count:
            raise FormatError(f"{path}: pointer section exceeds indirect symbol table")
        names = []
        for slot in range(section_entries):
            symbol_index = struct.unpack_from("<I", indirect_data, (pointer_record[7] + slot) * 4)[0]
            if symbol_index & 0xC0000000:
                raise FormatError(f"{path}: local/absolute indirect pointers are unsupported")
            if symbol_index >= len(symbols) or not symbols[symbol_index][0]:
                raise FormatError(f"{path}: invalid indirect pointer symbol index")
            names.append(symbols[symbol_index][0])
        pointer_symbols = tuple(names)

    return MachObject(
        path=path,
        text=text_section,
        static_init=static_init_section,
        const=const_section,
        cstring=cstring_section,
        data=data_section,
        bss=bss_section,
        pointers=pointer_section,
        initializers=initializer_section,
        terminators=terminator_section,
        pointer_symbols=pointer_symbols,
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
    ) -> tuple[bytes, bytes, int, bytes, int, int, int, int, int,
               list[tuple[int, int]], list[tuple[int, str]]]:
    if not objects:
        raise FormatError("at least one input object is required")
    cpu_subtype = objects[0].cpu_subtype
    if any(obj.cpu_subtype != cpu_subtype for obj in objects):
        raise FormatError("input objects have different i386 CPU subtypes")

    text_offsets: list[int] = []
    data_offsets: list[int] = []
    const_offsets: list[int] = []
    cstring_offsets: list[int] = []
    bss_offsets: list[int] = []
    pointer_offsets: list[int] = []
    initializer_offsets: list[int] = []
    terminator_offsets: list[int] = []
    static_init_offsets: list[int] = []
    text_size = 0
    data_size = 0
    const_size = 0
    cstring_size = 0
    bss_size = 0
    text_align_exp = 0
    data_align_exp = 0
    const_align_exp = 0
    cstring_align_exp = 0
    bss_align_exp = 0
    pointer_align_exp = 0
    initializer_align_exp = 0
    initializer_size = 0
    terminator_size = 0
    terminator_align_exp = 0
    static_init_size = 0
    static_init_align_exp = 0
    for obj in objects:
        text_size = align_up(text_size, obj.text.alignment)
        text_offsets.append(text_size)
        text_size += len(obj.text.contents)
        text_align_exp = max(text_align_exp, obj.text.alignment.bit_length() - 1)
        if obj.static_init is None:
            static_init_offsets.append(0)
        else:
            static_init_size = align_up(static_init_size, obj.static_init.alignment)
            static_init_offsets.append(static_init_size)
            static_init_size += len(obj.static_init.contents)
            static_init_align_exp = max(
                static_init_align_exp, obj.static_init.alignment.bit_length() - 1
            )
        if obj.data is None:
            data_offsets.append(0)
        else:
            data_size = align_up(data_size, obj.data.alignment)
            data_offsets.append(data_size)
            data_size += len(obj.data.contents)
            data_align_exp = max(data_align_exp, obj.data.alignment.bit_length() - 1)
        if obj.const is None:
            const_offsets.append(0)
        else:
            const_size = align_up(const_size, obj.const.alignment)
            const_offsets.append(const_size)
            const_size += len(obj.const.contents)
            const_align_exp = max(const_align_exp, obj.const.alignment.bit_length() - 1)
        if obj.cstring is None:
            cstring_offsets.append(0)
        else:
            cstring_size = align_up(cstring_size, obj.cstring.alignment)
            cstring_offsets.append(cstring_size)
            cstring_size += len(obj.cstring.contents)
            cstring_align_exp = max(cstring_align_exp, obj.cstring.alignment.bit_length() - 1)
        if obj.bss is None:
            bss_offsets.append(0)
        else:
            bss_size = align_up(bss_size, obj.bss.alignment)
            bss_offsets.append(bss_size)
            bss_size += obj.bss.memory_size
            bss_align_exp = max(bss_align_exp, obj.bss.alignment.bit_length() - 1)
            data_align_exp = max(data_align_exp, obj.bss.alignment.bit_length() - 1)
        if obj.pointers is None:
            pointer_offsets.append(0)
        else:
            pointer_align_exp = max(pointer_align_exp, obj.pointers.alignment.bit_length() - 1)
            pointer_offsets.append(0)
        if obj.initializers is None:
            initializer_offsets.append(0)
        else:
            initializer_size = align_up(initializer_size, max(4, obj.initializers.alignment))
            initializer_offsets.append(initializer_size)
            initializer_size += len(obj.initializers.contents)
            initializer_align_exp = max(
                initializer_align_exp, obj.initializers.alignment.bit_length() - 1
            )
        if obj.terminators is None:
            terminator_offsets.append(0)
        else:
            terminator_size = align_up(terminator_size, max(4, obj.terminators.alignment))
            terminator_offsets.append(terminator_size)
            terminator_size += len(obj.terminators.contents)
            terminator_align_exp = max(
                terminator_align_exp, obj.terminators.alignment.bit_length() - 1
            )
    if text_size == 0:
        raise FormatError("combined __text exceeds the one-page prototype image limit")
    static_init_start = align_up(text_size, 1 << static_init_align_exp) if static_init_size else text_size
    static_init_offsets = [static_init_start + offset for offset in static_init_offsets]
    text_size = static_init_start + static_init_size
    text_align_exp = max(text_align_exp, static_init_align_exp)
    const_start = 0
    bss_start = align_up(data_size, 1 << bss_align_exp) if bss_size else data_size
    combined_data_size = bss_start + bss_size
    if combined_data_size > PAGE_SIZE:
        raise FormatError("combined __data and zero-fill storage exceed one page")
    if text_align_exp > 10:
        raise FormatError("combined __text alignment exceeds the fixed 0x400 file offset")

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
        for name in obj.undefined_symbols:
            if name in {"___dso_handle", "__dso_handle"} and name not in definitions:
                common_sizes[name] = max(common_sizes.get(name, 0), 4)
                common_alignments[name] = max(common_alignments.get(name, 1), 4)
    for alignment in common_alignments.values():
        bss_align_exp = max(bss_align_exp, alignment.bit_length() - 1)
        data_align_exp = max(data_align_exp, alignment.bit_length() - 1)
    for name in sorted(common_sizes):
        bss_size = align_up(bss_size, common_alignments[name])
        common_offsets[name] = bss_size
        bss_size += common_sizes[name]
    for name, offset in common_offsets.items():
        definitions[name] = (0, "bss", offset)
    bss_start = align_up(data_size, 1 << max(bss_align_exp, 0)) if bss_size else data_size
    pointer_start = align_up(bss_start + bss_size, 1 << pointer_align_exp)
    data_align_exp = max(data_align_exp, pointer_align_exp)
    pointer_size = 0
    for index, obj in enumerate(objects):
        if obj.pointers is not None:
            pointer_offsets[index] = pointer_size
            pointer_size = align_up(pointer_size, obj.pointers.alignment)
            pointer_offsets[index] = pointer_size
            pointer_size += obj.pointers.memory_size
    combined_data_size = pointer_start + pointer_size if pointer_size else bss_start + bss_size
    if combined_data_size > PAGE_SIZE:
        raise FormatError("combined __data and common zero-fill storage exceed one page")
    pointer_import_names = {name for obj in objects for name in obj.pointer_symbols}
    function_import_names = {
        relocation.external_symbol
        for obj in objects for relocation in obj.relocations
        if relocation.external_symbol is not None and relocation.pc_relative
    }
    import_names = (pointer_import_names | function_import_names) - definitions.keys()
    missing = sorted(undefined - definitions.keys() - import_names)
    if missing:
        raise FormatError("unresolved external symbols: " + ", ".join(missing))
    stub_offsets: dict[str, int] = {}
    for name in sorted(function_import_names & import_names):
        text_size = align_up(text_size, 4)
        stub_offsets[name] = text_size
        text_size += 6
    if text_size > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text and import stubs exceed one page")
    const_start = align_up(TEXT_FILE_OFFSET + text_size, 1 << const_align_exp) - TEXT_FILE_OFFSET
    cstring_start = (
        align_up(TEXT_FILE_OFFSET + const_start + const_size, 1 << cstring_align_exp) - TEXT_FILE_OFFSET
        if cstring_size else 0
    )
    readonly_end = max(
        text_size, const_start + const_size,
        cstring_start + cstring_size if cstring_size else 0,
    )
    if readonly_end > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text, __const, and __cstring exceed the one-page text image limit")
    import_slot_offsets: dict[str, int] = {}
    for index, obj in enumerate(objects):
        if obj.pointers is None:
            continue
        for slot, name in enumerate(obj.pointer_symbols):
            import_slot_offsets.setdefault(name,
                pointer_offsets[index] + slot * 4)
    for name in sorted(import_names):
        if name not in import_slot_offsets:
            pointer_size = align_up(pointer_size, 4)
            import_slot_offsets[name] = pointer_size
            pointer_size += 4
    pointer_start = align_up(bss_start + bss_size, 4 if import_slot_offsets else 1 << pointer_align_exp)
    data_align_exp = max(data_align_exp, 2 if import_slot_offsets else pointer_align_exp)
    data_end = pointer_start + pointer_size if pointer_size else bss_start + bss_size
    initializer_start = (
        align_up(data_end, max(4, 1 << initializer_align_exp)) if initializer_size else data_end
    )
    after_initializers = initializer_start + initializer_size if initializer_size else data_end
    terminator_start = (
        align_up(after_initializers, max(4, 1 << terminator_align_exp)) if terminator_size else after_initializers
    )
    data_align_exp = max(data_align_exp, initializer_align_exp, terminator_align_exp)
    combined_data_size = terminator_start + terminator_size if terminator_size else after_initializers
    if combined_data_size > PAGE_SIZE:
        raise FormatError("combined __DATA and import pointer storage exceed one page")
    if entry_name not in definitions:
        raise FormatError(f"entry symbol {entry_name} was not found in input objects")

    text = bytearray(text_size)
    const = bytearray(const_size)
    cstring = bytearray(cstring_size)
    data = bytearray(combined_data_size)
    preferred_vm = {"text": TEXT_VM_BASE, "data": DATA_VM_BASE}
    section_placements = {
        "text": text_offsets, "data": data_offsets, "const": const_offsets,
        "cstring": cstring_offsets,
        "static_init": static_init_offsets,
        "bss": bss_offsets, "ptr": pointer_offsets, "init": initializer_offsets,
        "term": terminator_offsets,
    }
    section_sizes = {
        "text": text_size, "data": data_size, "const": const_size,
        "cstring": cstring_size,
        "static_init": static_init_size,
        "bss": bss_size, "ptr": pointer_size, "init": initializer_size,
        "term": terminator_size,
    }
    output_sections = {"text": text, "data": data}
    rebase_sites: set[tuple[int, int]] = set()
    bind_sites: list[tuple[int, str]] = []
    for name, stub_offset in stub_offsets.items():
        pointer_vm = DATA_VM_BASE + pointer_start + import_slot_offsets[name]
        text[stub_offset : stub_offset + 2] = b"\xff\x25"
        struct.pack_into("<I", text, stub_offset + 2, pointer_vm)
        rebase_sites.add((0, TEXT_FILE_OFFSET + stub_offset + 2))

    for index, obj in enumerate(objects):
        text_base = text_offsets[index]
        text[text_base : text_base + len(obj.text.contents)] = obj.text.contents
        if obj.static_init is not None:
            init_code_base = static_init_offsets[index]
            text[init_code_base : init_code_base + len(obj.static_init.contents)] = obj.static_init.contents
        if obj.data is not None:
            data_base = data_offsets[index]
            data[data_base : data_base + len(obj.data.contents)] = obj.data.contents
        if obj.const is not None:
            const_base = const_offsets[index]
            const[const_base : const_base + len(obj.const.contents)] = obj.const.contents
        if obj.cstring is not None:
            cstring_base = cstring_offsets[index]
            cstring[cstring_base : cstring_base + len(obj.cstring.contents)] = obj.cstring.contents
        if obj.pointers is not None:
            pointer_base = pointer_start + pointer_offsets[index]
            data[pointer_base : pointer_base + len(obj.pointers.contents)] = obj.pointers.contents
        if obj.initializers is not None:
            init_base = initializer_start + initializer_offsets[index]
            data[init_base : init_base + len(obj.initializers.contents)] = obj.initializers.contents
        if obj.terminators is not None:
            term_base = terminator_start + terminator_offsets[index]
            data[term_base : term_base + len(obj.terminators.contents)] = obj.terminators.contents

        for relocation in obj.relocations:
            source_base = section_placements[relocation.source_kind][index]
            source_section = (
                obj.text if relocation.source_kind == "text" else
                obj.static_init if relocation.source_kind == "static_init" else
                obj.const if relocation.source_kind == "const" else
                obj.cstring if relocation.source_kind == "cstring" else
                obj.pointers if relocation.source_kind == "ptr" else
                obj.initializers if relocation.source_kind == "init" else
                obj.terminators if relocation.source_kind == "term" else
                obj.bss if relocation.source_kind == "bss" else obj.data
            )
            if source_section is None:
                raise FormatError("relocation source section is missing")
            source_file_base = (
                pointer_start + source_base if relocation.source_kind == "ptr" else
                initializer_start + source_base if relocation.source_kind == "init" else
                terminator_start + source_base if relocation.source_kind == "term" else
                source_base if relocation.source_kind in ("const", "cstring", "static_init") else
                bss_start + source_base if relocation.source_kind == "bss" else source_base
            )
            source_output_vm = (
                TEXT_VM_BASE + const_start + source_base if relocation.source_kind == "const" else
                TEXT_VM_BASE + cstring_start + source_base if relocation.source_kind == "cstring" else
                TEXT_VM_BASE + source_base if relocation.source_kind in ("text", "static_init") else
                preferred_vm["data"] + source_file_base if relocation.source_kind in ("ptr", "bss", "init", "term")
                else preferred_vm[relocation.source_kind] + source_base
            )
            source_site = source_file_base + relocation.offset
            source_bytes = (
                const if relocation.source_kind == "const" else
                cstring if relocation.source_kind == "cstring" else
                data if relocation.source_kind in ("ptr", "bss", "init", "term") else
                text if relocation.source_kind == "static_init" else
                output_sections[relocation.source_kind]
            )

            if relocation.minuend_kind is not None:
                target_kind = relocation.target_kind
                minuend_kind = relocation.minuend_kind
                if target_kind is None:
                    raise FormatError("section-difference relocation has no target section")

                def output_address(kind: str, offset: int) -> int:
                    base = section_placements[kind][index]
                    if kind in ("bss", "init", "term"):
                        start = (
                            initializer_start if kind == "init" else
                            terminator_start if kind == "term" else bss_start
                        )
                        return preferred_vm["data"] + start + base + offset
                    if kind == "ptr":
                        return preferred_vm["data"] + pointer_start + base + offset
                    if kind == "const":
                        return TEXT_VM_BASE + const_start + base + offset
                    if kind == "cstring":
                        return TEXT_VM_BASE + cstring_start + base + offset
                    if kind in ("text", "static_init"):
                        return TEXT_VM_BASE + base + offset
                    return preferred_vm[kind] + base + offset

                target_section = getattr(obj, target_kind if target_kind != "ptr" else "pointers")
                minuend_section = getattr(obj, minuend_kind if minuend_kind != "ptr" else "pointers")
                if target_section is None or minuend_section is None:
                    raise FormatError("section-difference relocation references a missing section")
                old_target = target_section.vm_address + relocation.target_offset
                old_minuend = minuend_section.vm_address + relocation.minuend_offset
                field_value = struct.unpack_from("<I", source_bytes, source_site)[0]
                addend = field_value - (old_target - old_minuend)
                linked_value = (
                    output_address(target_kind, relocation.target_offset) -
                    output_address(minuend_kind, relocation.minuend_offset) + addend
                )
                if not -(1 << 31) <= linked_value <= 0xFFFFFFFF:
                    raise FormatError("section-difference relocation overflows i386")
                struct.pack_into("<I", source_bytes, source_site, linked_value & 0xFFFFFFFF)
            elif relocation.external_symbol is not None:
                target = definitions.get(relocation.external_symbol)
                if target is None:
                    if not relocation.pc_relative or relocation.external_symbol not in stub_offsets:
                        raise FormatError(f"unresolved relocation symbol {relocation.external_symbol}")
                    target_vm = TEXT_VM_BASE + stub_offsets[relocation.external_symbol]
                else:
                    target_index, target_kind, target_value = target
                    target_base = section_placements[target_kind][target_index]
                    if target_kind in ("bss", "init", "term"):
                        start = (
                            initializer_start if target_kind == "init" else
                            terminator_start if target_kind == "term" else bss_start
                        )
                        target_vm = preferred_vm["data"] + start + target_base + target_value
                    elif target_kind in ("text", "static_init"):
                        target_vm = TEXT_VM_BASE + target_base + target_value
                    elif target_kind == "ptr":
                        target_vm = preferred_vm["data"] + pointer_start + target_base + target_value
                    elif target_kind == "const":
                        target_vm = TEXT_VM_BASE + const_start + target_base + target_value
                    elif target_kind == "cstring":
                        target_vm = TEXT_VM_BASE + cstring_start + target_base + target_value
                    else:
                        target_vm = preferred_vm[target_kind] + target_base + target_value
                    if target_value >= section_sizes[target_kind]:
                        raise FormatError(f"relocation target {relocation.external_symbol} is outside its section")
                if relocation.pc_relative:
                    addend = struct.unpack_from("<i", source_bytes, source_site)[0]
                    # Mach-O's scattered section-relative addend can include
                    # the original source-section VM base. Remove that base
                    # from the final source placement before applying S-P.
                    linked_value = (
                        addend + target_vm -
                        (source_output_vm - source_section.vm_address)
                    )
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
                    segment_index = 0 if relocation.source_kind in ("text", "static_init") else 1
                    segment_offset = (
                        (TEXT_FILE_OFFSET if relocation.source_kind in ("text", "static_init") else 0) +
                        source_site
                    )
                    rebase_sites.add((segment_index, segment_offset))
            else:
                target_kind = relocation.target_kind
                if target_kind is None:
                    raise FormatError("local relocation has no target section")
                target_section = (
                    obj.text if target_kind == "text" else
                    obj.static_init if target_kind == "static_init" else
                    obj.const if target_kind == "const" else
                    obj.cstring if target_kind == "cstring" else
                    obj.initializers if target_kind == "init" else
                    obj.terminators if target_kind == "term" else
                    obj.bss if target_kind == "bss" else
                    obj.pointers if target_kind == "ptr" else obj.data
                )
                if target_section is None:
                    raise FormatError("local relocation targets a missing section")
                target_base = section_placements[target_kind][index]
                if target_kind in ("bss", "init", "term"):
                    start = (
                        initializer_start if target_kind == "init" else
                        terminator_start if target_kind == "term" else bss_start
                    )
                    target_output_vm = preferred_vm["data"] + start + target_base
                elif target_kind in ("text", "static_init"):
                    target_output_vm = TEXT_VM_BASE + target_base
                elif target_kind == "ptr":
                    target_output_vm = preferred_vm["data"] + pointer_start + target_base
                elif target_kind == "const":
                    target_output_vm = TEXT_VM_BASE + const_start + target_base
                elif target_kind == "cstring":
                    target_output_vm = TEXT_VM_BASE + cstring_start + target_base
                else:
                    target_output_vm = preferred_vm[target_kind] + target_base
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
                    segment_index = 0 if relocation.source_kind in ("text", "static_init", "const", "cstring") else 1
                    segment_offset = (
                        (TEXT_FILE_OFFSET + (
                            const_start if relocation.source_kind == "const" else
                            cstring_start if relocation.source_kind == "cstring" else 0
                         ) if relocation.source_kind in ("text", "static_init", "const", "cstring") else 0) +
                        source_site
                    )
                    rebase_sites.add((segment_index, segment_offset))

    for index, obj in enumerate(objects):
        if obj.pointers is None:
            continue
        for slot, name in enumerate(obj.pointer_symbols):
            target = definitions.get(name)
            if target is None:
                bind_sites.append((pointer_start + pointer_offsets[index] + slot * 4, name))
                continue
            target_index, target_kind, target_value = target
            if target_value >= section_sizes[target_kind]:
                raise FormatError(f"import pointer target {name} lies outside its section")
            target_base = section_placements[target_kind][target_index]
            if target_kind == "bss":
                target_vm = DATA_VM_BASE + bss_start + target_base + target_value
            elif target_kind == "ptr":
                target_vm = DATA_VM_BASE + pointer_start + target_base + target_value
            elif target_kind == "const":
                target_vm = TEXT_VM_BASE + const_start + target_base + target_value
            elif target_kind == "cstring":
                target_vm = TEXT_VM_BASE + cstring_start + target_base + target_value
            else:
                target_vm = preferred_vm[target_kind] + target_base + target_value
            slot_offset = pointer_start + pointer_offsets[index] + slot * 4
            struct.pack_into("<I", data, slot_offset, target_vm)
            rebase_sites.add((1, slot_offset))

    for name in sorted(import_names - pointer_import_names):
        bind_sites.append((pointer_start + import_slot_offsets[name], name))

    entry_index, entry_kind, entry_value = definitions[entry_name]
    if entry_kind != "text":
        raise FormatError(f"entry symbol {entry_name} is not in __text")
    return (
        bytes(text), bytes(const), const_start, bytes(cstring), cstring_start,
        bytes(data), initializer_start,
        initializer_size, terminator_start, terminator_size,
        text_offsets[entry_index] + entry_value,
        cpu_subtype, text_align_exp, const_align_exp, cstring_align_exp, data_align_exp,
        sorted(rebase_sites), sorted(set(bind_sites)),
    )


def encode_uleb(value: int) -> bytes:
    result = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        result.append(byte | (0x80 if value else 0))
        if value == 0:
            return bytes(result)


def encode_bind_stream(bind_sites: list[tuple[int, str]]) -> bytes:
    stream = bytearray()
    for segment_offset, symbol in bind_sites:
        stream.extend((0x3E, 0x40))  # flat lookup, symbol name
        stream.extend(symbol.encode("utf-8"))
        stream.extend((0, 0x51, 0x71))  # pointer bind, segment 1 + offset
        stream.extend(encode_uleb(segment_offset))
        stream.append(0x90)  # DO_BIND
    if stream:
        stream.append(0)
    return bytes(stream)


def make_segment_command(
    name: bytes, vm_address: int, file_offset: int, file_size: int,
    max_protection: int, initial_protection: int,
    section_name: bytes, section_segment: bytes, section_vm_address: int,
    section_size: int, section_file_offset: int, section_alignment: int,
    section_flags: int = 0,
    additional_sections: tuple[tuple[bytes, bytes, int, int, int, int, int], ...] = (),
) -> bytes:
    command_size = 56 + 68 * (1 + len(additional_sections))
    command = struct.pack(
        "<II16sIIIIiiII", LC_SEGMENT, command_size, name, vm_address,
        PAGE_SIZE, file_offset, file_size, max_protection,
        initial_protection, 1 + len(additional_sections), 0,
    )
    def pack_section(record: tuple[bytes, bytes, int, int, int, int, int]) -> bytes:
        sect_name, seg_name, vmaddr, size, fileoff, align_exp, flags = record
        return struct.pack(
            "<16s16sIIIIIIIII", sect_name, seg_name, vmaddr,
            size, fileoff, align_exp, 0, 0, flags, 0, 0,
        )
    sections = [pack_section((
        section_name, section_segment, section_vm_address, section_size,
        section_file_offset, section_alignment, section_flags,
    ))]
    sections.extend(pack_section(record) for record in additional_sections)
    return command + b"".join(sections)


def make_executable(
    text: bytes, const: bytes, const_start: int, data: bytes,
    cstring: bytes, cstring_start: int,
    initializer_start: int, initializer_size: int,
    terminator_start: int, terminator_size: int,
    entry_offset: int, cpu_subtype: int,
    text_align_exp: int, const_align_exp: int, cstring_align_exp: int, data_align_exp: int,
    rebase_sites: list[tuple[int, int]],
    bind_sites: list[tuple[int, str]],
) -> bytes:
    if not text:
        raise FormatError("combined __text is empty")
    if entry_offset < 0 or entry_offset >= len(text):
        raise FormatError("entry symbol lies outside combined __text")
    if const and const_start + len(const) > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined __text and __const exceed one page")
    if cstring and cstring_start + len(cstring) > PAGE_SIZE - TEXT_FILE_OFFSET:
        raise FormatError("combined read-only text sections exceed one page")
    if len(data) > PAGE_SIZE:
        raise FormatError("combined __data exceeds the one-page prototype image limit")

    text_sections = []
    if const:
        text_sections.append((
            b"__const", b"__TEXT", TEXT_VM_BASE + const_start, len(const),
            TEXT_FILE_OFFSET + const_start, const_align_exp, 0,
        ))
    if cstring:
        text_sections.append((
            b"__cstring", b"__TEXT", TEXT_VM_BASE + cstring_start, len(cstring),
            TEXT_FILE_OFFSET + cstring_start, cstring_align_exp, S_CSTRING_LITERALS,
        ))
    commands = [make_segment_command(
        b"__TEXT", 0x1000, 0, PAGE_SIZE, 7, 5,
        b"__text", b"__TEXT", TEXT_VM_BASE, len(text), TEXT_FILE_OFFSET,
        text_align_exp, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
        tuple(text_sections),
    )]
    if data:
        runtime_sections = []
        data_section_size = len(data)
        if initializer_size:
            data_section_size = min(data_section_size, initializer_start)
            runtime_sections.append((
                b"__mod_init_func", b"__DATA", DATA_VM_BASE + initializer_start,
                initializer_size, DATA_FILE_OFFSET + initializer_start, 2,
                S_MOD_INIT_FUNC_POINTERS,
            ))
        if terminator_size:
            data_section_size = min(data_section_size, terminator_start)
            runtime_sections.append((
                b"__mod_term_func", b"__DATA", DATA_VM_BASE + terminator_start,
                terminator_size, DATA_FILE_OFFSET + terminator_start, 2,
                S_MOD_TERM_FUNC_POINTERS,
            ))
        commands.append(make_segment_command(
            b"__DATA", DATA_VM_BASE, DATA_FILE_OFFSET, PAGE_SIZE, 7, 3,
            b"__data", b"__DATA", DATA_VM_BASE, data_section_size, DATA_FILE_OFFSET,
            data_align_exp, additional_sections=tuple(runtime_sections),
        ))

    rebase_stream = bytearray()
    if rebase_sites:
        for segment_index, segment_offset in rebase_sites:
            rebase_stream.extend((0x12, 0x20 | segment_index))
            rebase_stream.extend(encode_uleb(segment_offset))
            rebase_stream.append(0x51)
        rebase_stream.append(0)
    bind_stream = encode_bind_stream(bind_sites)
    stream_base = (DATA_FILE_OFFSET + PAGE_SIZE) if data else PAGE_SIZE
    rebase_offset = stream_base if rebase_stream else 0
    bind_offset = stream_base + len(rebase_stream) if bind_stream else 0
    if rebase_stream or bind_stream:
        dyld_info = struct.pack(
            "<12I", LC_DYLD_INFO_ONLY, 48, rebase_offset, len(rebase_stream),
            bind_offset, len(bind_stream), 0, 0, 0, 0, 0, 0,
        )
        commands.append(dyld_info)

    main = struct.pack("<IIQQ", LC_MAIN, 24, TEXT_FILE_OFFSET + entry_offset, 0)
    commands.append(main)
    sizeofcmds = sum(map(len, commands))
    flags = MH_DYLDLINK if rebase_sites or bind_sites else 0
    header = struct.pack(
        "<IiiIIII", MH_MAGIC, CPU_TYPE_I386, cpu_subtype, MH_EXECUTE,
        len(commands), sizeofcmds, flags,
    )

    image_size = (DATA_FILE_OFFSET + PAGE_SIZE) if data else PAGE_SIZE
    if rebase_stream:
        image_size = max(image_size, rebase_offset + len(rebase_stream))
    if bind_stream:
        image_size = max(image_size, bind_offset + len(bind_stream))
    image = bytearray(image_size)
    image[:28] = header
    cursor = 28
    for command in commands:
        image[cursor : cursor + len(command)] = command
        cursor += len(command)
    if cursor > TEXT_FILE_OFFSET:
        raise FormatError("Mach-O load commands overlap the __text file offset")
    image[TEXT_FILE_OFFSET : TEXT_FILE_OFFSET + len(text)] = text
    if const:
        image[TEXT_FILE_OFFSET + const_start : TEXT_FILE_OFFSET + const_start + len(const)] = const
    if cstring:
        image[TEXT_FILE_OFFSET + cstring_start : TEXT_FILE_OFFSET + cstring_start + len(cstring)] = cstring
    if data:
        image[DATA_FILE_OFFSET : DATA_FILE_OFFSET + len(data)] = data
    if rebase_stream:
        image[rebase_offset : rebase_offset + len(rebase_stream)] = rebase_stream
    if bind_stream:
        image[bind_offset : bind_offset + len(bind_stream)] = bind_stream
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="i386 Mach-O object files")
    parser.add_argument("-o", "--output", required=True, type=Path, help="output i386 MH_EXECUTE")
    parser.add_argument("--entry", default="_main", help="external entry symbol (default: _main)")
    args = parser.parse_args()

    try:
        objects = [parse_object(path, path.read_bytes()) for path in args.inputs]
        (text, const, const_start, cstring, cstring_start, data, initializer_start, initializer_size,
         terminator_start, terminator_size,
         entry_offset, cpu_subtype,
         text_align_exp, const_align_exp, cstring_align_exp, data_align_exp, rebase_sites,
         bind_sites) = link_objects(objects, args.entry)
        image = make_executable(
            text, const, const_start, data, cstring, cstring_start,
            initializer_start, initializer_size,
            terminator_start, terminator_size,
            entry_offset, cpu_subtype,
            text_align_exp, const_align_exp, cstring_align_exp, data_align_exp, rebase_sites,
            bind_sites,
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
