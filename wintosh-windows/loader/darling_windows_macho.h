/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace darling::windows_host {

constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t MH_MAGIC = 0xfeedface;
constexpr std::uint32_t FAT_MAGIC_BE = 0xbebafeca;
constexpr std::uint32_t FAT_MAGIC_64_BE = 0xbfbafeca;
constexpr std::int32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr std::int32_t CPU_TYPE_X86 = 7;
constexpr std::int32_t CPU_TYPE_ARM64 = 0x0100000c;
constexpr std::uint32_t MH_EXECUTE = 0x2;
constexpr std::uint32_t MH_DYLIB = 0x6;
constexpr std::uint32_t MH_BUNDLE = 0x8;
constexpr std::uint32_t LC_SYMTAB = 0x2;
constexpr std::uint32_t LC_DYSYMTAB = 0xb;
constexpr std::uint32_t LC_DYLD_INFO = 0x22;
constexpr std::uint32_t LC_DYLD_INFO_ONLY = 0x80000022;
constexpr std::uint32_t LC_DYLD_CHAINED_FIXUPS = 0x80000034;
constexpr std::uint32_t LC_DYLD_EXPORTS_TRIE = 0x80000033;
constexpr std::uint32_t LC_RPATH = 0x8000001c;
constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_SEGMENT = 0x1;
constexpr std::uint32_t LC_MAIN = 0x80000028;
constexpr std::uint32_t LC_UUID = 0x1b;
constexpr std::uint32_t LC_CODE_SIGNATURE = 0x1d;
constexpr std::uint32_t LC_ID_DYLIB = 0x0d;
constexpr std::uint32_t LC_UNIXTHREAD = 0x5;
constexpr std::uint32_t LC_LOAD_DYLIB = 0xc;
constexpr std::uint32_t LC_LOAD_WEAK_DYLIB = 0x80000018;
constexpr std::uint32_t LC_REEXPORT_DYLIB = 0x8000001f;
constexpr std::uint32_t LC_LOAD_UPWARD_DYLIB = 0x80000023;

#pragma pack(push, 1)
struct MachHeader64 final {
	std::uint32_t magic;
	std::int32_t cpu_type;
	std::int32_t cpu_subtype;
	std::uint32_t file_type;
	std::uint32_t command_count;
	std::uint32_t command_bytes;
	std::uint32_t flags;
	std::uint32_t reserved;
};

struct MachHeader32 final {
	std::uint32_t magic;
	std::int32_t cpu_type;
	std::int32_t cpu_subtype;
	std::uint32_t file_type;
	std::uint32_t command_count;
	std::uint32_t command_bytes;
	std::uint32_t flags;
};

struct LoadCommand final {
	std::uint32_t command;
	std::uint32_t command_bytes;
};

struct SegmentCommand64 final {
	LoadCommand header;
	char segment_name[16];
	std::uint64_t vm_address;
	std::uint64_t vm_size;
	std::uint64_t file_offset;
	std::uint64_t file_size;
	std::int32_t max_protection;
	std::int32_t initial_protection;
	std::uint32_t section_count;
	std::uint32_t flags;
};

struct SegmentCommand32 final {
	LoadCommand header;
	char segment_name[16];
	std::uint32_t vm_address;
	std::uint32_t vm_size;
	std::uint32_t file_offset;
	std::uint32_t file_size;
	std::int32_t max_protection;
	std::int32_t initial_protection;
	std::uint32_t section_count;
	std::uint32_t flags;
};

struct Section64 final {
	char section_name[16];
	char segment_name[16];
	std::uint64_t address;
	std::uint64_t size;
	std::uint32_t file_offset;
	std::uint32_t alignment;
	std::uint32_t relocation_offset;
	std::uint32_t relocation_count;
	std::uint32_t flags;
	std::uint32_t reserved1;
	std::uint32_t reserved2;
	std::uint32_t reserved3;
};

struct Section32 final {
	char section_name[16];
	char segment_name[16];
	std::uint32_t address;
	std::uint32_t size;
	std::uint32_t file_offset;
	std::uint32_t alignment;
	std::uint32_t relocation_offset;
	std::uint32_t relocation_count;
	std::uint32_t flags;
	std::uint32_t reserved1;
	std::uint32_t reserved2;
};

struct MainCommand final {
	LoadCommand header;
	std::uint64_t entry_offset;
	std::uint64_t stack_size;
};

struct DylibCommand final {
	LoadCommand header;
	std::uint32_t name_offset;
	std::uint32_t timestamp;
	std::uint32_t current_version;
	std::uint32_t compatibility_version;
};

struct SymtabCommand final {
	LoadCommand header;
	std::uint32_t symbol_offset;
	std::uint32_t symbol_count;
	std::uint32_t string_offset;
	std::uint32_t string_bytes;
};

struct DYSymtabCommand final {
	LoadCommand header;
	std::uint32_t local_symbol_index;
	std::uint32_t local_symbol_count;
	std::uint32_t external_symbol_index;
	std::uint32_t external_symbol_count;
	std::uint32_t undefined_symbol_index;
	std::uint32_t undefined_symbol_count;
	std::uint32_t toc_offset;
	std::uint32_t toc_count;
	std::uint32_t module_offset;
	std::uint32_t module_count;
	std::uint32_t external_reference_offset;
	std::uint32_t external_reference_count;
	std::uint32_t indirect_symbol_offset;
	std::uint32_t indirect_symbol_count;
	std::uint32_t external_relocation_offset;
	std::uint32_t external_relocation_count;
	std::uint32_t local_relocation_offset;
	std::uint32_t local_relocation_count;
};

struct DyldInfoCommand final {
	LoadCommand header;
	std::uint32_t rebase_offset;
	std::uint32_t rebase_size;
	std::uint32_t binding_offset;
	std::uint32_t binding_size;
	std::uint32_t weak_binding_offset;
	std::uint32_t weak_binding_size;
	std::uint32_t lazy_binding_offset;
	std::uint32_t lazy_binding_size;
	std::uint32_t export_offset;
	std::uint32_t export_size;
};

struct LinkeditDataCommand final {
	LoadCommand header;
	std::uint32_t data_offset;
	std::uint32_t data_size;
};

struct RPathCommand final {
	LoadCommand header;
	std::uint32_t path_offset;
};

struct RelocationInfo final {
	std::int32_t address;
	std::uint32_t info;
};

struct NList64 final {
	std::uint32_t name_offset;
	std::uint8_t type;
	std::uint8_t section;
	std::uint16_t description;
	std::uint64_t value;
};

struct NList32 final {
	std::uint32_t name_offset;
	std::uint8_t type;
	std::uint8_t section;
	std::uint16_t description;
	std::uint32_t value;
};
#pragma pack(pop)

struct MachOSegment final {
	std::string name;
	std::uint64_t vm_address = 0;
	std::uint64_t vm_size = 0;
	std::uint64_t file_offset = 0;
	std::uint64_t file_size = 0;
	std::int32_t max_protection = 0;
	std::int32_t initial_protection = 0;
};

struct MachOSection final {
	std::string segment_name;
	std::string section_name;
	std::uint64_t address = 0;
	std::uint64_t size = 0;
	std::uint32_t file_offset = 0;
};

struct MappedMachOSegment final {
	std::string name;
	std::uintptr_t address = 0;
	std::size_t size = 0;
};

struct MachOSymbol final {
	std::string name;
	std::uint8_t type = 0;
	std::uint8_t section = 0;
	std::uint16_t description = 0;
	std::uint64_t value = 0;
};

struct MachORelocation final {
	std::int32_t address = 0;
	std::uint32_t symbol_index = 0;
	bool pc_relative = false;
	std::uint8_t length = 0;
	bool external = false;
	std::uint8_t type = 0;
	bool has_explicit_addend = false;
	std::int64_t explicit_addend = 0;
	bool scattered = false;
	std::int32_t scattered_value = 0;
};

struct DyldBindAction final {
	std::string symbol;
	std::uint32_t segment_index = 0;
	std::uint64_t segment_offset = 0;
	std::uint8_t type = 0;
	std::int64_t addend = 0;
	std::int32_t dylib_ordinal = 0;
	bool weak = false;
	bool lazy = false;
	bool threaded = false;
	std::size_t threaded_table_size = 0;
};

struct DyldResolvedBinding final {
	std::string symbol;
	std::uintptr_t address = 0;
};

struct DyldRebaseAction final {
	std::uint32_t segment_index = 0;
	std::uint64_t segment_offset = 0;
	std::uint8_t type = 0;
};

struct DyldChainedFixup final {
	std::uint32_t segment_index = 0;
	std::uint64_t vm_address = 0;
	bool bind = false;
	std::uint32_t bind_ordinal = 0;
	std::int64_t addend = 0;
	std::uint64_t target = 0;
	std::uint16_t next = 0;
	std::uint16_t pointer_format = 2;
	bool authenticated = false;
};

struct DyldChainedImport final {
	std::int32_t dylib_ordinal = 0;
	bool weak = false;
	std::string symbol;
	std::int64_t addend = 0;
};

struct MachOReexport final {
	std::string name;
	std::string target;
	std::uint32_t dependency_ordinal = 0;
};

class MachOImage final {
public:
	class Mapping;

	static MachOImage Open(const std::wstring& path);
	MachOImage(const MachOImage&) = delete;
	MachOImage& operator=(const MachOImage&) = delete;
	MachOImage(MachOImage&& other) noexcept;
	MachOImage& operator=(MachOImage&& other) noexcept;
	~MachOImage() noexcept;

	[[nodiscard]] const MachHeader64& Header() const noexcept { return *m_header; }
	[[nodiscard]] const std::array<std::uint8_t, 16>& UUID() const noexcept { return m_uuid; }
	[[nodiscard]] bool IsArm64() const noexcept { return m_header->cpu_type == CPU_TYPE_ARM64; }
	[[nodiscard]] bool Is32Bit() const noexcept { return m_is_32_bit; }
	[[nodiscard]] const std::vector<MachOSegment>& Segments() const noexcept { return m_segments; }
	[[nodiscard]] const std::vector<MachOSection>& Sections() const noexcept { return m_sections; }
	[[nodiscard]] std::uint64_t EntryOffset() const noexcept { return m_entry_offset; }
	[[nodiscard]] std::size_t FileBytes() const noexcept { return m_file_bytes; }
	[[nodiscard]] const std::vector<std::string>& Dependencies() const noexcept { return m_dependencies; }
	[[nodiscard]] const std::string& InstallName() const noexcept { return m_install_name; }
	[[nodiscard]] bool IsWeakDependency(const std::string& name) const noexcept;
	[[nodiscard]] const std::vector<std::string>& RPaths() const noexcept { return m_rpaths; }
	[[nodiscard]] const std::vector<MachOSymbol>& Symbols() const noexcept { return m_symbols; }
	[[nodiscard]] const std::vector<std::uint32_t>& IndirectSymbols() const noexcept { return m_indirect_symbols; }
	[[nodiscard]] std::vector<std::uint64_t> IndirectImportSlots() const;
	void ApplyIndirectImportBindings(const Mapping& mapping,
		const std::vector<DyldResolvedBinding>& bindings) const;
	[[nodiscard]] const std::vector<MachOReexport>& Reexports() const noexcept { return m_reexports; }
	[[nodiscard]] static bool IsWeakUndefined(const MachOSymbol& symbol) noexcept;
	[[nodiscard]] const std::vector<MachORelocation>& Relocations() const noexcept { return m_relocations; }
	[[nodiscard]] const std::vector<DyldBindAction>& BindActions() const noexcept { return m_bind_actions; }
	[[nodiscard]] const std::vector<DyldRebaseAction>& RebaseActions() const noexcept { return m_rebase_actions; }
	[[nodiscard]] const std::vector<DyldChainedFixup>& ChainedFixups() const noexcept { return m_chained_fixups; }
	[[nodiscard]] const std::vector<DyldChainedImport>& ChainedImports() const noexcept { return m_chained_imports; }
	[[nodiscard]] std::vector<MachOSymbol> UndefinedSymbols() const;
	[[nodiscard]] std::uintptr_t SymbolAddress(
		const Mapping& mapping, const std::string& name) const;
	[[nodiscard]] std::uintptr_t SectionAddress(const Mapping& mapping,
		const std::string& segment_name, const std::string& section_name) const;
	void ApplyAbsolute64(const Mapping& mapping, std::uint64_t vm_address,
		std::uintptr_t target) const;
	void ApplyAbsolute32(const Mapping& mapping, std::uint64_t vm_address,
		std::uintptr_t target) const;
	void ApplyRelative32(const Mapping& mapping, std::uint64_t vm_address,
		std::uintptr_t target) const;
	void ApplyRelative32WithAddend(const Mapping& mapping, std::uint64_t vm_address,
		std::uintptr_t target, std::int32_t addend) const;
	[[nodiscard]] std::uintptr_t TLVPointerAddress(const Mapping& mapping,
		std::uintptr_t descriptor_address) const;
	[[nodiscard]] bool IsOptimizedTLVLEA(const Mapping& mapping,
		std::uint64_t vm_address, std::uintptr_t target) const;
	void ApplyRelocations(const Mapping& mapping,
		const std::vector<std::uintptr_t>& symbol_addresses,
		bool defer_unreachable_external = false) const;
	void ApplyBindActions(const Mapping& mapping,
		const std::vector<DyldResolvedBinding>& bindings) const;
	void ApplyLazyBindAction(std::size_t action_index, const Mapping& mapping,
		const std::vector<DyldResolvedBinding>& bindings) const;
	void ApplyLazyBindActions(const Mapping& mapping,
		const std::vector<DyldResolvedBinding>& bindings) const;
	void ApplyChainedFixups(const Mapping& mapping,
		const std::vector<std::uintptr_t>& bind_addresses) const;
	void ApplyChainedFixups(const Mapping& mapping,
		const std::vector<DyldResolvedBinding>& bindings) const;
	void ApplyRebaseActions(const Mapping& mapping) const;
	void ExecuteInitializers(const Mapping& mapping) const;
	void ExecuteTerminators(const Mapping& mapping) const;
	[[nodiscard]] const std::uint8_t* FileData(std::uint64_t offset, std::size_t bytes) const;

	Mapping MapSegments(std::uintptr_t near_address = 0) const;
	void ExecuteEntryNoArgs(const Mapping& mapping) const;
	[[nodiscard]] int ExecuteEntry(const Mapping& mapping,
		const std::vector<std::string>& arguments,
		const std::vector<std::string>& environment = {}) const;

private:
	MachOImage(HANDLE file, HANDLE mapping, const std::uint8_t* view,
		std::size_t file_bytes, std::size_t image_offset, MachHeader64 header,
		bool is_32_bit,
		std::vector<MachOSegment> segments, std::vector<std::string> dependencies,
		std::vector<MachOSection> sections,
		std::vector<std::string> weak_dependencies, std::vector<std::string> rpaths,
		std::vector<MachOSymbol> symbols, std::vector<MachORelocation> relocations,
		std::vector<std::uint32_t> indirect_symbols,
		std::vector<DyldBindAction> bind_actions,
		std::vector<DyldBindAction> threaded_bind_targets,
		std::vector<DyldRebaseAction> rebase_actions,
		std::vector<DyldChainedFixup> chained_fixups,
		std::vector<DyldChainedImport> chained_imports,
		std::vector<MachOReexport> reexports,
		std::uint64_t entry_offset, std::array<std::uint8_t, 16> uuid,
		std::string install_name) noexcept;
	void Reset() noexcept;

	HANDLE m_file = INVALID_HANDLE_VALUE;
	HANDLE m_mapping = nullptr;
	const std::uint8_t* m_view = nullptr;
	std::size_t m_file_bytes = 0;
	std::size_t m_image_offset = 0;
	MachHeader64 m_header_storage{};
	const MachHeader64* m_header = &m_header_storage;
	bool m_is_32_bit = false;
	std::vector<MachOSegment> m_segments;
	std::vector<MachOSection> m_sections;
	std::vector<std::string> m_dependencies;
	std::vector<std::string> m_weak_dependencies;
	std::vector<std::string> m_rpaths;
	std::vector<MachOSymbol> m_symbols;
	std::vector<std::uint32_t> m_indirect_symbols;
	std::vector<MachORelocation> m_relocations;
	std::vector<DyldBindAction> m_bind_actions;
	std::vector<DyldBindAction> m_threaded_bind_targets;
	std::vector<DyldRebaseAction> m_rebase_actions;
	std::vector<DyldChainedFixup> m_chained_fixups;
	std::vector<DyldChainedImport> m_chained_imports;
	std::vector<MachOReexport> m_reexports;
	std::array<std::uint8_t, 16> m_uuid{};
	std::string m_install_name;
	std::uint64_t m_entry_offset = 0;
};

class MachOImage::Mapping final {
public:
	Mapping() noexcept = default;
	Mapping(const Mapping&) = delete;
	Mapping& operator=(const Mapping&) = delete;
	Mapping(Mapping&& other) noexcept;
	Mapping& operator=(Mapping&& other) noexcept;
	~Mapping() noexcept;

	[[nodiscard]] const std::vector<MappedMachOSegment>& Segments() const noexcept { return m_segments; }
	[[nodiscard]] std::uintptr_t EntryAddress() const noexcept { return m_entry_address; }
	[[nodiscard]] std::intptr_t Slide() const noexcept { return m_slide; }

private:
	friend class MachOImage;
	Mapping(void* reservation, std::vector<MappedMachOSegment> segments,
		std::uintptr_t entry_address, std::intptr_t slide, std::size_t reservation_bytes) noexcept;
	void Reset() noexcept;

	void* m_reservation = nullptr;
	std::vector<MappedMachOSegment> m_segments;
	std::uintptr_t m_entry_address = 0;
	std::intptr_t m_slide = 0;
	std::size_t m_reservation_bytes = 0;
};

} // namespace darling::windows_host
