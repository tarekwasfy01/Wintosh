/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_macho.h"
#include "darling_windows_stdio.h"
#include "darling_windows_abi_bridge.h"
#include "darling_windows_tlv.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace darling::windows_host {

bool DecodeDarwinX86_64StubFrame(const std::uintptr_t* metadata_stack,
	DarwinX86_64StubFrame* frame) noexcept
{
	if (metadata_stack == nullptr || frame == nullptr)
		return false;
	frame->image_cache = metadata_stack[0];
	frame->lazy_binding_info = metadata_stack[1];
	frame->return_address = metadata_stack[2];
	return frame->image_cache != 0 && frame->lazy_binding_info != 0 &&
		frame->return_address != 0;
}

namespace {
thread_local const MachOLazyBindingSession* active_lazy_binding_session = nullptr;
std::mutex active_lazy_binding_sessions_mutex;
std::vector<const MachOLazyBindingSession*> active_lazy_binding_sessions;

void RemoveActiveLazyBindingSession(const MachOLazyBindingSession* session) noexcept
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	const auto found = std::find(active_lazy_binding_sessions.rbegin(),
		active_lazy_binding_sessions.rend(), session);
	if (found != active_lazy_binding_sessions.rend())
		active_lazy_binding_sessions.erase(std::next(found).base());
}
}

#if defined(_WIN64) && defined(__clang__)
extern "C" void darling_windows_enter_darwin_start(void* entry, void* darwin_stack);
#endif

namespace {

#pragma pack(push, 1)
struct ChainedFixupsHeader final {
	std::uint32_t version;
	std::uint32_t starts_offset;
	std::uint32_t imports_offset;
	std::uint32_t symbols_offset;
	std::uint32_t imports_count;
	std::uint32_t imports_format;
	std::uint32_t symbols_format;
};

struct ChainedStartsSegment final {
	std::uint32_t size;
	std::uint16_t page_size;
	std::uint16_t pointer_format;
	std::uint64_t segment_offset;
	std::uint32_t max_valid_pointer;
	std::uint16_t page_count;
};
#pragma pack(pop)

class DeflateBitReader final {
public:
	explicit DeflateBitReader(const std::uint8_t* bytes, std::size_t size) :
		m_bytes(bytes), m_size(size) {}

	std::uint32_t Read(unsigned count)
	{
		if (count > 16)
			throw std::runtime_error("invalid zlib deflate bit count");
		std::uint32_t value = 0;
		for (unsigned bit = 0; bit < count; ++bit) {
			if (m_offset / 8 >= m_size)
				throw std::runtime_error("truncated zlib deflate stream");
			value |= static_cast<std::uint32_t>(
				(m_bytes[m_offset / 8] >> (m_offset % 8)) & 1u) << bit;
			++m_offset;
		}
		return value;
	}

	void AlignByte() noexcept { m_offset = (m_offset + 7) & ~std::size_t(7); }
	[[nodiscard]] std::size_t ByteOffset() const noexcept { return m_offset / 8; }

private:
	const std::uint8_t* m_bytes;
	std::size_t m_size;
	std::size_t m_offset = 0;
};

class DeflateHuffman final {
public:
	explicit DeflateHuffman(const std::vector<std::uint8_t>& lengths,
		bool allow_empty = false)
	{
		std::array<std::uint16_t, 16> counts{};
		for (const auto length : lengths) {
			if (length > 15)
				throw std::runtime_error("invalid zlib deflate Huffman code length");
			if (length != 0)
				++counts[length];
		}
		std::int32_t remaining = 1;
		for (std::size_t length = 1; length <= 15; ++length) {
			remaining = (remaining << 1) - counts[length];
			if (remaining < 0)
				throw std::runtime_error("oversubscribed zlib deflate Huffman tree");
		}
		if (std::all_of(lengths.begin(), lengths.end(),
			[](std::uint8_t length) { return length == 0; })) {
			if (allow_empty)
				return;
			throw std::runtime_error("empty zlib deflate Huffman tree");
		}
		std::array<std::uint16_t, 16> next_code{};
		std::uint16_t code = 0;
		for (std::size_t length = 1; length <= 15; ++length) {
			code = static_cast<std::uint16_t>((code + counts[length - 1]) << 1);
			next_code[length] = code;
		}
		for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
			const auto length = lengths[symbol];
			if (length != 0)
				m_codes[length].emplace_back(next_code[length]++,
					static_cast<std::uint16_t>(symbol));
		}
	}

	std::uint16_t Decode(DeflateBitReader& bits) const
	{
		std::uint16_t code = 0;
		for (std::uint16_t length = 1; length <= 15; ++length) {
			code = static_cast<std::uint16_t>((code << 1) | bits.Read(1));
			const auto& entries = m_codes[length];
			const auto found = std::lower_bound(entries.begin(), entries.end(), code,
				[](const auto& entry, std::uint16_t value) { return entry.first < value; });
			if (found != entries.end() && found->first == code)
				return found->second;
		}
		throw std::runtime_error("invalid zlib deflate Huffman code");
	}

private:
	std::vector<std::pair<std::uint16_t, std::uint16_t>> m_codes[16];
};

std::vector<std::uint8_t> InflateZlib(const std::uint8_t* bytes,
	std::size_t size)
{
	constexpr std::size_t MaximumOutputBytes = 0x10000000;
	if (bytes == nullptr || size < 6)
		throw std::runtime_error("truncated zlib symbol pool");
	const auto cmf = bytes[0];
	const auto flg = bytes[1];
	if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 ||
		((static_cast<unsigned>(cmf) << 8) | flg) % 31 != 0 || (flg & 0x20) != 0)
		throw std::runtime_error("invalid or unsupported zlib symbol-pool header");
	DeflateBitReader bits(bytes + 2, size - 6);
	std::vector<std::uint8_t> output;
	output.reserve(size < MaximumOutputBytes / 2 ? size * 2 : MaximumOutputBytes);
	const auto emit = [&](std::uint8_t byte) {
		if (output.size() >= MaximumOutputBytes)
			throw std::runtime_error("zlib symbol pool exceeds the 256 MiB limit");
		output.push_back(byte);
	};
	static constexpr std::array<std::uint16_t, 29> length_base = {
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258};
	static constexpr std::array<std::uint8_t, 29> length_extra = {
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
	static constexpr std::array<std::uint16_t, 30> distance_base = {
		1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,
		1537,2049,3073,4097,6145,8193,12289,16385,24577};
	static constexpr std::array<std::uint8_t, 30> distance_extra = {
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
	const auto decode_compressed_block = [&](const DeflateHuffman& literals,
		const DeflateHuffman& distances) {
		for (;;) {
			const auto symbol = literals.Decode(bits);
			if (symbol < 256) {
				emit(static_cast<std::uint8_t>(symbol));
				continue;
			}
			if (symbol == 256)
				return;
			if (symbol < 257 || symbol > 285)
				throw std::runtime_error("invalid zlib deflate length symbol");
			const auto length_index = symbol - 257;
			const auto length = length_base[length_index] +
				bits.Read(length_extra[length_index]);
			const auto distance_symbol = distances.Decode(bits);
			if (distance_symbol >= distance_base.size())
				throw std::runtime_error("invalid zlib deflate distance symbol");
			const auto distance = distance_base[distance_symbol] +
				bits.Read(distance_extra[distance_symbol]);
			if (distance == 0 || distance > output.size() ||
				length > MaximumOutputBytes - output.size())
				throw std::runtime_error("invalid or excessive zlib deflate back-reference");
			for (std::uint32_t index = 0; index < length; ++index)
				emit(output[output.size() - distance]);
		}
	};
	bool final_block = false;
	while (!final_block) {
		final_block = bits.Read(1) != 0;
		const auto type = bits.Read(2);
		if (type == 0) {
			bits.AlignByte();
			const auto length = bits.Read(16);
			const auto inverse = bits.Read(16);
			if ((length ^ 0xffffu) != inverse || length > MaximumOutputBytes - output.size())
				throw std::runtime_error("invalid or excessive zlib stored block");
			for (std::uint32_t index = 0; index < length; ++index)
				emit(static_cast<std::uint8_t>(bits.Read(8)));
			continue;
		}
		if (type == 3)
			throw std::runtime_error("reserved zlib deflate block type");
		std::vector<std::uint8_t> literal_lengths;
		std::vector<std::uint8_t> distance_lengths;
		if (type == 1) {
			literal_lengths.resize(288);
			for (std::size_t index = 0; index <= 143; ++index) literal_lengths[index] = 8;
			for (std::size_t index = 144; index <= 255; ++index) literal_lengths[index] = 9;
			for (std::size_t index = 256; index <= 279; ++index) literal_lengths[index] = 7;
			for (std::size_t index = 280; index <= 287; ++index) literal_lengths[index] = 8;
			distance_lengths.assign(32, 5);
		} else {
			const auto literal_count = bits.Read(5) + 257;
			const auto distance_count = bits.Read(5) + 1;
			const auto code_count = bits.Read(4) + 4;
			if (literal_count > 286)
				throw std::runtime_error("reserved zlib literal/length code count");
			static constexpr std::array<std::uint8_t, 19> order = {
				16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
			std::vector<std::uint8_t> code_lengths(19, 0);
			for (std::uint32_t index = 0; index < code_count; ++index)
				code_lengths[order[index]] = static_cast<std::uint8_t>(bits.Read(3));
			const DeflateHuffman code_tree(code_lengths);
			std::vector<std::uint8_t> combined;
			combined.reserve(literal_count + distance_count);
			while (combined.size() < literal_count + distance_count) {
				const auto symbol = code_tree.Decode(bits);
				if (symbol <= 15) {
					combined.push_back(static_cast<std::uint8_t>(symbol));
				} else {
					std::uint32_t repeat = 0;
					std::uint8_t value = 0;
					if (symbol == 16) {
						if (combined.empty())
							throw std::runtime_error("invalid zlib repeated code length");
						repeat = bits.Read(2) + 3;
						value = combined.back();
					} else if (symbol == 17) {
						repeat = bits.Read(3) + 3;
					} else if (symbol == 18) {
						repeat = bits.Read(7) + 11;
					} else {
						throw std::runtime_error("invalid zlib code-length symbol");
					}
					if (repeat > literal_count + distance_count - combined.size())
						throw std::runtime_error("zlib code-length repeat exceeds its table");
					combined.insert(combined.end(), repeat, value);
				}
			}
			literal_lengths.assign(combined.begin(), combined.begin() + literal_count);
			distance_lengths.assign(combined.begin() + literal_count, combined.end());
			if (literal_lengths.size() <= 256 || literal_lengths[256] == 0)
				throw std::runtime_error("zlib literal tree has no end-of-block code");
		}
		const DeflateHuffman literals(literal_lengths);
		const DeflateHuffman distances(distance_lengths, true);
		decode_compressed_block(literals, distances);
	}
	bits.AlignByte();
	const auto trailer = 2 + bits.ByteOffset();
	if (trailer > size || size - trailer < 4)
		throw std::runtime_error("truncated zlib Adler-32 trailer");
	const std::uint32_t expected = (static_cast<std::uint32_t>(bytes[trailer]) << 24) |
		(static_cast<std::uint32_t>(bytes[trailer + 1]) << 16) |
		(static_cast<std::uint32_t>(bytes[trailer + 2]) << 8) | bytes[trailer + 3];
	std::uint32_t a = 1;
	std::uint32_t b = 0;
	for (const auto byte : output) {
		a = (a + byte) % 65521;
		b = (b + a) % 65521;
	}
	if (((b << 16) | a) != expected)
		throw std::runtime_error("zlib symbol-pool Adler-32 mismatch");
	return output;
}

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

void Require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void RequireNativeExecution(const MachOImage& image)
{
#if defined(_M_ARM64) || defined(__aarch64__)
	if (!image.IsArm64()) {
		throw std::runtime_error(
			"non-ARM64 Mach-O execution requires a CPU translation engine on this ARM64 Windows host");
	}
#elif defined(_WIN64)
	if (image.Is32Bit()) {
		throw std::runtime_error(
			"32-bit Mach-O execution is not implemented on this x86_64 Windows host");
	}
	if (image.IsArm64()) {
		throw std::runtime_error(
			"ARM64 Mach-O execution is not implemented on this x86_64 Windows host");
	}
#else
	if (!image.Is32Bit()) {
		throw std::runtime_error(
			"64-bit Mach-O execution is not implemented on this Win32 host");
	}
#endif
}

bool RangeFits(std::size_t file_bytes, std::uint64_t offset, std::uint64_t bytes)
{
	return offset <= file_bytes && bytes <= file_bytes - offset;
}

std::uint64_t CheckedAddOffset(std::uint64_t left, std::uint64_t right)
{
	if (right > std::numeric_limits<std::uint64_t>::max() - left)
		throw std::overflow_error("Mach-O segment offset addition overflow");
	return left + right;
}

std::uint64_t CheckedMultiplyOffset(std::uint64_t left, std::uint64_t right)
{
	if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
		throw std::overflow_error("Mach-O segment offset multiplication overflow");
	return left * right;
}

std::uintptr_t AddSignedAddress(std::uintptr_t address, std::int64_t addend)
{
	if (addend >= 0) {
		const auto positive = static_cast<std::uint64_t>(addend);
		if (positive > std::numeric_limits<std::uintptr_t>::max() - address)
			throw std::overflow_error("Mach-O bind target address overflow");
		return address + static_cast<std::uintptr_t>(positive);
	}
	const auto magnitude = static_cast<std::uint64_t>(-(addend + 1)) + 1;
	if (magnitude > address)
		throw std::overflow_error("Mach-O bind target address underflow");
	return address - static_cast<std::uintptr_t>(magnitude);
}

std::uint64_t ReadULEB(const std::uint8_t* bytes, std::size_t size, std::size_t& cursor)
{
	std::uint64_t value = 0;
	unsigned shift = 0;
	while (cursor < size && shift < 64) {
		const auto byte = bytes[cursor++];
		if (shift == 63 && (byte & 0x7e) != 0) {
			throw std::runtime_error("Mach-O ULEB128 overflow");
		}
		value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0) {
			return value;
		}
		shift += 7;
	}
	throw std::runtime_error("truncated Mach-O ULEB128 value");
}

std::int64_t ReadSLEB(const std::uint8_t* bytes, std::size_t size, std::size_t& cursor)
{
	std::uint64_t value = 0;
	unsigned shift = 0;
	std::uint8_t byte = 0;
	while (cursor < size && shift < 64) {
		byte = bytes[cursor++];
		value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
		shift += 7;
		if ((byte & 0x80) == 0) {
			if (shift < 64 && (byte & 0x40) != 0) {
				value |= ~std::uint64_t(0) << shift;
			}
			return static_cast<std::int64_t>(value);
		}
	}
	throw std::runtime_error("truncated Mach-O SLEB128 value");
}

std::int32_t DecodeSpecialDylibOrdinal(std::uint8_t immediate)
{
	return static_cast<std::int32_t>(static_cast<std::int8_t>(immediate | 0xf0));
}

void ParseSimpleBindStream(const std::uint8_t* bytes, std::size_t size,
	std::vector<DyldBindAction>& actions, bool weak, bool lazy)
{
	std::size_t cursor = 0;
	// Weak-bind streams coalesce definitions across loaded images instead of
	// naming one LC_LOAD_*_DYLIB dependency.
	std::int32_t ordinal = weak ? -2 : 0;
	std::string symbol;
	std::uint8_t type = 1;
	std::int64_t addend = 0;
	std::uint32_t segment_index = 0;
	std::uint64_t segment_offset = 0;
	bool weak_import = false;
	std::size_t lazy_sequence_offset = 0;
	while (cursor < size) {
		const auto opcode = bytes[cursor++];
		const auto action_offset = cursor - 1;
		const auto operation = opcode & 0xf0;
		const auto immediate = opcode & 0x0f;
		switch (operation) {
		case 0x00:
			Require(opcode == 0, "invalid Mach-O weak/lazy bind terminator");
			// A lazy-bind stream concatenates one DONE-terminated opcode
			// sequence per symbol. DONE terminates only the current symbol;
			// dyld continues until it reaches the end of the whole stream.
			if (lazy) lazy_sequence_offset = cursor;
			else cursor = size;
			break;
		case 0x10:
			ordinal = immediate;
			break;
		case 0x20:
			ordinal = static_cast<std::int32_t>(ReadULEB(bytes, size, cursor));
			break;
		case 0x30:
			ordinal = DecodeSpecialDylibOrdinal(immediate);
			break;
		case 0x40: {
			weak_import = (immediate & 1) != 0;
			const auto start = cursor;
			while (cursor < size && bytes[cursor] != 0) {
				++cursor;
			}
			Require(cursor < size, "unterminated Mach-O weak/lazy symbol");
			symbol.assign(reinterpret_cast<const char*>(bytes + start), cursor - start);
			++cursor;
			break;
		}
		case 0x50:
			type = immediate;
			break;
		case 0x60:
			addend = ReadSLEB(bytes, size, cursor);
			break;
		case 0x70:
			segment_index = immediate;
			segment_offset = ReadULEB(bytes, size, cursor);
			break;
		case 0x80:
			segment_offset = CheckedAddOffset(segment_offset,
				ReadULEB(bytes, size, cursor));
			break;
		case 0x90:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset, 8);
			break;
		case 0xa0:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset,
				CheckedAddOffset(ReadULEB(bytes, size, cursor), 8));
			break;
		case 0xb0:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset,
				static_cast<std::uint64_t>(immediate) * 8 + 8);
			break;
		case 0xc0: {
			const auto repeat = ReadULEB(bytes, size, cursor);
			const auto skip = ReadULEB(bytes, size, cursor);
			Require(repeat <= 0x100000,
				"Mach-O weak/lazy bind repeat count is unreasonable");
			for (std::uint64_t index = 0; index < repeat; ++index) {
				actions.push_back({symbol, segment_index, segment_offset, type, addend,
					ordinal, weak || weak_import, lazy, false, 0,
					lazy ? lazy_sequence_offset : action_offset, weak});
				segment_offset = CheckedAddOffset(segment_offset,
					CheckedAddOffset(skip, 8));
			}
			break;
		}
		default:
			throw std::runtime_error("unsupported Mach-O weak/lazy bind opcode");
		}
	}
}

constexpr std::uint64_t kPageSize = 0x1000;
constexpr std::uint8_t N_TYPE = 0x0e;
constexpr std::uint8_t N_UNDF = 0x00;
constexpr std::uint8_t N_ABS = 0x02;
constexpr std::uint8_t N_INDR = 0x0a;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10;
constexpr std::uint32_t R_SYMBOL_MASK = 0x00ffffff;
constexpr std::uint32_t R_PCREL = 0x01000000;
constexpr std::uint32_t R_LENGTH = 0x06000000;
constexpr std::uint32_t R_EXTERN = 0x08000000;
constexpr std::uint32_t R_TYPE = 0xf0000000;

std::uint64_t AlignDown(std::uint64_t value)
{
	return value & ~(kPageSize - 1);
}

std::uint64_t AlignUp(std::uint64_t value)
{
	if (value > std::numeric_limits<std::uint64_t>::max() - (kPageSize - 1)) {
		throw std::overflow_error("Mach-O address alignment overflow");
	}
	return (value + kPageSize - 1) & ~(kPageSize - 1);
}

DWORD ProtectionFor(std::int32_t protection)
{
	const bool read = (protection & 1) != 0;
	const bool write = (protection & 2) != 0;
	const bool execute = (protection & 4) != 0;
	if (execute) {
		return write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	}
	return write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS);
}

std::string SegmentName(const char (&name)[16])
{
	const auto length = std::find(std::begin(name), std::end(name), '\0');
	return std::string(name, length);
}

std::uint32_t ReadBigEndian32(const std::uint8_t* bytes)
{
	return (static_cast<std::uint32_t>(bytes[0]) << 24) |
		(static_cast<std::uint32_t>(bytes[1]) << 16) |
		(static_cast<std::uint32_t>(bytes[2]) << 8) |
		static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t ReadBigEndian64(const std::uint8_t* bytes)
{
	return (static_cast<std::uint64_t>(ReadBigEndian32(bytes)) << 32) |
		ReadBigEndian32(bytes + 4);
}

} // namespace

MachOImage MachOImage::Open(const std::wstring& path)
{
	const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		ThrowLastError("CreateFileW(Mach-O)");
	}

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
		static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
		CloseHandle(file);
		ThrowLastError("GetFileSizeEx(Mach-O)");
	}
	const auto mapped_file_bytes = static_cast<std::size_t>(size.QuadPart);
	const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (mapping == nullptr) {
		CloseHandle(file);
		ThrowLastError("CreateFileMappingW(Mach-O)");
	}
	const auto* mapped_view = static_cast<const std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
	if (mapped_view == nullptr) {
		CloseHandle(mapping);
		CloseHandle(file);
		ThrowLastError("MapViewOfFile(Mach-O)");
	}

	try {
		std::size_t image_offset = 0;
		std::size_t image_bytes = mapped_file_bytes;
		const auto raw_magic = mapped_file_bytes >= sizeof(std::uint32_t) ?
			*reinterpret_cast<const std::uint32_t*>(mapped_view) : 0;
		if (raw_magic == FAT_MAGIC_BE || raw_magic == FAT_MAGIC_64_BE) {
			Require(mapped_file_bytes >= 8, "Fat Mach-O header is truncated");
			const auto architecture_count = ReadBigEndian32(mapped_view + 4);
			Require(architecture_count > 0 && architecture_count <= 128,
				"Fat Mach-O has an unreasonable architecture count");
			const auto architecture_bytes = raw_magic == FAT_MAGIC_64_BE ? 32u : 20u;
			Require(RangeFits(mapped_file_bytes, 8,
				static_cast<std::uint64_t>(architecture_count) * architecture_bytes),
				"Fat Mach-O architecture table is outside the file");
			bool found_x86_64 = false;
			bool found_x86 = false;
			bool found_arm64 = false;
			std::size_t x86_64_offset = 0;
			std::size_t x86_64_bytes = 0;
			std::size_t x86_offset = 0;
			std::size_t x86_bytes = 0;
			std::size_t arm64_offset = 0;
			std::size_t arm64_bytes = 0;
			for (std::uint32_t index = 0; index < architecture_count; ++index) {
				const auto* architecture = mapped_view + 8 +
					static_cast<std::size_t>(index) * architecture_bytes;
				const auto cpu_type = static_cast<std::int32_t>(ReadBigEndian32(architecture));
				if (cpu_type != CPU_TYPE_X86_64 && cpu_type != CPU_TYPE_X86 &&
					cpu_type != CPU_TYPE_ARM64) {
					continue;
				}
				const auto candidate_offset = raw_magic == FAT_MAGIC_64_BE ?
					static_cast<std::size_t>(ReadBigEndian64(architecture + 8)) :
					static_cast<std::size_t>(ReadBigEndian32(architecture + 8));
				const auto candidate_bytes = raw_magic == FAT_MAGIC_64_BE ?
					static_cast<std::size_t>(ReadBigEndian64(architecture + 16)) :
					static_cast<std::size_t>(ReadBigEndian32(architecture + 12));
				Require(RangeFits(mapped_file_bytes, candidate_offset, candidate_bytes),
					"Fat Mach-O slice is outside the file");
				if (cpu_type == CPU_TYPE_X86_64) {
					found_x86_64 = true;
					x86_64_offset = candidate_offset;
					x86_64_bytes = candidate_bytes;
					continue;
				}
				if (cpu_type == CPU_TYPE_X86) {
					if (!found_x86) {
						x86_offset = candidate_offset;
						x86_bytes = candidate_bytes;
						found_x86 = true;
					}
					continue;
				}
				found_arm64 = true;
				arm64_offset = candidate_offset;
				arm64_bytes = candidate_bytes;
			}
#if defined(_M_ARM64) || defined(__aarch64__)
			if (found_arm64) {
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else if (found_x86_64) {
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else {
				Require(found_x86, "Fat Mach-O has no supported slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			}
#else
			if (found_x86_64) {
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else if (found_arm64) {
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else {
				Require(found_x86, "Fat Mach-O has no supported slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			}
#endif
		}
		const auto file_bytes = image_bytes;
		const auto slice_magic = image_bytes >= sizeof(std::uint32_t) ?
			*reinterpret_cast<const std::uint32_t*>(mapped_view + image_offset) : 0;
		const bool is_32_bit = slice_magic == MH_MAGIC;
		Require(image_bytes >= (is_32_bit ? sizeof(MachHeader32) : sizeof(MachHeader64)),
			"Mach-O file is smaller than its header");
		const auto* view = mapped_view + image_offset;
		MachHeader64 normalized_header{};
		if (is_32_bit) {
			const auto* header32 = reinterpret_cast<const MachHeader32*>(view);
			normalized_header = {header32->magic, header32->cpu_type, header32->cpu_subtype,
				header32->file_type, header32->command_count, header32->command_bytes,
				header32->flags, 0};
		} else {
			const auto* header64 = reinterpret_cast<const MachHeader64*>(view);
			Require(header64->magic == MH_MAGIC_64,
				"unsupported Mach-O magic; expected 32-bit or 64-bit little-endian");
			normalized_header = *header64;
		}
		const auto* header = &normalized_header;
		Require(header->cpu_type == CPU_TYPE_X86_64 || header->cpu_type == CPU_TYPE_X86 ||
			header->cpu_type == CPU_TYPE_ARM64,
			"unsupported Mach-O CPU type; expected x86, x86_64, or ARM64");
		Require(header->file_type == MH_EXECUTE || header->file_type == MH_DYLIB ||
			header->file_type == MH_BUNDLE,
			"Mach-O is neither an executable, dylib, nor bundle");
		Require(header->command_count <= 4096, "Mach-O has an unreasonable load-command count");
		Require(RangeFits(file_bytes, sizeof(MachHeader64), header->command_bytes),
			"Mach-O load-command table is outside the file");

		std::vector<MachOSegment> segments;
		std::vector<MachOSection> sections;
		std::vector<std::string> dependencies;
		std::vector<std::string> weak_dependencies;
		std::vector<std::string> rpaths;
		std::vector<MachOSymbol> symbols;
		std::vector<std::uint32_t> indirect_symbols;
		std::vector<MachOReexport> reexports;
		std::array<std::uint8_t, 16> uuid{};
		std::string install_name;
		std::string dylinker_path;
		std::vector<MachORelocation> relocations;
		const SymtabCommand* symtab = nullptr;
		const DYSymtabCommand* dysymtab = nullptr;
		const DyldInfoCommand* dyld_info = nullptr;
		const LinkeditDataCommand* chained_command = nullptr;
		const LinkeditDataCommand* exports_command = nullptr;
		const LinkeditDataCommand* code_signature_command = nullptr;
		std::uint64_t entry_offset = 0;
		bool has_main = false;
		std::uint64_t thread_rip = 0;
		bool has_thread = false;
		std::size_t cursor = is_32_bit ? sizeof(MachHeader32) : sizeof(MachHeader64);
		const auto commands_end = cursor + header->command_bytes;
		for (std::uint32_t index = 0; index < header->command_count; ++index) {
			Require(cursor <= commands_end && commands_end - cursor >= sizeof(LoadCommand),
				"Mach-O load command header is truncated");
			const auto* command = reinterpret_cast<const LoadCommand*>(view + cursor);
			Require(command->command_bytes >= sizeof(LoadCommand) &&
				command->command_bytes <= commands_end - cursor,
				"Mach-O load command has an invalid size");

			if (command->command == LC_SEGMENT_64) {
				Require(command->command_bytes >= sizeof(SegmentCommand64),
					"Mach-O segment command is truncated");
				const auto* segment = reinterpret_cast<const SegmentCommand64*>(command);
				Require(segment->section_count <=
					(command->command_bytes - sizeof(SegmentCommand64)) / sizeof(Section64),
					"Mach-O section table is truncated");
				Require(RangeFits(file_bytes, segment->file_offset, segment->file_size),
					"Mach-O segment points outside the file");
				segments.push_back({SegmentName(segment->segment_name), segment->vm_address,
					segment->vm_size, segment->file_offset, segment->file_size,
					segment->max_protection, segment->initial_protection});
				const auto* section = reinterpret_cast<const Section64*>(
					reinterpret_cast<const std::uint8_t*>(segment) + sizeof(SegmentCommand64));
				for (std::uint32_t section_index = 0;
					section_index < segment->section_count; ++section_index) {
					sections.push_back({SegmentName(section[section_index].segment_name),
						SegmentName(section[section_index].section_name),
						section[section_index].address, section[section_index].size,
						section[section_index].file_offset, section[section_index].flags,
						section[section_index].reserved2});
				}
			} else if (command->command == LC_SEGMENT) {
				Require(is_32_bit && command->command_bytes >= sizeof(SegmentCommand32),
					"Mach-O 32-bit segment command is truncated");
				const auto* segment = reinterpret_cast<const SegmentCommand32*>(command);
				Require(segment->section_count <=
					(command->command_bytes - sizeof(SegmentCommand32)) / sizeof(Section32),
					"Mach-O 32-bit section table is truncated");
				Require(RangeFits(file_bytes, segment->file_offset, segment->file_size),
					"Mach-O 32-bit segment points outside the file");
				segments.push_back({SegmentName(segment->segment_name), segment->vm_address,
					segment->vm_size, segment->file_offset, segment->file_size,
					segment->max_protection, segment->initial_protection});
				const auto* section = reinterpret_cast<const Section32*>(
					reinterpret_cast<const std::uint8_t*>(segment) + sizeof(SegmentCommand32));
				for (std::uint32_t section_index = 0;
					section_index < segment->section_count; ++section_index) {
					sections.push_back({SegmentName(section[section_index].segment_name),
						SegmentName(section[section_index].section_name),
						section[section_index].address, section[section_index].size,
						section[section_index].file_offset, section[section_index].flags,
						section[section_index].reserved2});
				}
			} else if (command->command == LC_SYMTAB) {
				Require(command->command_bytes >= sizeof(SymtabCommand),
					"Mach-O symbol-table command is truncated");
				symtab = reinterpret_cast<const SymtabCommand*>(command);
			} else if (command->command == LC_RPATH) {
				Require(command->command_bytes >= sizeof(RPathCommand),
					"Mach-O rpath command is truncated");
				const auto* rpath = reinterpret_cast<const RPathCommand*>(command);
				Require(rpath->path_offset >= sizeof(RPathCommand) &&
					rpath->path_offset < command->command_bytes,
					"Mach-O rpath offset is invalid");
				const auto* path = reinterpret_cast<const char*>(command) + rpath->path_offset;
				const auto path_bytes = command->command_bytes - rpath->path_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(path, '\0', path_bytes));
				Require(terminator != nullptr, "Mach-O rpath is not terminated");
				Require(terminator != path, "Mach-O rpath is empty");
				rpaths.emplace_back(path, terminator);
			} else if (command->command == LC_DYSYMTAB) {
				Require(command->command_bytes >= sizeof(DYSymtabCommand),
					"Mach-O dynamic symbol-table command is truncated");
				dysymtab = reinterpret_cast<const DYSymtabCommand*>(command);
			} else if (command->command == LC_DYLD_INFO ||
				command->command == LC_DYLD_INFO_ONLY) {
				Require(command->command_bytes >= sizeof(DyldInfoCommand),
					"Mach-O dyld-info command is truncated");
				dyld_info = reinterpret_cast<const DyldInfoCommand*>(command);
			} else if (command->command == LC_DYLD_CHAINED_FIXUPS) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O chained-fixups command is truncated");
				chained_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_DYLD_EXPORTS_TRIE) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O exports-trie command is truncated");
				exports_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_CODE_SIGNATURE) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O code-signature command is truncated");
				code_signature_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_MAIN) {
				Require(command->command_bytes >= sizeof(MainCommand),
					"Mach-O main command is truncated");
				const auto* main = reinterpret_cast<const MainCommand*>(command);
				entry_offset = main->entry_offset;
				has_main = true;
			} else if (command->command == LC_UUID) {
				Require(command->command_bytes >= 24, "Mach-O UUID command is truncated");
				std::memcpy(uuid.data(), reinterpret_cast<const std::uint8_t*>(command) + 8, 16);
			} else if (command->command == LC_UNIXTHREAD) {
				std::uint32_t flavor = 0;
				std::uint32_t count = 0;
				std::memcpy(&flavor, reinterpret_cast<const std::uint8_t*>(command) + 8,
					sizeof(flavor));
				std::memcpy(&count, reinterpret_cast<const std::uint8_t*>(command) + 12,
					sizeof(count));
				if (header->cpu_type == CPU_TYPE_X86_64) {
					Require(command->command_bytes >= 16 + 17 * sizeof(std::uint64_t) &&
						flavor == 4 && count >= 17,
						"unsupported Mach-O UNIXTHREAD flavor; expected x86_64");
					std::memcpy(&thread_rip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 16 * sizeof(std::uint64_t), sizeof(thread_rip));
				} else if (header->cpu_type == CPU_TYPE_X86) {
					Require(command->command_bytes >= 16 + 16 * sizeof(std::uint32_t) &&
						flavor == 1 && count >= 16,
						"unsupported Mach-O UNIXTHREAD flavor; expected i386");
					std::uint32_t thread_eip = 0;
					std::memcpy(&thread_eip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 10 * sizeof(std::uint32_t), sizeof(thread_eip));
					thread_rip = thread_eip;
				} else {
					// ARM_THREAD_STATE64 contains 68 32-bit words; PC is
					// the 33rd 64-bit register in its payload.
					Require(command->command_bytes >= 16 + 68 * sizeof(std::uint32_t) &&
						flavor == 6 && count >= 68,
						"unsupported Mach-O UNIXTHREAD flavor; expected ARM64");
					std::memcpy(&thread_rip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 32 * sizeof(std::uint64_t), sizeof(thread_rip));
				}
				has_thread = true;
			} else if (command->command == LC_LOAD_DYLINKER) {
				Require(command->command_bytes >= sizeof(DylinkerCommand),
					"Mach-O dylinker command is truncated");
				const auto* dylinker = reinterpret_cast<const DylinkerCommand*>(command);
				Require(dylinker->name_offset >= sizeof(DylinkerCommand) &&
					dylinker->name_offset < command->command_bytes,
					"Mach-O dylinker name offset is invalid");
				const auto* name = reinterpret_cast<const char*>(command) + dylinker->name_offset;
				const auto name_bytes = command->command_bytes - dylinker->name_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', name_bytes));
				Require(terminator != nullptr, "Mach-O dylinker name is not terminated");
				Require(terminator != name, "Mach-O dylinker name is empty");
				dylinker_path.assign(name, terminator);
			} else if (command->command == LC_ID_DYLIB || command->command == LC_LOAD_DYLIB ||
				command->command == LC_LOAD_WEAK_DYLIB ||
				command->command == LC_REEXPORT_DYLIB ||
				command->command == LC_LOAD_UPWARD_DYLIB) {
				Require(command->command_bytes >= sizeof(DylibCommand),
					"Mach-O dylib command is truncated");
				const auto* dylib = reinterpret_cast<const DylibCommand*>(command);
				Require(dylib->name_offset >= sizeof(DylibCommand) &&
					dylib->name_offset < command->command_bytes,
					"Mach-O dylib name offset is invalid");
				const auto* name = reinterpret_cast<const char*>(command) + dylib->name_offset;
				const auto name_bytes = command->command_bytes - dylib->name_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', name_bytes));
				Require(terminator != nullptr, "Mach-O dylib name is not terminated");
				Require(terminator != name, "Mach-O dylib name is empty");
				if (command->command == LC_ID_DYLIB)
					install_name.assign(name, terminator);
				else
					dependencies.emplace_back(name, terminator);
				if (command->command == LC_LOAD_WEAK_DYLIB) {
					weak_dependencies.emplace_back(name, terminator);
				}
			}
			cursor += command->command_bytes;
		}
		Require(cursor == commands_end, "Mach-O load-command table has trailing bytes");
		if (!has_main && has_thread) {
			for (const auto& segment : segments) {
				if (thread_rip >= segment.vm_address &&
					thread_rip - segment.vm_address < segment.file_size) {
					entry_offset = segment.file_offset + (thread_rip - segment.vm_address);
					has_main = true;
					break;
				}
			}
		}
		Require(header->file_type != MH_EXECUTE || has_main,
			"Mach-O executable has no LC_MAIN entry point");
		if (code_signature_command != nullptr) {
			Require(RangeFits(file_bytes, code_signature_command->data_offset,
				code_signature_command->data_size),
				"Mach-O code-signature payload is outside the file");
		}
		if (symtab != nullptr) {
			Require(RangeFits(file_bytes, symtab->string_offset, symtab->string_bytes),
				"Mach-O string table is outside the file");
			const auto* strings = reinterpret_cast<const char*>(view + symtab->string_offset);
			const auto make_symbol = [&](std::string name, std::uint8_t type,
				std::uint8_t section, std::uint16_t description,
				std::uint64_t value) {
				MachOSymbol symbol{std::move(name), type, section, description, value};
				if ((type & N_TYPE) == N_INDR) {
					Require(value < symtab->string_bytes,
						"Mach-O indirect symbol target is outside the string table");
					const auto* target = strings + value;
					const auto remaining = symtab->string_bytes -
						static_cast<std::uint32_t>(value);
					const auto* terminator = static_cast<const char*>(
						std::memchr(target, '\0', remaining));
					Require(terminator != nullptr,
						"Mach-O indirect symbol target is not terminated");
					symbol.indirect_target.assign(target, terminator);
				}
				return symbol;
			};
			if (is_32_bit) {
				Require(RangeFits(file_bytes, symtab->symbol_offset,
					static_cast<std::uint64_t>(symtab->symbol_count) * sizeof(NList32)),
					"Mach-O 32-bit symbol table is outside the file");
				const auto* entries = reinterpret_cast<const NList32*>(view + symtab->symbol_offset);
				for (std::uint32_t index = 0; index < symtab->symbol_count; ++index) {
					const auto& entry = entries[index];
					Require(entry.name_offset < symtab->string_bytes,
						"Mach-O symbol name offset is outside the string table");
					const auto* name = strings + entry.name_offset;
					const auto remaining = symtab->string_bytes - entry.name_offset;
					const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
					Require(terminator != nullptr, "Mach-O symbol name is not terminated");
					symbols.push_back(make_symbol(std::string(name, terminator),
						entry.type, entry.section, entry.description, entry.value));
				}
			} else {
				Require(RangeFits(file_bytes, symtab->symbol_offset,
					static_cast<std::uint64_t>(symtab->symbol_count) * sizeof(NList64)),
					"Mach-O symbol table is outside the file");
				const auto* entries = reinterpret_cast<const NList64*>(view + symtab->symbol_offset);
				for (std::uint32_t index = 0; index < symtab->symbol_count; ++index) {
					const auto& entry = entries[index];
					Require(entry.name_offset < symtab->string_bytes,
						"Mach-O symbol name offset is outside the string table");
					const auto* name = strings + entry.name_offset;
					const auto remaining = symtab->string_bytes - entry.name_offset;
					const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
					Require(terminator != nullptr, "Mach-O symbol name is not terminated");
					symbols.push_back(make_symbol(std::string(name, terminator),
						entry.type, entry.section, entry.description, entry.value));
				}
			}
		}
		if (dysymtab != nullptr) {
			if (dysymtab->indirect_symbol_count != 0) {
				Require(symtab != nullptr, "Mach-O indirect symbols require LC_SYMTAB");
				Require(RangeFits(file_bytes, dysymtab->indirect_symbol_offset,
					static_cast<std::uint64_t>(dysymtab->indirect_symbol_count) * sizeof(std::uint32_t)),
					"Mach-O indirect symbol table is outside the file");
				const auto* entries = reinterpret_cast<const std::uint32_t*>(
					view + dysymtab->indirect_symbol_offset);
				for (std::uint32_t index = 0; index < dysymtab->indirect_symbol_count; ++index) {
					const auto symbol_index = entries[index];
					const auto special = symbol_index & 0xC0000000u;
					Require(special == 0 || special == 0xC0000000u ||
						symbol_index < symtab->symbol_count,
						"Mach-O indirect symbol index is outside LC_SYMTAB");
					indirect_symbols.push_back(symbol_index);
				}
			}
			const auto read_relocations = [&](std::uint32_t offset, std::uint32_t count) {
				Require(RangeFits(file_bytes, offset,
					static_cast<std::uint64_t>(count) * sizeof(RelocationInfo)),
					"Mach-O relocation table is outside the file");
				const auto* entries = reinterpret_cast<const RelocationInfo*>(view + offset);
				bool pending_addend = false;
				std::int32_t pending_address = 0;
				std::int64_t pending_value = 0;
				for (std::uint32_t index = 0; index < count; ++index) {
					const auto raw_address = static_cast<std::uint32_t>(entries[index].address);
					if ((raw_address & 0x80000000u) != 0) {
						const auto type = static_cast<std::uint8_t>((raw_address >> 24) & 0x0f);
						MachORelocation relocation{
							static_cast<std::int32_t>(raw_address & 0x00ffffffu), 0,
							(raw_address & 0x40000000u) != 0,
							static_cast<std::uint8_t>((raw_address >> 28) & 0x3), false,
							type, false, 0, true,
							static_cast<std::int32_t>(entries[index].info)};
						relocations.push_back(relocation);
						continue;
					}
					const auto info = entries[index].info;
					const auto type = static_cast<std::uint8_t>((info & R_TYPE) >> 28);
					const auto external = (info & R_EXTERN) != 0;
					if (header->cpu_type == CPU_TYPE_ARM64 && type == 10) {
						Require(!external && (info & R_PCREL) == 0 &&
							((info & R_LENGTH) >> 25) == 2 && !pending_addend,
							"invalid ARM64_RELOC_ADDEND record");
						const auto raw = info & R_SYMBOL_MASK;
						pending_value = (raw & 0x00800000u) != 0 ?
							static_cast<std::int64_t>(raw | 0xff000000u) :
							static_cast<std::int64_t>(raw);
						pending_address = entries[index].address;
						pending_addend = true;
						continue;
					}
					MachORelocation relocation{entries[index].address,
						info & R_SYMBOL_MASK, (info & R_PCREL) != 0,
						static_cast<std::uint8_t>((info & R_LENGTH) >> 25),
						external, type};
					if (pending_addend) {
						Require(entries[index].address == pending_address,
							"ARM64_RELOC_ADDEND does not precede its paired relocation");
						relocation.has_explicit_addend = true;
						relocation.explicit_addend = pending_value;
						pending_addend = false;
					}
					relocations.push_back(relocation);
				}
				Require(!pending_addend, "ARM64_RELOC_ADDEND has no paired relocation");
			};
			read_relocations(dysymtab->external_relocation_offset,
				dysymtab->external_relocation_count);
			read_relocations(dysymtab->local_relocation_offset,
				dysymtab->local_relocation_count);
		}
		std::vector<DyldBindAction> bind_actions;
		std::vector<DyldBindAction> threaded_bind_targets;
		if (dyld_info != nullptr && dyld_info->binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->binding_offset, dyld_info->binding_size),
				"Mach-O dyld binding stream is outside the file");
			const auto* bytes = view + dyld_info->binding_offset;
			const auto size = static_cast<std::size_t>(dyld_info->binding_size);
			std::size_t cursor = 0;
			std::int32_t ordinal = 0;
			std::string symbol;
			std::uint8_t type = 1;
			std::int64_t addend = 0;
			std::uint32_t segment_index = 0;
			std::uint64_t segment_offset = 0;
			std::uint64_t threaded_table_size = 0;
			bool weak_import = false;
			bool collecting_threaded_targets = false;
			const auto emit_bind = [&]() {
				const DyldBindAction action{symbol, segment_index, segment_offset, type,
					addend, ordinal, weak_import, false, false, 0};
				if (collecting_threaded_targets) {
					threaded_bind_targets.push_back(action);
				} else {
					bind_actions.push_back(action);
					segment_offset = CheckedAddOffset(segment_offset, 8);
				}
			};
			while (cursor < size) {
				const auto opcode = bytes[cursor++];
				const auto operation = opcode & 0xf0;
				const auto immediate = opcode & 0x0f;
				switch (operation) {
				case 0x00:
					Require(opcode == 0, "invalid Mach-O dyld bind terminator");
					cursor = size;
					break;
				case 0x10:
					ordinal = immediate;
					break;
				case 0x20:
					ordinal = static_cast<std::int32_t>(ReadULEB(bytes, size, cursor));
					break;
				case 0x30:
					ordinal = DecodeSpecialDylibOrdinal(immediate);
					break;
				case 0x40: {
					weak_import = (immediate & 1) != 0;
					const auto start = cursor;
					while (cursor < size && bytes[cursor] != 0) {
						++cursor;
					}
					Require(cursor < size, "unterminated Mach-O dyld symbol");
					symbol.assign(reinterpret_cast<const char*>(bytes + start), cursor - start);
					++cursor;
					break;
				}
				case 0x50:
					type = immediate;
					break;
				case 0x60:
					addend = ReadSLEB(bytes, size, cursor);
					break;
				case 0x70:
					segment_index = immediate;
					segment_offset = ReadULEB(bytes, size, cursor);
					break;
				case 0x80:
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x90:
					emit_bind();
					break;
				case 0xa0:
					emit_bind();
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0xb0:
					emit_bind();
					segment_offset = CheckedAddOffset(segment_offset,
						static_cast<std::uint64_t>(immediate) * 8);
					break;
				case 0xc0:
				{
					const auto repeat = ReadULEB(bytes, size, cursor);
					const auto skip = ReadULEB(bytes, size, cursor);
					Require(repeat <= 0x100000,
						"Mach-O bind repeat count is unreasonable");
					for (std::uint64_t index = 0; index < repeat; ++index) {
						emit_bind();
						segment_offset = CheckedAddOffset(segment_offset, skip);
					}
					break;
				}
					break;
				case 0xd0:
					if (immediate == 0) {
						threaded_table_size = ReadULEB(bytes, size, cursor);
						Require(threaded_table_size <= 0x100000,
							"Mach-O threaded bind table is unreasonable");
						threaded_bind_targets.clear();
						collecting_threaded_targets = true;
					} else if (immediate == 1) {
						Require(threaded_table_size != 0,
							"Mach-O threaded bind apply has no ordinal table");
						Require(threaded_bind_targets.size() == threaded_table_size,
							"Mach-O threaded bind ordinal table is incomplete");
						bind_actions.push_back({symbol, segment_index, segment_offset, type,
							addend, ordinal, false, false, true,
							static_cast<std::size_t>(threaded_table_size)});
						collecting_threaded_targets = false;
					} else {
						throw std::runtime_error("unsupported Mach-O threaded bind opcode");
					}
					break;
				default:
					throw std::runtime_error("unsupported Mach-O dyld bind opcode");
				}
			}
		}
		if (dyld_info != nullptr && dyld_info->weak_binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->weak_binding_offset,
				dyld_info->weak_binding_size),
				"Mach-O weak-binding stream is outside the file");
			ParseSimpleBindStream(view + dyld_info->weak_binding_offset,
				dyld_info->weak_binding_size, bind_actions, true, false);
		}
		if (dyld_info != nullptr && dyld_info->lazy_binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->lazy_binding_offset,
				dyld_info->lazy_binding_size),
				"Mach-O lazy-binding stream is outside the file");
			ParseSimpleBindStream(view + dyld_info->lazy_binding_offset,
				dyld_info->lazy_binding_size, bind_actions, false, true);
		}
		std::vector<DyldRebaseAction> rebase_actions;
		if (dyld_info != nullptr && dyld_info->rebase_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->rebase_offset, dyld_info->rebase_size),
				"Mach-O dyld rebase stream is outside the file");
			const auto* bytes = view + dyld_info->rebase_offset;
			const auto size = static_cast<std::size_t>(dyld_info->rebase_size);
			std::size_t cursor = 0;
			std::uint32_t segment_index = 0;
			std::uint64_t segment_offset = 0;
			std::uint8_t type = 1;
			const std::uint64_t pointer_size = is_32_bit ? 4u : 8u;
			const auto emit_rebases = [&](std::uint64_t count) {
				Require(count <= 0x100000,
					"Mach-O dyld rebase count is unreasonable");
				if (count == 0) return;
				Require(segment_index < segments.size(),
					"Mach-O dyld rebase segment index is invalid");
				const auto& segment = segments[segment_index];
				const std::uint64_t width = type == 2 || type == 3 ? 4u : pointer_size;
				Require(segment_offset <= segment.vm_size &&
					segment.vm_size - segment_offset >= width,
					"Mach-O dyld rebase starts outside its segment");
				const auto available = 1 +
					(segment.vm_size - segment_offset - width) / pointer_size;
				Require(count <= available,
					"Mach-O dyld rebase sequence exceeds its segment");
				for (std::uint64_t index = 0; index < count; ++index) {
					rebase_actions.push_back({segment_index, segment_offset, type});
					segment_offset = CheckedAddOffset(segment_offset, pointer_size);
				}
			};
			while (cursor < size) {
				const auto opcode = bytes[cursor++];
				const auto operation = opcode & 0xf0;
				const auto immediate = opcode & 0x0f;
				switch (operation) {
				case 0x00:
					Require(opcode == 0, "invalid Mach-O dyld rebase terminator");
					cursor = size;
					break;
				case 0x10:
					type = immediate;
					break;
				case 0x20:
					segment_index = immediate;
					segment_offset = ReadULEB(bytes, size, cursor);
					break;
				case 0x30:
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x40:
					segment_offset = CheckedAddOffset(segment_offset,
						static_cast<std::uint64_t>(immediate) * pointer_size);
					break;
				case 0x50:
					emit_rebases(immediate);
					break;
				case 0x60:
					emit_rebases(ReadULEB(bytes, size, cursor));
					break;
				case 0x80:
					emit_rebases(1);
					segment_offset = CheckedAddOffset(segment_offset,
						static_cast<std::uint64_t>(immediate) * pointer_size);
					break;
				case 0x70:
					emit_rebases(1);
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x90: {
					const auto count = ReadULEB(bytes, size, cursor);
					const auto skip = ReadULEB(bytes, size, cursor);
					emit_rebases(count);
					segment_offset = CheckedAddOffset(segment_offset,
					CheckedMultiplyOffset(skip, count));
					break;
				}
				default:
					throw std::runtime_error("unsupported Mach-O dyld rebase opcode");
				}
			}
		}
		std::vector<DyldChainedFixup> chained_fixups;
		std::vector<DyldChainedImport> chained_imports;
		if (chained_command != nullptr) {
			Require(RangeFits(file_bytes, chained_command->data_offset,
				chained_command->data_size), "Mach-O chained-fixups payload is outside the file");
			Require(chained_command->data_size >= sizeof(ChainedFixupsHeader),
				"Mach-O chained-fixups header is truncated");
			const auto* payload = view + chained_command->data_offset;
			const auto* header = reinterpret_cast<const ChainedFixupsHeader*>(payload);
			Require(header->version == 0, "unsupported Mach-O chained-fixups version");
			Require(header->imports_format >= 1 && header->imports_format <= 3,
				"unsupported Mach-O chained import format");
			Require(header->symbols_format <= 1,
				"unsupported Mach-O chained symbol format");
			const std::uint64_t chained_import_size =
				header->imports_format == 1 ? 4u :
				header->imports_format == 2 ? 8u : 16u;
			Require(header->imports_offset <= chained_command->data_size &&
				chained_command->data_size - header->imports_offset >=
				static_cast<std::uint64_t>(header->imports_count) *
					chained_import_size,
				"Mach-O chained import table is outside the payload");
			Require(header->symbols_offset < chained_command->data_size,
				"Mach-O chained symbol pool is outside the payload");
			const auto* import_bytes = payload + header->imports_offset;
			std::vector<std::uint8_t> decompressed_symbols;
			const char* symbol_bytes = nullptr;
			std::size_t symbol_pool_size = 0;
			if (header->symbols_format == 0) {
				symbol_bytes = reinterpret_cast<const char*>(payload + header->symbols_offset);
				symbol_pool_size = chained_command->data_size - header->symbols_offset;
			} else {
				decompressed_symbols = InflateZlib(payload + header->symbols_offset,
					chained_command->data_size - header->symbols_offset);
				symbol_bytes = reinterpret_cast<const char*>(decompressed_symbols.data());
				symbol_pool_size = decompressed_symbols.size();
			}
			const auto decode_ordinal8 = [](std::uint32_t raw_ordinal) {
				const auto ordinal = static_cast<std::uint8_t>(raw_ordinal & 0xffu);
				return ordinal > 0xf0u ? static_cast<std::int32_t>(ordinal) - 0x100 :
					static_cast<std::int32_t>(ordinal);
			};
			const auto decode_ordinal16 = [](std::uint64_t raw_ordinal) {
				const auto ordinal = static_cast<std::uint16_t>(raw_ordinal & 0xffffu);
				return ordinal > 0xfff0u ? static_cast<std::int32_t>(ordinal) - 0x10000 :
					static_cast<std::int32_t>(ordinal);
			};
			for (std::uint32_t index = 0; index < header->imports_count; ++index) {
				std::uint32_t name_offset = 0;
				std::int32_t dylib_ordinal = 0;
				bool weak = false;
				std::int64_t addend = 0;
				if (header->imports_format == 1) {
					std::uint32_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 4, sizeof(raw));
					dylib_ordinal = decode_ordinal8(raw);
					weak = (raw & 0x100) != 0;
					name_offset = raw >> 9;
				} else if (header->imports_format == 2) {
					std::uint32_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 8, sizeof(raw));
					std::int32_t addend32 = 0;
					std::memcpy(&addend32, import_bytes + index * 8 + 4,
						sizeof(addend32));
					dylib_ordinal = decode_ordinal8(raw);
					weak = (raw & 0x100) != 0;
					name_offset = raw >> 9;
					addend = addend32;
				} else {
					std::uint64_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 16, sizeof(raw));
					std::memcpy(&addend, import_bytes + index * 16 + 8,
						sizeof(addend));
					dylib_ordinal = decode_ordinal16(raw);
					weak = (raw & (std::uint64_t{1} << 16)) != 0;
					name_offset = static_cast<std::uint32_t>(raw >> 32);
				}
			Require(name_offset < symbol_pool_size,
				"Mach-O chained import name offset is outside the symbol pool");
			const auto* name = symbol_bytes + name_offset;
			const auto* end = static_cast<const char*>(std::memchr(name, '\0',
				symbol_pool_size - name_offset));
			Require(end != nullptr, "unterminated Mach-O chained import symbol");
			chained_imports.push_back({dylib_ordinal, weak, std::string(name, end), addend});
		}
		if (exports_command != nullptr) {
			Require(RangeFits(file_bytes, exports_command->data_offset,
				exports_command->data_size), "Mach-O exports trie is outside the file");
			const auto* trie = view + exports_command->data_offset;
			const auto trie_size = static_cast<std::size_t>(exports_command->data_size);
			std::set<std::uint32_t> visited;
			const std::function<void(std::uint32_t, const std::string&)> visit =
				[&](std::uint32_t node_offset, const std::string& prefix) {
					Require(node_offset < trie_size && visited.insert(node_offset).second,
						"invalid or cyclic Mach-O exports trie node");
					std::size_t cursor = node_offset;
					const auto terminal_size = ReadULEB(trie, trie_size, cursor);
					Require(terminal_size <= trie_size - cursor,
						"Mach-O exports trie terminal is outside the payload");
					const auto terminal_end = cursor + static_cast<std::size_t>(terminal_size);
					if (terminal_size != 0) {
						const auto flags = ReadULEB(trie, terminal_end, cursor);
						if ((flags & 0x8) != 0) {
							const auto ordinal = ReadULEB(trie, terminal_end, cursor);
							const auto target_start = cursor;
							while (cursor < terminal_end && trie[cursor] != 0)
								++cursor;
							Require(cursor < terminal_end,
								"unterminated Mach-O re-export target");
							reexports.push_back({prefix,
								std::string(reinterpret_cast<const char*>(trie + target_start),
									cursor - target_start),
								static_cast<std::uint32_t>(ordinal)});
						} else {
							const auto address = ReadULEB(trie, terminal_end, cursor);
							const bool has_resolver = (flags &
								EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) != 0;
							const auto resolver = has_resolver ?
								ReadULEB(trie, terminal_end, cursor) : 0;
							if (!prefix.empty()) {
								const bool absolute = (flags &
									EXPORT_SYMBOL_FLAGS_KIND_MASK) ==
									EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE;
								const auto type = absolute ? 0x03u : 0x0fu;
								const auto description = (flags &
									EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION) != 0 ? 0x0080u : 0u;
								std::uint64_t image_base = 0;
								if (!absolute || has_resolver) {
									const auto text_segment = std::find_if(segments.begin(),
										segments.end(), [](const MachOSegment& segment) {
											return segment.name == "__TEXT";
										});
									Require(text_segment != segments.end(),
										"Mach-O export trie requires an __TEXT image base");
									image_base = text_segment->vm_address;
								}
								const auto preferred_address = absolute ? address :
									image_base + address;
								Require(absolute || preferred_address >= image_base,
									"Mach-O export address overflows its image base");
								const auto preferred_resolver = has_resolver ?
									image_base + resolver : 0;
								Require(!has_resolver || preferred_resolver >= image_base,
									"Mach-O resolver address overflows its image base");
								auto existing = std::find_if(symbols.begin(), symbols.end(),
									[&prefix](const MachOSymbol& symbol) {
										return symbol.name == prefix &&
											(symbol.type & N_TYPE) != N_UNDF;
									});
								if (existing == symbols.end()) {
									symbols.push_back({prefix,
										static_cast<std::uint8_t>(type), 1,
										static_cast<std::uint16_t>(description), preferred_address});
									existing = std::prev(symbols.end());
								} else {
									existing->type = static_cast<std::uint8_t>(type);
									existing->description = static_cast<std::uint16_t>(
										(existing->description & ~0x0080u) | description);
								}
								existing->has_resolver = has_resolver;
								existing->resolver_value = preferred_resolver;
							}
						}
						cursor = terminal_end;
					}
					Require(cursor < trie_size, "Mach-O exports trie node has no child count");
					const auto child_count = trie[cursor++];
					for (std::uint8_t child = 0; child < child_count; ++child) {
						const auto edge_start = cursor;
						while (cursor < trie_size && trie[cursor] != 0) {
							++cursor;
						}
						Require(cursor < trie_size, "unterminated Mach-O exports trie edge");
						const std::string edge(reinterpret_cast<const char*>(trie + edge_start),
							cursor - edge_start);
						++cursor;
						const auto child_offset = ReadULEB(trie, trie_size, cursor);
						visit(static_cast<std::uint32_t>(child_offset), prefix + edge);
					}
				};
			visit(0, {});
		}
			Require(header->starts_offset <= chained_command->data_size &&
				chained_command->data_size - header->starts_offset >= sizeof(std::uint32_t),
				"Mach-O chained-fixups starts table is outside the payload");
			const auto* starts = payload + header->starts_offset;
			const auto segment_count = *reinterpret_cast<const std::uint32_t*>(starts);
			Require(segment_count <= 4096 &&
				header->starts_offset + sizeof(std::uint32_t) +
				static_cast<std::uint64_t>(segment_count) * sizeof(std::uint32_t) <=
				chained_command->data_size, "Mach-O chained-fixups segment table is invalid");
			const auto* segment_offsets = reinterpret_cast<const std::uint32_t*>(starts + sizeof(std::uint32_t));
			for (std::uint32_t segment_index = 0; segment_index < segment_count; ++segment_index) {
				const auto segment_offset = segment_offsets[segment_index];
				if (segment_offset == 0) {
					continue;
				}
				const auto starts_bytes_available = header->starts_offset <=
					chained_command->data_size ?
					chained_command->data_size - header->starts_offset : 0;
				const bool starts_relative = segment_offset < starts_bytes_available &&
					starts_bytes_available - segment_offset >= sizeof(ChainedStartsSegment);
				const bool payload_relative = segment_offset < chained_command->data_size &&
					chained_command->data_size - segment_offset >= sizeof(ChainedStartsSegment);
				Require(starts_relative || payload_relative,
					"Mach-O chained-fixups segment starts are invalid");
				const auto* starts_candidate = reinterpret_cast<const ChainedStartsSegment*>(
					starts + segment_offset);
				const auto* payload_candidate = reinterpret_cast<const ChainedStartsSegment*>(
					payload + segment_offset);
				auto supported_pointer_format = [](std::uint16_t format) {
					return format == 1 || format == 2 || format == 6 || format == 7 ||
						format == 9 || format == 10 || format == 12;
				};
				const auto* segment_starts = starts_relative &&
					supported_pointer_format(starts_candidate->pointer_format) ?
					starts_candidate : payload_candidate;
				const auto starts_location = segment_starts == starts_candidate ?
					static_cast<std::uint64_t>(header->starts_offset) + segment_offset :
					static_cast<std::uint64_t>(segment_offset);
				const auto pointer_format = segment_starts->pointer_format;
				Require(pointer_format == 1 || pointer_format == 2 || pointer_format == 6 ||
					pointer_format == 7 || pointer_format == 9 || pointer_format == 10 ||
					pointer_format == 12,
					"unsupported Mach-O chained pointer format; expected a supported ARM64e/64 userland format");
				const auto starts_bytes = segment_starts->size;
				Require(starts_bytes >= sizeof(ChainedStartsSegment) +
					static_cast<std::uint64_t>(segment_starts->page_count) * sizeof(std::uint16_t) &&
					starts_location <= chained_command->data_size &&
					starts_bytes <= chained_command->data_size - starts_location,
					"Mach-O chained-fixups page starts are invalid");
				Require(segment_starts->page_size == 0x1000 ||
					segment_starts->page_size == 0x4000,
					"Mach-O chained-fixups page size is unsupported");
				const auto* page_starts = reinterpret_cast<const std::uint16_t*>(
					payload + starts_location + sizeof(ChainedStartsSegment));
				Require(segment_index < segments.size(), "Mach-O chained-fixups segment index is invalid");
				const auto segment_page_count = segments[segment_index].vm_size /
					segment_starts->page_size +
					(segments[segment_index].vm_size % segment_starts->page_size != 0);
				Require(segment_starts->page_count <= segment_page_count,
					"Mach-O chained-fixups page count exceeds its segment");
				const auto chain_starts_count = (starts_bytes -
					sizeof(ChainedStartsSegment) -
					static_cast<std::uint64_t>(segment_starts->page_count) *
					sizeof(std::uint16_t)) / sizeof(std::uint16_t);
				const auto* chain_starts = page_starts + segment_starts->page_count;
				const bool arm64e_format = pointer_format == 1 || pointer_format == 7 ||
					pointer_format == 9 || pointer_format == 10 || pointer_format == 12;
				const std::uint64_t chain_stride = arm64e_format &&
					pointer_format != 7 && pointer_format != 10 ? 8u : 4u;
				const auto walk_chain = [&](std::uint16_t start, std::uint16_t page) {
					Require(start < segment_starts->page_size,
						"Mach-O chained-fixup start exceeds its page");
					const auto page_vm = segments[segment_index].vm_address +
						static_cast<std::uint64_t>(page) * segment_starts->page_size;
					auto vm = page_vm + start;
					const auto max_nodes = segment_starts->page_size / chain_stride + 1;
					for (std::uint64_t chain = 0; chain < max_nodes; ++chain) {
						const auto page_offset = vm - page_vm;
						Require(page_offset <= segment_starts->page_size &&
							segment_starts->page_size - page_offset >= sizeof(std::uint64_t),
							"Mach-O chained-fixup pointer crosses its page");
						const auto* segment = &segments[segment_index];
						Require(vm >= segment->vm_address &&
							vm - segment->vm_address < segment->file_size,
							"Mach-O chained-fixup location is not file-backed");
						const auto file_offset = segment->file_offset +
							(vm - segment->vm_address);
						Require(RangeFits(file_bytes, file_offset, sizeof(std::uint64_t)),
							"Mach-O chained-fixup pointer is outside the file");
						std::uint64_t raw = 0;
						std::memcpy(&raw, view + file_offset, sizeof(raw));
						const bool authenticated = arm64e_format && (raw >> 63) != 0;
						const bool bind = arm64e_format ?
							((raw >> 62) & 1) != 0 : (raw >> 63) != 0;
						const auto next = static_cast<std::uint16_t>(arm64e_format ?
							((raw >> 51) & 0x7ff) : ((raw >> 51) & 0xfff));
						std::uint32_t bind_ordinal = 0;
						std::int64_t addend = 0;
						std::uint64_t target = 0;
						if (arm64e_format) {
							bind_ordinal = static_cast<std::uint32_t>(raw &
								(pointer_format == 12 ? 0xffffff : 0xffff));
							if (authenticated) {
								// In authenticated layouts, bits 32..50 contain PAC
								// diversity/key metadata, not a bind addend.
								addend = 0;
								if (!bind) target = raw & 0xffffffffu;
							} else if (bind) {
								const auto encoded_addend = (raw >> 32) & 0x7ffff;
								addend = static_cast<std::int64_t>(encoded_addend);
								if ((encoded_addend & 0x40000) != 0)
									addend -= 0x80000;
							} else {
								target = (raw & ((1ull << 43) - 1)) |
									(((raw >> 43) & 0xff) << 56);
							}
						} else {
							bind_ordinal = static_cast<std::uint32_t>(raw & 0xffffff);
							addend = static_cast<std::int64_t>((raw >> 24) & 0xff);
							target = (raw & 0xfffffffffull) |
								(((raw >> 36) & 0xff) << 56);
						}
						chained_fixups.push_back({segment_index, vm, bind,
							bind_ordinal, addend, target, next, pointer_format,
							authenticated});
						if (next == 0) return;
						const auto advance = static_cast<std::uint64_t>(next) * chain_stride;
						Require(advance <= segment_starts->page_size - page_offset,
							"Mach-O chained-fixup link exceeds its page");
						vm += advance;
					}
					throw std::runtime_error("Mach-O chained-fixup chain does not terminate");
				};
				for (std::uint16_t page = 0; page < segment_starts->page_count; ++page) {
					const auto page_start = page_starts[page];
					if (page_start == 0xffff) {
						continue;
					}
					if ((page_start & 0x8000u) == 0) {
						walk_chain(page_start, page);
						continue;
					}
					std::uint64_t chain_start_index = page_start & 0x7fffu;
					bool last_start = false;
					while (chain_start_index < chain_starts_count) {
						const auto chain_start = chain_starts[chain_start_index];
						walk_chain(static_cast<std::uint16_t>(chain_start & 0x7fffu), page);
						last_start = (chain_start & 0x8000u) != 0;
						if (last_start) break;
						++chain_start_index;
					}
					Require(last_start,
						"Mach-O chained-fixup multi-start list has no terminator");
				}
			}
		}

		return MachOImage(file, mapping, mapped_view, file_bytes, image_offset,
			normalized_header, is_32_bit,
			std::move(segments), std::move(dependencies), std::move(sections),
			std::move(weak_dependencies),
			std::move(rpaths), std::move(symbols),
		std::move(relocations), std::move(indirect_symbols), std::move(bind_actions),
			std::move(threaded_bind_targets),
		std::move(rebase_actions),
			std::move(chained_fixups), std::move(chained_imports),
			std::move(reexports), entry_offset, uuid, std::move(install_name),
			std::move(dylinker_path));
	} catch (...) {
		UnmapViewOfFile(mapped_view);
		CloseHandle(mapping);
		CloseHandle(file);
		throw;
	}
}

std::vector<MachOSymbol> MachOImage::UndefinedSymbols() const
{
	std::vector<MachOSymbol> result;
	for (const auto& symbol : m_symbols) {
		if ((symbol.type & N_TYPE) == N_UNDF && !symbol.name.empty()) {
			result.push_back(symbol);
		}
	}
	return result;
}

bool MachOImage::IsWeakUndefined(const MachOSymbol& symbol) noexcept
{
	// N_WEAK_REF in n_desc. The caller already restricts this query to an
	// N_UNDF symbol; keeping the bit test here makes the Darwin rule explicit.
	return (symbol.description & 0x0040u) != 0;
}

bool MachOImage::IsFlatLookupUndefined(const MachOSymbol& symbol) noexcept
{
	// N_REF_TO_WEAK requests flat namespace lookup; it is distinct from
	// N_WEAK_REF, which only makes a missing symbol resolve to NULL.
	return (symbol.description & 0x0080u) != 0;
}

std::vector<MachOImage::IndirectImportSlot> MachOImage::IndirectImportSlots() const
{
	std::vector<IndirectImportSlot> slots;
	const auto pointer_size = Is32Bit() ? sizeof(std::uint32_t) : sizeof(std::uint64_t);
	std::size_t indirect_index = 0;
	for (const auto& section : m_sections) {
		const auto section_type = section.flags & 0xffu;
		if (section_type == 0x8u) {
			if (section.reserved2 == 0 || section.size % section.reserved2 != 0)
				throw std::runtime_error("Mach-O symbol-stub section is misaligned");
			indirect_index += static_cast<std::size_t>(section.size / section.reserved2);
			continue;
		}
		if (section_type != 0x6u && section_type != 0x7u &&
			section.section_name != "__la_symbol_ptr" &&
			section.section_name != "__nl_symbol_ptr")
			continue;
		if (section.size % pointer_size != 0)
			throw std::runtime_error("Mach-O indirect symbol section is misaligned");
		const bool lazy = section_type == 0x7u ||
			section.section_name == "__la_symbol_ptr";
		for (std::uint64_t offset = 0; offset < section.size; offset += pointer_size)
			slots.push_back({section.address + offset,
				static_cast<std::uint32_t>(indirect_index++), lazy});
	}
	return slots;
}

void MachOImage::ApplyIndirectImportBindings(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings, bool bind_lazy) const
{
	// Modern chained-fixups images carry their import locations in the chained
	// starts table.  Some toolchains retain a legacy indirect-symbol table for
	// stub metadata as well; it is not a second set of pointer slots and must
	// not be forced through the legacy __la/__nl slot walk.
	if (!m_chained_fixups.empty())
		return;
	const auto slots = IndirectImportSlots();
	if (slots.empty())
		return;
	if (m_indirect_symbols.size() > m_symbols.size() + 0x1000000u)
		throw std::runtime_error("Mach-O indirect symbol table is unreasonable");
	for (const auto& slot : slots) {
		if (slot.symbol_index >= m_indirect_symbols.size())
			throw std::out_of_range("Mach-O indirect symbol table exceeds import slots");
		if (slot.lazy && !bind_lazy)
			continue;
		const auto symbol_index = m_indirect_symbols[slot.symbol_index];
		if ((symbol_index & 0xC0000000u) != 0)
			continue; // LOCAL and ABSOLUTE entries are not imported bindings.
		if (symbol_index >= m_symbols.size())
			throw std::out_of_range("Mach-O indirect symbol index is invalid");
		const auto& symbol = m_symbols[symbol_index];
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&symbol](const auto& binding) {
				return binding.symbol == symbol.name &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						MachOLibraryOrdinalFromSymbolDescription(
							symbol.description));
			});
		if (match == bindings.end()) {
			if (IsWeakUndefined(symbol)) {
				if (Is32Bit()) ApplyAbsolute32(mapping, slot.address, 0);
				else ApplyAbsolute64(mapping, slot.address, 0);
				continue;
			}
			throw std::runtime_error("unresolved Mach-O indirect import: " + symbol.name);
		}
		if (Is32Bit()) ApplyAbsolute32(mapping, slot.address, match->address);
		else ApplyAbsolute64(mapping, slot.address, match->address);
	}
}

bool MachOImage::CodeSignatureRange(std::uint32_t& offset,
	std::uint32_t& size) const noexcept
{
	offset = 0;
	size = 0;
	if (m_view == nullptr || m_header == nullptr) return false;
	const auto command_offset = m_image_offset + (m_is_32_bit ?
		sizeof(MachHeader32) : sizeof(MachHeader64));
	if (command_offset > m_file_bytes ||
		m_header->command_bytes > m_file_bytes - command_offset)
		return false;
	const auto* cursor = m_view + command_offset;
	const auto* end = cursor + m_header->command_bytes;
	for (std::uint32_t index = 0; index < m_header->command_count; ++index) {
		if (cursor + sizeof(LoadCommand) > end) return false;
		const auto* command = reinterpret_cast<const LoadCommand*>(cursor);
		if (command->command_bytes < sizeof(LoadCommand) ||
			command->command_bytes > static_cast<std::size_t>(end - cursor))
			return false;
		if (command->command == LC_CODE_SIGNATURE) {
			if (command->command_bytes < sizeof(LinkeditDataCommand)) return false;
			const auto* signature = reinterpret_cast<const LinkeditDataCommand*>(command);
			offset = signature->data_offset;
			size = signature->data_size;
			return true;
		}
		cursor += command->command_bytes;
	}
	return false;
}

bool MachOImage::HasCodeSignature() const noexcept
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
	return CodeSignatureRange(offset, size);
}

bool MachOImage::HasValidCodeSignatureSuperBlob() const noexcept
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
	if (!CodeSignatureRange(offset, size) || size < 12 ||
		offset > m_file_bytes || size > m_file_bytes - offset)
		return false;
	const auto* blob = m_view + offset;
	const auto read_be32 = [](const std::uint8_t* value) {
		return (static_cast<std::uint32_t>(value[0]) << 24) |
			(static_cast<std::uint32_t>(value[1]) << 16) |
			(static_cast<std::uint32_t>(value[2]) << 8) | value[3];
	};
	if (read_be32(blob) != 0xfade0cc0u) return false;
	const auto length = read_be32(blob + 4);
	const auto count = read_be32(blob + 8);
	if (length < 12 || length > size || count > (length - 12) / 8) return false;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto blob_offset = read_be32(blob + 12 + index * 8 + 4);
		if (blob_offset > length || length - blob_offset < 8) return false;
		const auto blob_length = read_be32(blob + blob_offset + 4);
		if (blob_length < 8 || blob_length > length - blob_offset) return false;
		if (read_be32(blob + 12 + index * 8) == 0 &&
			(blob_length < 44 || read_be32(blob + blob_offset) != 0xfade0c02u))
			return false;
	}
	return true;
}

bool MachOImage::IsWeakDependency(const std::string& name) const noexcept
{
	return std::find(m_weak_dependencies.begin(), m_weak_dependencies.end(), name)
		!= m_weak_dependencies.end();
}

std::uintptr_t MachOImage::SymbolAddress(
	const Mapping& mapping, const std::string& name) const
{
	std::string resolved_name = name;
	std::set<std::string> visited_indirect_symbols;
	for (;;) {
		if (!visited_indirect_symbols.insert(resolved_name).second)
			throw std::runtime_error("Mach-O indirect symbol cycle: " + name);
		bool followed_indirect = false;
		for (const auto& symbol : m_symbols) {
			if (symbol.name != resolved_name ||
				(symbol.type & N_TYPE) == N_UNDF)
				continue;
			if ((symbol.type & N_TYPE) == N_INDR) {
				if (symbol.indirect_target.empty())
					throw std::runtime_error("Mach-O indirect symbol has no target: " +
						resolved_name);
				resolved_name = symbol.indirect_target;
				followed_indirect = true;
				break;
			}
			if ((symbol.type & N_TYPE) == N_ABS) {
				if (symbol.value > std::numeric_limits<std::uintptr_t>::max())
					throw std::overflow_error("Mach-O absolute symbol exceeds host address width");
				return static_cast<std::uintptr_t>(symbol.value);
			}
			if (symbol.value > static_cast<std::uint64_t>(
				std::numeric_limits<std::intptr_t>::max())) {
				throw std::overflow_error("Mach-O symbol value cannot be slid safely");
			}
			const auto address = static_cast<std::intptr_t>(symbol.value) + mapping.Slide();
			if (address <= 0) {
				throw std::runtime_error("Mach-O symbol address is invalid");
			}
			return static_cast<std::uintptr_t>(address);
		}
		if (!followed_indirect)
			break;
	}
	throw std::runtime_error("Mach-O defined symbol not found: " + resolved_name);
}

std::uintptr_t MachOImage::SymbolResolverAddress(
	const Mapping& mapping, const std::string& name) const
{
	const auto symbol = std::find_if(m_symbols.begin(), m_symbols.end(),
		[&name](const MachOSymbol& candidate) {
			return candidate.name == name && candidate.has_resolver;
		});
	if (symbol == m_symbols.end())
		throw std::runtime_error("Mach-O export resolver not found: " + name);
	const auto segment = std::find_if(m_segments.begin(), m_segments.end(),
		[&symbol](const MachOSegment& candidate) {
			return symbol->resolver_value >= candidate.vm_address &&
				symbol->resolver_value - candidate.vm_address < candidate.vm_size &&
				(candidate.initial_protection & 4) != 0;
		});
	if (segment == m_segments.end())
		throw std::runtime_error(
			"Mach-O export resolver is outside executable image segments");
	if (symbol->resolver_value > std::numeric_limits<std::uintptr_t>::max())
		throw std::overflow_error("Mach-O export resolver exceeds host address width");
	const auto preferred = static_cast<std::uintptr_t>(symbol->resolver_value);
	const auto slide = mapping.Slide();
	if (slide >= 0) {
		const auto amount = static_cast<std::uintptr_t>(slide);
		if (preferred > std::numeric_limits<std::uintptr_t>::max() - amount)
			throw std::overflow_error("Mach-O export resolver cannot be slid safely");
		const auto address = preferred + amount;
		if (address == 0)
			throw std::runtime_error("Mach-O export resolver address is null");
		return address;
	}
	const auto amount = static_cast<std::uintptr_t>(-(slide + 1)) + 1;
	if (preferred < amount)
		throw std::runtime_error("Mach-O export resolver address is invalid after sliding");
	const auto address = preferred - amount;
	if (address == 0)
		throw std::runtime_error("Mach-O export resolver address is null");
	return address;
}

std::uintptr_t MachOImage::SectionAddress(const Mapping& mapping,
	const std::string& segment_name, const std::string& section_name) const
{
	for (const auto& section : m_sections) {
		if (section.segment_name != segment_name ||
			section.section_name != section_name)
			continue;
		if (section.address > static_cast<std::uint64_t>(
			std::numeric_limits<std::intptr_t>::max()))
			throw std::overflow_error("Mach-O section address cannot be slid safely");
		const auto address = static_cast<std::intptr_t>(section.address) + mapping.Slide();
		if (address <= 0)
			throw std::runtime_error("Mach-O section address is invalid");
		return static_cast<std::uintptr_t>(address);
	}
	throw std::runtime_error("Mach-O section not found: " + segment_name + "," +
		section_name);
}

void MachOImage::ApplyAbsolute64(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(target)) {
			continue;
		}
		const auto address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(target),
			PAGE_READWRITE, &old_protection)) {
			ThrowLastError("VirtualProtect(Mach-O relocation)");
		}
		std::memcpy(reinterpret_cast<void*>(address), &target, sizeof(target));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(target),
			ProtectionFor(segment.initial_protection), &restored_protection)) {
			ThrowLastError("VirtualProtect(Mach-O relocation restore)");
		}
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(target));
		return;
	}
	throw std::out_of_range("Mach-O absolute relocation is outside a segment");
}

void MachOImage::ApplyAbsolute32(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	if (target > std::numeric_limits<std::uint32_t>::max())
		throw std::overflow_error("Mach-O 32-bit absolute bind target is out of range");
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::uint32_t))
			continue;
		const auto address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::uint32_t),
			PAGE_READWRITE, &old_protection))
			ThrowLastError("VirtualProtect(Mach-O 32-bit bind)");
		const auto value = static_cast<std::uint32_t>(target);
		std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::uint32_t),
			ProtectionFor(segment.initial_protection), &restored_protection))
			ThrowLastError("VirtualProtect(Mach-O 32-bit bind restore)");
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(value));
		return;
	}
	throw std::out_of_range("Mach-O 32-bit bind is outside a segment");
}

void MachOImage::ApplyRelocations(const Mapping& mapping,
	const std::vector<std::uintptr_t>& symbol_addresses,
	bool defer_unreachable_external) const
{
	if (mapping.m_relocation_original_widths.empty()) {
		mapping.m_relocation_original_values.resize(m_relocations.size());
		mapping.m_relocation_original_sites.resize(m_relocations.size());
		mapping.m_relocation_original_widths.resize(m_relocations.size());
	} else if (mapping.m_relocation_original_widths.size() != m_relocations.size() ||
		mapping.m_relocation_original_values.size() != m_relocations.size() ||
		mapping.m_relocation_original_sites.size() != m_relocations.size()) {
		throw std::logic_error("Mach-O relocation snapshot does not match image");
	}
	const auto original_relocation_value = [&mapping](std::size_t index,
		std::uintptr_t site, std::size_t width) {
		if (index >= mapping.m_relocation_original_widths.size() || width == 0 ||
			width > sizeof(std::uint64_t))
			throw std::logic_error("invalid Mach-O relocation snapshot request");
		auto& saved_width = mapping.m_relocation_original_widths[index];
		if (saved_width == 0) {
			std::uint64_t value = 0;
			std::memcpy(&value, reinterpret_cast<const void*>(site), width);
			mapping.m_relocation_original_values[index] = value;
			mapping.m_relocation_original_sites[index] = site;
			saved_width = static_cast<std::uint8_t>(width);
		} else if (saved_width != width || mapping.m_relocation_original_sites[index] != site) {
			throw std::logic_error("Mach-O relocation site changed after snapshot");
		}
		return mapping.m_relocation_original_values[index];
	};
	if (Is32Bit()) {
		for (std::size_t index = 0; index < m_relocations.size(); ++index) {
			const auto& relocation = m_relocations[index];
			if (!relocation.scattered && relocation.external &&
				relocation.type == 5 && relocation.pc_relative &&
				relocation.length == 2) {
				if (relocation.address < 0 ||
					relocation.symbol_index >= symbol_addresses.size()) {
					throw std::runtime_error(
						"invalid 32-bit i386 TLV relocation record");
				}
				const auto descriptor_address = symbol_addresses[relocation.symbol_index];
				const auto pointer_address = TLVPointerAddress(mapping, descriptor_address);
				if (pointer_address == 0)
					throw std::runtime_error(
						"32-bit i386 TLV relocation has no __thread_ptrs slot");
				ApplyRelative32(mapping,
					static_cast<std::uint32_t>(relocation.address), pointer_address);
				continue;
			}
			if (relocation.type == 3 && relocation.scattered &&
				relocation.length == 2 && !relocation.pc_relative) {
				const auto target = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(relocation.scattered_value) + mapping.Slide());
				ApplyAbsolute32(mapping, static_cast<std::uint32_t>(relocation.address), target);
				continue;
			}
			if ((relocation.type == 2 || relocation.type == 4) &&
				relocation.scattered) {
				if (index + 1 >= m_relocations.size())
					throw std::runtime_error("32-bit i386 SECTDIFF relocation has no pair");
				const auto& pair = m_relocations[index + 1];
				if (!pair.scattered || pair.type != 1 || pair.length != relocation.length ||
					pair.pc_relative || relocation.pc_relative || relocation.length != 2) {
					throw std::runtime_error("invalid 32-bit i386 SECTDIFF relocation pair");
				}
				const auto vm_address = static_cast<std::uint32_t>(relocation.address);
				const auto site = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto addend_bits = static_cast<std::uint32_t>(
					original_relocation_value(index, site, sizeof(std::uint32_t)));
				const auto value = static_cast<std::uintptr_t>(
					static_cast<std::int64_t>(relocation.scattered_value) -
					static_cast<std::int64_t>(pair.scattered_value) +
					static_cast<std::int32_t>(addend_bits));
				ApplyAbsolute32(mapping, vm_address, value);
				++index;
				continue;
			}
			if (relocation.type == 1 && relocation.scattered)
				throw std::runtime_error("stray 32-bit i386 SECTDIFF pair relocation");
			if (relocation.scattered || relocation.type != 0 || relocation.length != 2 ||
				relocation.address < 0) {
				throw std::runtime_error(
					"unsupported 32-bit i386 Mach-O relocation record");
			}
			if (relocation.external && relocation.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid 32-bit i386 relocation symbol index");
			}
			const auto vm_address = static_cast<std::uint32_t>(relocation.address);
			const auto site = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(vm_address) + mapping.Slide());
			const auto addend_bits = static_cast<std::uint32_t>(
				original_relocation_value(index, site, sizeof(std::uint32_t)));
			const auto addend = static_cast<std::int32_t>(addend_bits);
			std::uintptr_t target = 0;
			if (relocation.external) {
				target = symbol_addresses[relocation.symbol_index];
			} else if (relocation.symbol_index < m_symbols.size()) {
				target = SymbolAddress(mapping, m_symbols[relocation.symbol_index].name);
			} else {
				throw std::runtime_error("invalid 32-bit i386 local relocation symbol");
			}
			const auto adjusted = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(target) + addend);
			if (relocation.pc_relative)
				ApplyRelative32(mapping, vm_address, adjusted);
			else
				ApplyAbsolute32(mapping, vm_address, adjusted);
		}
		return;
	}
	if (IsArm64()) {
		const auto resolve_vm_address = [&](std::uint64_t value) {
			for (const auto& segment : m_segments) {
				if (value >= segment.vm_address &&
					value - segment.vm_address <= segment.vm_size) {
					return value;
				}
			}
			for (const auto& segment : m_segments) {
				if (value <= segment.vm_size &&
					segment.vm_address <= std::numeric_limits<std::uint64_t>::max() - value) {
					return segment.vm_address + value;
				}
			}
			throw std::out_of_range("ARM64 Mach-O relocation address is outside all segments");
		};
		const auto find_address = [&](std::uint64_t vm_address, std::size_t bytes) {
			vm_address = resolve_vm_address(vm_address);
			for (const auto& segment : m_segments) {
				if (vm_address < segment.vm_address ||
					vm_address - segment.vm_address > segment.vm_size ||
					segment.vm_size - (vm_address - segment.vm_address) < bytes) {
					continue;
				}
				return std::pair<std::uintptr_t, const MachOSegment*>(
					static_cast<std::uintptr_t>(static_cast<std::intptr_t>(vm_address) + mapping.Slide()),
					&segment);
			}
			throw std::out_of_range("ARM64 Mach-O relocation is outside a segment");
		};
		const auto read32 = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto [address, segment] = find_address(vm_address, sizeof(std::uint32_t));
			(void)segment;
			return static_cast<std::uint32_t>(original_relocation_value(
				relocation_index, address, sizeof(std::uint32_t)));
		};
		const auto read64 = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto [address, segment] = find_address(vm_address, sizeof(std::uint64_t));
			(void)segment;
			return original_relocation_value(relocation_index, address,
				sizeof(std::uint64_t));
		};
		const auto reject_authenticated_pointer = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto encoded = read64(relocation_index, vm_address);
			if ((encoded >> 63) == 0 || ((encoded >> 62) & 1) != 0 ||
				((encoded >> 51) & 0x7ff) != 0) {
				throw std::runtime_error("invalid ARM64 authenticated-pointer metadata");
			}
			const auto key = (encoded >> 49) & 0x3;
			const auto address_diversity = (encoded >> 48) & 1;
			const auto discriminator = (encoded >> 32) & 0xffff;
			const auto addend = encoded & 0xffffffffu;
			throw std::runtime_error(
				"ARM64 authenticated pointer requires PAC backend (key=" +
				std::to_string(key) + ", address_diversity=" +
				std::to_string(address_diversity) + ", discriminator=" +
				std::to_string(discriminator) + ", addend=" +
				std::to_string(addend) + ")");
		};
		const auto write32 = [&](std::uint64_t vm_address, std::uint32_t value) {
			const auto [address, segment] = find_address(vm_address, sizeof(value));
			DWORD old_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value),
				PAGE_READWRITE, &old_protection)) {
				ThrowLastError("VirtualProtect(ARM64 Mach-O relocation)");
			}
			std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
			DWORD restored_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value),
				ProtectionFor(segment->initial_protection), &restored_protection)) {
				ThrowLastError("VirtualProtect(ARM64 Mach-O relocation restore)");
			}
			FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), sizeof(value));
		};
		const auto page = [](std::uint64_t address) { return address & ~std::uint64_t(0xfff); };
		for (std::size_t index = 0; index < m_relocations.size(); ++index) {
			const auto& relocation = m_relocations[index];
			if (!relocation.external) {
				if (relocation.type == 11) {
					if (relocation.pc_relative || relocation.length != 3 || relocation.address < 0) {
						throw std::runtime_error("invalid ARM64 authenticated-pointer relocation");
					}
					reject_authenticated_pointer(index, resolve_vm_address(
						static_cast<std::uint32_t>(relocation.address)));
				}
				if (relocation.type != 0 || relocation.pc_relative || relocation.length != 3 ||
					relocation.address < 0) {
					throw std::runtime_error("unsupported ARM64 local Mach-O relocation record");
				}
				const auto vm_address = resolve_vm_address(
					static_cast<std::uint32_t>(relocation.address));
				const auto rebased = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(read64(index, vm_address)) + mapping.Slide() +
					(relocation.has_explicit_addend ? relocation.explicit_addend : 0));
				ApplyAbsolute64(mapping, vm_address, rebased);
				continue;
			}
			if (relocation.address < 0 || relocation.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid ARM64 Mach-O relocation record");
			}
			const auto vm_address = resolve_vm_address(
				static_cast<std::uint32_t>(relocation.address));
			if (relocation.type == 11) {
				if (relocation.pc_relative || relocation.length != 3) {
					throw std::runtime_error("invalid ARM64 authenticated-pointer relocation");
				}
				reject_authenticated_pointer(index, vm_address);
			}
			const auto target = symbol_addresses[relocation.symbol_index];
			const auto target_with_addend = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(target) +
				(relocation.has_explicit_addend ? relocation.explicit_addend : 0));
			if (relocation.type == 1 && !relocation.pc_relative && relocation.length == 3) {
				if (index + 1 >= m_relocations.size()) {
					throw std::runtime_error("ARM64 SUBTRACTOR relocation has no pair");
				}
				const auto& pair = m_relocations[index + 1];
				if (!pair.external || pair.type != 0 || pair.pc_relative || pair.length != 3 ||
					pair.address != relocation.address ||
					pair.symbol_index >= symbol_addresses.size()) {
					throw std::runtime_error("invalid ARM64 SUBTRACTOR relocation pair");
				}
				const auto value = target_with_addend - symbol_addresses[pair.symbol_index];
				ApplyAbsolute64(mapping, vm_address, value);
				++index;
			} else if (relocation.type == 0 && !relocation.pc_relative && relocation.length == 3) {
				ApplyAbsolute64(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 7 && !relocation.pc_relative && relocation.length == 3) {
				ApplyAbsolute64(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 7 && relocation.pc_relative && relocation.length == 2) {
				ApplyRelative32(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 2 && relocation.pc_relative && relocation.length == 2) {
				const auto place = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto delta = static_cast<std::int64_t>(target_with_addend) -
					static_cast<std::int64_t>(place);
				if ((delta & 3) != 0 || delta < -(std::int64_t(1) << 27) ||
					delta >= (std::int64_t(1) << 27)) {
					throw std::overflow_error("ARM64 BRANCH26 relocation is out of range");
				}
				const auto instruction = read32(index, vm_address);
				const auto immediate = static_cast<std::uint32_t>((delta >> 2) & 0x03ffffff);
				write32(vm_address, (instruction & 0xfc000000u) | immediate);
			} else if ((relocation.type == 3 || relocation.type == 5 || relocation.type == 8) &&
				relocation.pc_relative && relocation.length == 2) {
				const auto place = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto page_delta = static_cast<std::int64_t>(page(target_with_addend)) -
					static_cast<std::int64_t>(page(place));
				const auto page_delta_units = page_delta >> 12;
				if (page_delta_units < -(std::int64_t(1) << 20) ||
					page_delta_units >= (std::int64_t(1) << 20)) {
					throw std::overflow_error("ARM64 PAGE21 relocation is out of range");
				}
				const auto instruction = read32(index, vm_address);
				const auto encoded = static_cast<std::uint32_t>(page_delta_units);
				const auto immlo = (encoded & 3u) << 29;
				const auto immhi = ((encoded >> 2) & 0x7ffffu) << 5;
				write32(vm_address, (instruction & 0x9f00001fu) | immlo | immhi);
			} else if ((relocation.type == 4 || relocation.type == 6 || relocation.type == 9) &&
				!relocation.pc_relative && relocation.length == 2) {
				const auto instruction = read32(index, vm_address);
				const auto offset = static_cast<std::uint32_t>(target_with_addend & 0xfff);
				std::uint32_t encoded = offset;
				if ((instruction & 0x1f000000u) == 0x11000000u) {
					if ((instruction & (1u << 22)) != 0 && offset != 0) {
						throw std::runtime_error("ARM64 PAGEOFF12 ADD instruction has an unsupported shift");
					}
				} else {
					const auto scale = 1u << ((instruction >> 30) & 3u);
					if ((offset % scale) != 0) {
						throw std::runtime_error("ARM64 PAGEOFF12 load/store offset is unaligned");
					}
					encoded = offset / scale;
				}
				if (encoded > 0xfff) {
					throw std::overflow_error("ARM64 PAGEOFF12 relocation is out of range");
				}
				write32(vm_address, (instruction & 0xffc003ffu) | (encoded << 10));
			} else {
				throw std::runtime_error("unsupported ARM64 Mach-O relocation record");
			}
		}
		return;
	}
	RequireNativeExecution(*this);
	const auto relocation_addend = [&](std::size_t index, std::uint64_t vm_address,
		std::size_t width) {
		const auto site = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		const auto raw = original_relocation_value(index, site, width);
		if (width == sizeof(std::int32_t)) {
			std::int32_t value = 0;
			std::memcpy(&value, &raw, sizeof(value));
			return static_cast<std::int64_t>(value);
		}
		if (width == sizeof(std::int64_t)) {
			std::int64_t value = 0;
			std::memcpy(&value, &raw, sizeof(value));
			return value;
		}
		throw std::logic_error("unsupported x86-64 Mach-O relocation addend width");
	};
	const auto got_slot_address = [&](std::uint32_t symbol_index) {
		std::uintptr_t lazy_slot = 0;
		for (const auto& slot : IndirectImportSlots()) {
			if (slot.symbol_index >= m_indirect_symbols.size())
				throw std::out_of_range("Mach-O indirect symbol table exceeds GOT slots");
			const auto indirect_symbol = m_indirect_symbols[slot.symbol_index];
			if ((indirect_symbol & 0xC0000000u) == 0 &&
				indirect_symbol == symbol_index) {
				const auto address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(slot.address) + mapping.Slide());
				if (!slot.lazy)
					return address;
				if (lazy_slot == 0)
					lazy_slot = address;
			}
		}
		if (lazy_slot != 0)
			return lazy_slot;
		throw std::runtime_error("Mach-O GOT relocation has no indirect pointer slot");
	};
	for (std::size_t index = 0; index < m_relocations.size(); ++index) {
		const auto& relocation = m_relocations[index];
		if (!relocation.external) {
			continue;
		}
		if (relocation.address < 0 ||
			relocation.symbol_index >= symbol_addresses.size()) {
			throw std::runtime_error("unsupported x86-64 Mach-O relocation record");
		}
		if (relocation.type == 5 && !relocation.pc_relative && relocation.length == 3) {
			if (index + 1 >= m_relocations.size()) {
				throw std::runtime_error("Mach-O SUBTRACTOR relocation has no pair");
			}
			const auto& pair = m_relocations[index + 1];
			if (!pair.external || pair.type != 0 || pair.pc_relative || pair.length != 3 ||
				pair.address != relocation.address ||
				pair.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid Mach-O SUBTRACTOR relocation pair");
			}
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int64_t));
			const auto value = AddSignedAddress(
				symbol_addresses[relocation.symbol_index] -
				symbol_addresses[pair.symbol_index], addend);
			ApplyAbsolute64(mapping, static_cast<std::uint32_t>(relocation.address), value);
			++index;
		} else if (!relocation.pc_relative && relocation.length == 3 &&
			(relocation.type == 0 || relocation.type == 3 || relocation.type == 4)) {
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int64_t));
			ApplyAbsolute64(mapping, static_cast<std::uint32_t>(relocation.address),
				AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 1 || relocation.type == 2)) {
			if (IsOptimizedTLVLEA(mapping,
				static_cast<std::uint32_t>(relocation.address),
				symbol_addresses[relocation.symbol_index]))
				throw std::runtime_error(
					"optimized Mach-O TLV LEA requires a per-thread direct-address bridge");
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			try {
				ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
			} catch (const std::overflow_error&) {
				if (relocation.type == 2) {
					ApplyBranchThunk(mapping,
						static_cast<std::uint32_t>(relocation.address),
						AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
				} else if (!defer_unreachable_external) {
					throw;
				}
			}
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 3 || relocation.type == 4)) {
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			const auto slot = got_slot_address(relocation.symbol_index);
			try {
				ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(slot, addend));
			} catch (const std::overflow_error&) {
				if (!defer_unreachable_external)
					throw;
			}
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 6 || relocation.type == 7 || relocation.type == 8)) {
			const auto addend = relocation.type == 6 ? 1 :
				relocation.type == 7 ? 2 : 4;
			const auto encoded_addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			try {
				ApplyRelative32WithAddend(mapping,
					static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(symbol_addresses[relocation.symbol_index],
						encoded_addend), addend);
			} catch (const std::overflow_error&) {
				if (!defer_unreachable_external)
					throw;
			}
		} else if (relocation.type == 9) {
			const auto local_pointer_address = TLVPointerAddress(mapping,
				symbol_addresses[relocation.symbol_index]);
			const auto pointer_address = local_pointer_address == 0 ?
				symbol_addresses[relocation.symbol_index] : local_pointer_address;
			if (pointer_address == 0 || !relocation.pc_relative || relocation.length != 2)
				throw std::runtime_error(
					"Mach-O x86-64 TLV relocation has no local __thread_ptrs slot");
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
				AddSignedAddress(pointer_address, addend));
		} else {
			throw std::runtime_error("unsupported x86-64 Mach-O relocation record");
		}
	}
}

std::uintptr_t MachOImage::TLVPointerAddress(const Mapping& mapping,
	std::uintptr_t descriptor_address) const
{
	const MachOSection* variables = nullptr;
	const MachOSection* pointers = nullptr;
	for (const auto& section : m_sections) {
		if (section.section_name == "__thread_vars")
			variables = &section;
		else if (section.section_name == "__thread_ptrs")
			pointers = &section;
	}
	if (variables == nullptr || pointers == nullptr ||
		variables->size == 0 || pointers->size < sizeof(std::uintptr_t))
		return 0;
	const auto variables_address = SectionAddress(mapping, variables->segment_name,
		variables->section_name);
	if (descriptor_address < variables_address ||
		descriptor_address - variables_address >= variables->size ||
		(descriptor_address - variables_address) % sizeof(DarwinTLVDescriptor) != 0)
		return 0;
	const auto index = (descriptor_address - variables_address) /
		sizeof(DarwinTLVDescriptor);
	if (index > std::numeric_limits<std::size_t>::max() / sizeof(std::uintptr_t) ||
		index * sizeof(std::uintptr_t) >= pointers->size)
		return 0;
	const auto pointers_address = SectionAddress(mapping, pointers->segment_name,
		pointers->section_name);
	return pointers_address + index * sizeof(std::uintptr_t);
}

bool MachOImage::IsOptimizedTLVLEA(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	if (TLVPointerAddress(mapping, target) == 0)
		return false;
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address + 2 ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t))
			continue;
		const auto instruction = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address - 2) + mapping.Slide());
		return *reinterpret_cast<const std::uint8_t*>(instruction) == 0x8d;
	}
	return false;
}

void MachOImage::ApplyBindActions(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	RequireNativeExecution(*this);
	for (const auto& action : m_bind_actions) {
		if (action.lazy) {
			continue;
		}
		if (action.threaded) {
			if (action.type != 1 || action.segment_index >= m_segments.size() ||
				action.threaded_table_size != m_threaded_bind_targets.size()) {
				throw std::runtime_error("invalid Mach-O threaded bind action");
			}
			const auto& segment = m_segments[action.segment_index];
			if (action.segment_offset > segment.vm_size ||
				segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
				throw std::out_of_range("Mach-O threaded bind offset is outside its segment");
			}
			auto vm_address = segment.vm_address + action.segment_offset;
			// The ordinal table contains unique bind targets, not the number of
			// pointers in the chain. A binary may reuse one target across many
			// slots, so bound traversal by the segment's pointer capacity instead.
			const auto maximum_nodes = (segment.vm_size - action.segment_offset) /
				sizeof(std::uint64_t);
			for (std::uint64_t node = 0; node < maximum_nodes; ++node) {
				if (vm_address < segment.vm_address ||
					vm_address - segment.vm_address > segment.vm_size ||
					segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::uint64_t)) {
					throw std::out_of_range("Mach-O threaded bind chain leaves its segment");
				}
				const auto host_address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto raw = *reinterpret_cast<const std::uint64_t*>(host_address);
				const auto ordinal = static_cast<std::size_t>(raw & 0x00ffffffu);
				const auto next = static_cast<std::uint16_t>((raw >> 51) & 0x0fffu);
				if ((raw >> 63) == 0 || ordinal >= m_threaded_bind_targets.size()) {
					throw std::runtime_error("unsupported Mach-O threaded bind pointer");
				}
				const auto& target_action = m_threaded_bind_targets[ordinal];
				const auto match = std::find_if(bindings.begin(), bindings.end(),
					[&target_action](const auto& binding) {
						return binding.symbol == target_action.symbol &&
							(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
								target_action.dylib_ordinal);
					});
				if (match == bindings.end() && !target_action.weak) {
					throw std::runtime_error("unresolved Mach-O threaded bind action: " +
						target_action.symbol);
				}
				const auto pointer_addend = static_cast<std::int8_t>((raw >> 24) & 0xff);
				auto target = AddSignedAddress(match == bindings.end() ? 0 :
					match->address, target_action.addend);
				target = AddSignedAddress(target, pointer_addend);
				ApplyAbsolute64(mapping, vm_address, target);
				if (next == 0) {
					break;
				}
				const auto relative = vm_address - segment.vm_address;
				const auto next_bytes = static_cast<std::uint64_t>(next) * 8;
				if (next_bytes > segment.vm_size - relative ||
					vm_address > std::numeric_limits<std::uint64_t>::max() - next_bytes) {
					throw std::out_of_range(
						"Mach-O threaded bind chain leaves its segment");
				}
				vm_address += next_bytes;
				if (node + 1 == maximum_nodes) {
					throw std::runtime_error(
						"Mach-O threaded bind chain has no terminator");
				}
			}
			continue;
		}
		if ((action.type < 1 || action.type > 3) ||
			action.segment_index >= m_segments.size()) {
			throw std::runtime_error("unsupported Mach-O dyld bind action");
		}
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&action](const auto& binding) {
				return binding.symbol == action.symbol &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						action.dylib_ordinal);
			});
		if (match == bindings.end()) {
			if (!action.weak) {
				throw std::runtime_error("unresolved Mach-O dyld bind action: " + action.symbol);
			}
		}
		const auto& segment = m_segments[action.segment_index];
		if (action.segment_offset > segment.vm_size ||
			segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
			throw std::out_of_range("Mach-O dyld bind offset is outside its segment");
		}
		if (action.segment_offset > segment.file_size ||
			segment.file_size - action.segment_offset < sizeof(std::uintptr_t)) {
			throw std::out_of_range(
				"Mach-O dyld bind offset is outside its file-backed segment");
		}
		const auto target = AddSignedAddress(match == bindings.end() ? 0 :
			match->address, action.addend);
		const auto vm_address = segment.vm_address + action.segment_offset;
		if (action.type == 1)
			ApplyAbsolute64(mapping, vm_address, target);
		else if (action.type == 2)
			ApplyAbsolute32(mapping, vm_address, target);
		else
			ApplyRelative32(mapping, vm_address, target);
	}
}

void MachOImage::ApplyLazyBindAction(std::size_t action_index, const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	if (action_index >= m_bind_actions.size() || !m_bind_actions[action_index].lazy) {
		throw std::out_of_range("Mach-O lazy bind action index is invalid");
	}
	const auto& action = m_bind_actions[action_index];
	if (action.type < 1 || action.type > 3 || action.segment_index >= m_segments.size()) {
		throw std::runtime_error("unsupported Mach-O lazy bind action");
	}
	const auto same_binding_symbol = [](const std::string& left,
		const std::string& right) {
		return left == right || (left.size() > 1 && left.front() == '_' &&
			left.substr(1) == right);
	};
	const auto match = std::find_if(bindings.begin(), bindings.end(),
		[&action, &same_binding_symbol](const auto& binding) {
			return same_binding_symbol(action.symbol, binding.symbol) &&
				(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
					action.dylib_ordinal);
		});
	if (match == bindings.end()) {
		if (!action.weak) {
			throw std::runtime_error("unresolved Mach-O lazy bind action: " + action.symbol);
		}
	}
	const auto& segment = m_segments[action.segment_index];
	if (action.segment_offset > segment.vm_size ||
		segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
		throw std::out_of_range("Mach-O lazy bind offset is outside its segment");
	}
	if (action.segment_offset > segment.file_size ||
		segment.file_size - action.segment_offset < sizeof(std::uintptr_t)) {
		throw std::out_of_range(
			"Mach-O lazy bind offset is outside its file-backed segment");
	}
	const auto target = AddSignedAddress(match == bindings.end() ? 0 :
		match->address, action.addend);
	const auto vm_address = segment.vm_address + action.segment_offset;
	if (action.type == 1)
		ApplyAbsolute64(mapping, vm_address, target);
	else if (action.type == 2)
		ApplyAbsolute32(mapping, vm_address, target);
	else
		ApplyRelative32(mapping, vm_address, target);
}

void MachOImage::ApplyLazyBindActions(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	for (std::size_t index = 0; index < m_bind_actions.size(); ++index) {
		if (m_bind_actions[index].lazy)
			ApplyLazyBindAction(index, mapping, bindings);
	}
}

void MachOImage::ApplyChainedFixups(const Mapping& mapping,
	const std::vector<std::uintptr_t>& bind_addresses) const
{
	if (!IsArm64()) {
		RequireNativeExecution(*this);
	}
	for (const auto& fixup : m_chained_fixups) {
		if (fixup.authenticated) {
			throw std::runtime_error(
				"ARM64e chained authenticated pointer requires PAC backend");
		}
		if (fixup.segment_index >= m_segments.size()) {
			throw std::runtime_error("Mach-O chained fixup segment index is invalid");
		}
		std::uintptr_t value = 0;
		if (fixup.bind) {
			if (fixup.bind_ordinal >= bind_addresses.size()) {
				throw std::runtime_error("Mach-O chained bind ordinal is out of range");
			}
			value = AddSignedAddress(bind_addresses[fixup.bind_ordinal], fixup.addend);
		} else {
			if (fixup.pointer_format == 6 || fixup.pointer_format == 7 ||
				fixup.pointer_format == 9 || fixup.pointer_format == 12) {
				const auto text_segment = std::find_if(mapping.Segments().begin(),
					mapping.Segments().end(), [](const auto& segment) {
						return segment.name == "__TEXT";
					});
				if (text_segment == mapping.Segments().end())
					throw std::runtime_error("Mach-O chained offset fixup has no __TEXT base");
				if (fixup.target > std::numeric_limits<std::uintptr_t>::max() -
					text_segment->address)
					throw std::overflow_error("Mach-O chained offset target overflows");
				value = text_segment->address + static_cast<std::uintptr_t>(fixup.target);
			} else if (fixup.pointer_format == 2) {
				const auto high8 = fixup.target & 0xff00000000000000ull;
				const auto unslid_target = static_cast<std::uintptr_t>(
					fixup.target & ((1ull << 36) - 1));
				const auto slid_target = AddSignedAddress(unslid_target,
					mapping.Slide());
				value = (slid_target & 0x00ffffffffffffffull) | high8;
			} else {
				value = AddSignedAddress(static_cast<std::uintptr_t>(fixup.target),
					mapping.Slide());
			}
		}
		ApplyAbsolute64(mapping, fixup.vm_address, value);
	}
}

void MachOImage::ApplyChainedFixups(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	if (!IsArm64()) {
		RequireNativeExecution(*this);
	}
	std::vector<std::uintptr_t> addresses;
	addresses.reserve(m_chained_imports.size());
	for (const auto& import : m_chained_imports) {
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&import](const auto& binding) {
				return binding.symbol == import.symbol &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						import.dylib_ordinal);
			});
		if (match == bindings.end() && !import.weak) {
			throw std::runtime_error("unresolved chained import: " + import.symbol);
		}
		addresses.push_back(AddSignedAddress(match == bindings.end() ? 0 :
			match->address, import.addend));
	}
	ApplyChainedFixups(mapping, addresses);
}

void MachOImage::ApplyRebaseActions(const Mapping& mapping) const
{
	RequireNativeExecution(*this);
	for (const auto& action : m_rebase_actions) {
		if ((action.type != 1 && action.type != 2) ||
			action.segment_index >= m_segments.size()) {
			throw std::runtime_error("unsupported Mach-O dyld rebase action");
		}
		const auto& segment = m_segments[action.segment_index];
		const auto width = action.type == 2 ? sizeof(std::uint32_t) :
			sizeof(std::uintptr_t);
		if (action.segment_offset > segment.vm_size ||
			segment.vm_size - action.segment_offset < width) {
			throw std::out_of_range("Mach-O dyld rebase offset is outside its segment");
		}
		const auto vm_address = segment.vm_address + action.segment_offset;
		const auto host_address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		if (action.type == 2) {
			std::uint32_t old_value = 0;
			std::memcpy(&old_value, reinterpret_cast<const void*>(host_address),
				sizeof(old_value));
			const auto slide = mapping.Slide();
			if (slide < -static_cast<std::intptr_t>(old_value) ||
				slide > static_cast<std::intptr_t>(
					std::numeric_limits<std::uint32_t>::max() - old_value)) {
				throw std::overflow_error("Mach-O 32-bit absolute rebase is out of range");
			}
			const auto rebased_value = static_cast<std::uint32_t>(
				static_cast<std::intptr_t>(old_value) + slide);
			ApplyAbsolute32(mapping, vm_address,
				static_cast<std::uintptr_t>(rebased_value));
		} else {
			std::uintptr_t old_value = 0;
			std::memcpy(&old_value, reinterpret_cast<const void*>(host_address),
				sizeof(old_value));
			const auto rebased_value = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(old_value) + mapping.Slide());
			ApplyAbsolute64(mapping, vm_address, rebased_value);
		}
	}
}

void MachOImage::ApplyRelative32(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	const auto site = static_cast<std::intptr_t>(vm_address) + mapping.Slide();
	const auto displacement = static_cast<std::int64_t>(target) -
		(static_cast<std::int64_t>(site) + 4);
	if (displacement < std::numeric_limits<std::int32_t>::min() ||
		displacement > std::numeric_limits<std::int32_t>::max()) {
		throw std::overflow_error("x86-64 PC-relative relocation is out of range");
	}
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t)) {
			continue;
		}
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(site), sizeof(std::int32_t),
			PAGE_READWRITE, &old_protection)) {
			ThrowLastError("VirtualProtect(Mach-O PC-relative relocation)");
		}
		const auto value = static_cast<std::int32_t>(displacement);
		std::memcpy(reinterpret_cast<void*>(site), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(site), sizeof(std::int32_t),
			ProtectionFor(segment.initial_protection), &restored_protection)) {
			ThrowLastError("VirtualProtect(Mach-O PC-relative relocation restore)");
		}
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site),
			sizeof(value));
		return;
	}
	throw std::out_of_range("Mach-O PC-relative relocation is outside a segment");
}

void MachOImage::ApplyBranchThunk(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	constexpr std::size_t ThunkBytes = 16;
	constexpr std::size_t ThunkCapacity = 0x10000 / ThunkBytes;
	const auto existing = std::find_if(mapping.m_branch_thunks.begin(),
		mapping.m_branch_thunks.end(), [target](const auto& thunk) {
			return thunk.first == target;
		});
	if (existing != mapping.m_branch_thunks.end()) {
		ApplyRelative32(mapping, vm_address, existing->second);
		return;
	}
	if (mapping.m_reservation == nullptr || mapping.m_thunk_pool_used >= ThunkCapacity)
		throw std::runtime_error("Mach-O x86-64 branch thunk pool is exhausted");
	const auto pool_base = reinterpret_cast<std::uintptr_t>(mapping.m_reservation) +
		mapping.m_thunk_pool_offset;
	const auto thunk_address = pool_base + mapping.m_thunk_pool_used * ThunkBytes;
	const auto page_address = thunk_address & ~std::uintptr_t(0xfff);
	const auto page_offset = thunk_address - page_address;
	const bool first_on_page = (page_offset == 0);
	if (first_on_page && VirtualAlloc(reinterpret_cast<void*>(page_address), 0x1000,
		MEM_COMMIT, PAGE_READWRITE) == nullptr) {
		ThrowLastError("VirtualAlloc(Mach-O branch thunk)");
	}
	DWORD old_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
		PAGE_READWRITE, &old_protection)) {
		ThrowLastError("VirtualProtect(Mach-O branch thunk write)");
	}
	std::array<std::uint8_t, 14> code{};
	code[0] = 0xff;
	code[1] = 0x25; // jmp qword ptr [rip + 0]
	std::memcpy(code.data() + 6, &target, sizeof(target));
	std::memcpy(reinterpret_cast<void*>(thunk_address), code.data(), code.size());
	DWORD restored_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
		PAGE_EXECUTE_READ, &restored_protection)) {
		ThrowLastError("VirtualProtect(Mach-O branch thunk execute)");
	}
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(thunk_address),
		code.size());
	mapping.m_branch_thunks.emplace_back(target, thunk_address);
	++mapping.m_thunk_pool_used;
	ApplyRelative32(mapping, vm_address, thunk_address);
}

void MachOImage::ApplyRelative32WithAddend(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target, std::int32_t addend) const
{
	const auto site = static_cast<std::intptr_t>(vm_address) + mapping.Slide();
	const auto displacement = static_cast<std::int64_t>(target) -
		(static_cast<std::int64_t>(site) + static_cast<std::int64_t>(addend));
	const auto minimum = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
	const auto maximum = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
	if (displacement < minimum || displacement > maximum)
		throw std::overflow_error("Mach-O signed 32-bit relocation is out of range");
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t))
			continue;
		const auto address = static_cast<std::uintptr_t>(site);
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::int32_t),
			PAGE_READWRITE, &old_protection))
			ThrowLastError("VirtualProtect(Mach-O signed 32-bit relocation)");
		const auto value = static_cast<std::int32_t>(displacement);
		std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::int32_t),
			ProtectionFor(segment.initial_protection), &restored_protection))
			ThrowLastError("VirtualProtect(Mach-O signed 32-bit relocation restore)");
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(std::int32_t));
		return;
	}
	throw std::out_of_range("Mach-O sized PC-relative relocation is outside a segment");
}

const std::uint8_t* MachOImage::FileData(std::uint64_t offset, std::size_t bytes) const
{
	if (!RangeFits(m_file_bytes, offset, bytes)) {
		throw std::out_of_range("Mach-O file data range is outside the mapped file");
	}
	return m_view + m_image_offset + static_cast<std::size_t>(offset);
}

MachOImage::Mapping MachOImage::MapSegments(std::uintptr_t near_address) const
{
	Require(!m_segments.empty(), "Mach-O image has no loadable segments");

	std::uint64_t min_vm = std::numeric_limits<std::uint64_t>::max();
	std::uint64_t max_vm = 0;
	for (const auto& segment : m_segments) {
		Require(segment.vm_size != 0, "Mach-O segment has zero VM size");
		Require(segment.vm_address <= std::numeric_limits<std::uint64_t>::max() - segment.vm_size,
			"Mach-O segment VM range overflows");
		min_vm = std::min(min_vm, AlignDown(segment.vm_address));
		max_vm = std::max(max_vm, AlignUp(segment.vm_address + segment.vm_size));
	}
	Require(max_vm > min_vm && max_vm - min_vm <= std::numeric_limits<std::size_t>::max(),
		"Mach-O VM range is invalid");
	const auto image_reservation_bytes = static_cast<std::size_t>(max_vm - min_vm);
	constexpr std::size_t BranchThunkPoolBytes = 0x10000;
	Require(image_reservation_bytes <= std::numeric_limits<std::size_t>::max() -
		BranchThunkPoolBytes, "Mach-O reservation including branch thunks overflows");
	const auto reservation_bytes = image_reservation_bytes + BranchThunkPoolBytes;
	void* reservation = nullptr;
	if (near_address != 0) {
		constexpr std::uintptr_t AllocationGranularity = 0x10000;
		constexpr std::uintptr_t SearchStep = 0x1000000;
		// Keep every dependency reservation, including its thunk pool, inside
		// one shared window narrower than the signed x86-64 PC-relative range.
		constexpr std::uintptr_t SearchLimit = 0x3f000000;
		constexpr std::uintptr_t WindowBytes = SearchLimit * 2;
		const auto aligned_reference = near_address & ~(AllocationGranularity - 1);
		const auto lower_bound = aligned_reference > SearchLimit ?
			aligned_reference - SearchLimit : AllocationGranularity;
		const auto upper_center = aligned_reference <=
			std::numeric_limits<std::uintptr_t>::max() - SearchLimit ?
			aligned_reference + SearchLimit :
			std::numeric_limits<std::uintptr_t>::max();
		const auto upper_bound = reservation_bytes <= WindowBytes &&
			upper_center >= reservation_bytes ?
			upper_center - reservation_bytes : 0;
		for (std::uintptr_t distance = 0;
			distance <= SearchLimit && reservation == nullptr && upper_bound >= lower_bound;
			distance += SearchStep) {
			const std::uintptr_t candidates[] = {
				aligned_reference + distance,
				distance <= aligned_reference ? aligned_reference - distance : 0};
			for (const auto candidate : candidates) {
				if (candidate < lower_bound || candidate > upper_bound ||
					candidate > std::numeric_limits<std::uintptr_t>::max() - reservation_bytes)
					continue;
				reservation = VirtualAlloc(reinterpret_cast<void*>(candidate),
					reservation_bytes, MEM_RESERVE, PAGE_NOACCESS);
				if (reservation != nullptr)
					break;
			}
		}
	}
	if (reservation == nullptr)
		reservation = VirtualAlloc(nullptr, reservation_bytes, MEM_RESERVE, PAGE_NOACCESS);
	if (reservation == nullptr) {
		ThrowLastError("VirtualAlloc(Mach-O reservation)");
	}

	try {
		const auto base = reinterpret_cast<std::uintptr_t>(reservation);
		const auto slide = static_cast<std::intptr_t>(base) - static_cast<std::intptr_t>(min_vm);
		std::vector<MappedMachOSegment> mapped;
		mapped.reserve(m_segments.size());
		for (const auto& segment : m_segments) {
			const auto address = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(segment.vm_address) + slide);
			if (segment.file_size == 0 && segment.initial_protection == 0) {
				mapped.push_back({segment.name, address,
					static_cast<std::size_t>(AlignUp(segment.vm_size))});
				continue;
			}
			const auto commit_bytes = static_cast<std::size_t>(AlignUp(segment.vm_size));
			if (VirtualAlloc(reinterpret_cast<void*>(address), commit_bytes,
				MEM_COMMIT, PAGE_READWRITE) == nullptr) {
				ThrowLastError("VirtualAlloc(Mach-O segment)");
			}
			if (segment.file_size != 0) {
				const auto* source = FileData(segment.file_offset,
					static_cast<std::size_t>(segment.file_size));
				std::memcpy(reinterpret_cast<void*>(address), source,
					static_cast<std::size_t>(segment.file_size));
			}
			DWORD old_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), commit_bytes,
				ProtectionFor(segment.initial_protection), &old_protection)) {
				ThrowLastError("VirtualProtect(Mach-O segment)");
			}
			mapped.push_back({segment.name, address, commit_bytes});
		}

		std::uintptr_t entry_address = 0;
		for (const auto& segment : m_segments) {
			if (m_entry_offset >= segment.file_offset &&
				m_entry_offset - segment.file_offset < segment.file_size) {
				entry_address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(segment.vm_address) + slide +
					static_cast<std::intptr_t>(m_entry_offset - segment.file_offset));
				break;
			}
		}
		Require(entry_address != 0 || m_header->file_type != MH_EXECUTE,
			"Mach-O executable entry point is not inside a file-backed segment");
		return Mapping(reservation, std::move(mapped), entry_address, slide,
			reservation_bytes, image_reservation_bytes);
	} catch (...) {
		VirtualFree(reservation, 0, MEM_RELEASE);
		throw;
	}
}

void MachOImage::ExecuteEntryNoArgs(const Mapping& mapping) const
{
	RequireNativeExecution(*this);
	if (mapping.EntryAddress() == 0) {
		throw std::runtime_error("Mach-O mapping has no executable entry address");
	}
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<void*>(mapping.EntryAddress()), 1);
	using EntryFunction = abi::GuestVoidFn;
	auto entry = reinterpret_cast<EntryFunction>(mapping.EntryAddress());
	entry();
}

void MachOImage::ExecuteInitializers(const Mapping& mapping) const
{
	RequireNativeExecution(*this);
	for (const auto& section : m_sections) {
		if (section.section_name != "__mod_init_func")
			continue;
		if (section.size % sizeof(std::uintptr_t) != 0)
			throw std::runtime_error("Mach-O __mod_init_func section is not pointer-aligned");
		const auto address = SectionAddress(mapping, section.segment_name,
			section.section_name);
		const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
		for (std::size_t index = 0; index < count; ++index) {
			const auto function_address = *reinterpret_cast<const std::uintptr_t*>(
				address + index * sizeof(std::uintptr_t));
			if (function_address == 0 || function_address == static_cast<std::uintptr_t>(-1))
				continue;
			FlushInstructionCache(GetCurrentProcess(),
				reinterpret_cast<void*>(function_address), 1);
			using Initializer = abi::GuestVoidFn;
			auto initializer = reinterpret_cast<Initializer>(function_address);
			initializer();
		}
	}
}

void MachOImage::ExecuteTerminators(const Mapping& mapping) const
{
	RequireNativeExecution(*this);
	for (const auto& section : m_sections) {
		if (section.section_name != "__mod_term_func")
			continue;
		if (section.size % sizeof(std::uintptr_t) != 0)
			throw std::runtime_error("Mach-O __mod_term_func section is not pointer-aligned");
		const auto address = SectionAddress(mapping, section.segment_name,
			section.section_name);
		const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
		for (std::size_t index = count; index != 0; --index) {
			const auto function_address = *reinterpret_cast<const std::uintptr_t*>(
				address + (index - 1) * sizeof(std::uintptr_t));
			if (function_address == 0 || function_address == static_cast<std::uintptr_t>(-1))
				continue;
			FlushInstructionCache(GetCurrentProcess(),
				reinterpret_cast<void*>(function_address), 1);
			using Terminator = abi::GuestVoidFn;
			auto terminator = reinterpret_cast<Terminator>(function_address);
			terminator();
		}
	}
}

int MachOImage::ExecuteEntry(const Mapping& mapping,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment,
	const std::vector<std::string>& apple_parameters,
	DarwinEntryAbi entry_abi) const
{
	RequireNativeExecution(*this);
	if (mapping.EntryAddress() == 0) {
		throw std::runtime_error("Mach-O mapping has no executable entry address");
	}
	if (arguments.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		throw std::overflow_error("Mach-O argument count exceeds int range");
	}
	std::vector<std::vector<char>> argument_storage;
	argument_storage.reserve(arguments.size());
	std::vector<char*> argv;
	argv.reserve(arguments.size() + 1);
	for (const auto& argument : arguments) {
		argument_storage.emplace_back(argument.begin(), argument.end());
		argument_storage.back().push_back('\0');
		argv.push_back(argument_storage.back().data());
	}
	argv.push_back(nullptr);
	std::vector<std::vector<char>> environment_storage;
	environment_storage.reserve(environment.size());
	std::vector<char*> envp;
	envp.reserve(environment.size() + 1);
	for (const auto& value : environment) {
		environment_storage.emplace_back(value.begin(), value.end());
		environment_storage.back().push_back('\0');
		envp.push_back(environment_storage.back().data());
	}
	envp.push_back(nullptr);
	if (argv.empty() || argv.back() != nullptr ||
		envp.empty() || envp.back() != nullptr) {
		throw std::logic_error("Darwin startup vectors must be null terminated");
	}
	// Darwin exposes the same launch-owned vector through `environ` and
	// NSGetEnviron. Bind it before any guest startup code can observe it.
	darling_windows_set_environ_vector(
		const_cast<const char* const*>(envp.data()));
	// Materialize Apple parameters with the same lifetime as argv/envp. The
	// DarwinStart path places this vector after the null-terminated envp vector;
	// compatible-main entry keeps the parameters available to the launch scope.
	std::vector<std::vector<char>> apple_storage;
	apple_storage.reserve(apple_parameters.size());
	std::vector<char*> apple;
	apple.reserve(apple_parameters.size() + 1);
	for (const auto& parameter : apple_parameters) {
		apple_storage.emplace_back(parameter.begin(), parameter.end());
		apple_storage.back().push_back('\0');
		apple.push_back(apple_storage.back().data());
	}
	apple.push_back(nullptr);
	if (apple.empty() || apple.back() != nullptr) {
		throw std::logic_error("Darwin Apple parameter vector must be null terminated");
	}
	if (entry_abi == DarwinEntryAbi::DarwinStart) {
	#if defined(_WIN64) && defined(__clang__)
		// Darwin's crt1 entry receives argc at the initial stack pointer, followed by argv, envp and
		// the Apple parameter vector. Keep all pointed-to storage alive until
		// the non-returning entry transfer.
		// Leave committed scratch space below the initial Darwin stack.  The
		// guest entry may issue a normal call, which pushes its return address
		// below RSP even though the Darwin ABI starts with argc at [RSP].
		// Keep substantial native stack below the Darwin initial words. A guest
		// call into a SysV host bridge still needs return-address space and a
		// bounded prologue below RSP. The buffer stays on the native thread stack:
		// heap-backed alternate stacks faulted while guest code called Windows/SEH.
		constexpr std::size_t startup_stack_guard_words = 512;
		constexpr std::size_t startup_stack_capacity = 2048;
		alignas(16) std::array<std::uintptr_t, startup_stack_capacity> startup_stack{};
		const auto startup_words = startup_stack_guard_words + 1 + argv.size() +
			envp.size() + apple.size();
		if (startup_words > startup_stack.size()) {
			throw std::runtime_error("Darwin startup stack exceeds native stack fixture capacity");
		}
		std::size_t startup_index = startup_stack_guard_words;
		startup_stack[startup_index++] = static_cast<std::uintptr_t>(arguments.size());
		for (const auto value : argv)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		for (const auto value : envp)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		for (const auto value : apple)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		if (std::getenv("WINTOSH_TRACE_DARWIN_START") != nullptr) {
			std::fprintf(stderr,
				"WINTOSH_DARWIN_START_STACK=0x%llx argc=%zu argv=0x%llx envp=0x%llx apple=0x%llx words=%llx,%llx,%llx,%llx,%llx,%llx\n",
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words)),
				arguments.size(),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1)),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1 + argv.size())),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1 + argv.size() + envp.size())),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 1]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 2]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 3]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 4]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 5]));
		}
		darling_windows_enter_darwin_start(
			reinterpret_cast<void*>(mapping.EntryAddress()),
			startup_stack.data() + startup_stack_guard_words);
		throw std::runtime_error("Darwin startup entry returned unexpectedly");
#else
		throw std::runtime_error(
			"Darwin startup transfer requires a supported Clang Windows host architecture");
#endif
	}
	if (entry_abi == DarwinEntryAbi::CompatibleMain) {
#if !defined(__clang__)
		throw std::runtime_error(
			"real Mach-O execution requires a Clang build with System-V ABI support");
#endif
	}
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<void*>(mapping.EntryAddress()), 1);
	using EntryFunction = abi::GuestFn<int, int, char**, char**>;
	auto entry = reinterpret_cast<EntryFunction>(mapping.EntryAddress());
	return entry(static_cast<int>(arguments.size()), argv.data(), envp.data());
}

MachOImage::MachOImage(HANDLE file, HANDLE mapping, const std::uint8_t* view,
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
	std::vector<DyldChainedFixup> chained_fixups, std::vector<DyldChainedImport> chained_imports,
	std::vector<MachOReexport> reexports,
	std::uint64_t entry_offset, std::array<std::uint8_t, 16> uuid,
	std::string install_name, std::string dylinker_path) noexcept :
	m_file(file), m_mapping(mapping), m_view(view), m_file_bytes(file_bytes),
	m_image_offset(image_offset),
	m_header_storage(header), m_header(&m_header_storage), m_is_32_bit(is_32_bit),
	m_segments(std::move(segments)),
	m_sections(std::move(sections)), m_dependencies(std::move(dependencies)),
	m_weak_dependencies(std::move(weak_dependencies)),
	m_rpaths(std::move(rpaths)),
	m_symbols(std::move(symbols)),
	m_relocations(std::move(relocations)),
	m_indirect_symbols(std::move(indirect_symbols)),
	m_bind_actions(std::move(bind_actions)),
	m_threaded_bind_targets(std::move(threaded_bind_targets)),
	m_rebase_actions(std::move(rebase_actions)),
	m_chained_fixups(std::move(chained_fixups)),
	m_chained_imports(std::move(chained_imports)),
	m_reexports(std::move(reexports)),
	m_uuid(uuid),
	m_install_name(std::move(install_name)),
	m_dylinker_path(std::move(dylinker_path)),
	m_entry_offset(entry_offset)
{
}

MachOImage::MachOImage(MachOImage&& other) noexcept :
	m_file(other.m_file), m_mapping(other.m_mapping), m_view(other.m_view),
	m_file_bytes(other.m_file_bytes), m_image_offset(other.m_image_offset),
	m_header_storage(other.m_header_storage), m_header(&m_header_storage),
	m_is_32_bit(other.m_is_32_bit),
	m_segments(std::move(other.m_segments)),
	m_sections(std::move(other.m_sections)),
	m_dependencies(std::move(other.m_dependencies)),
	m_weak_dependencies(std::move(other.m_weak_dependencies)),
	m_rpaths(std::move(other.m_rpaths)),
	m_symbols(std::move(other.m_symbols)),
	m_relocations(std::move(other.m_relocations)),
	m_indirect_symbols(std::move(other.m_indirect_symbols)),
	m_bind_actions(std::move(other.m_bind_actions)),
	m_threaded_bind_targets(std::move(other.m_threaded_bind_targets)),
	m_rebase_actions(std::move(other.m_rebase_actions)),
	m_chained_fixups(std::move(other.m_chained_fixups)),
	m_chained_imports(std::move(other.m_chained_imports)),
	m_reexports(std::move(other.m_reexports)),
	m_uuid(other.m_uuid),
	m_install_name(std::move(other.m_install_name)),
	m_dylinker_path(std::move(other.m_dylinker_path)),
	m_entry_offset(other.m_entry_offset)
{
	other.m_file = INVALID_HANDLE_VALUE;
	other.m_mapping = nullptr;
	other.m_view = nullptr;
	other.m_image_offset = 0;
	other.m_header = &other.m_header_storage;
	other.m_is_32_bit = false;
}

MachOImage& MachOImage::operator=(MachOImage&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_file = other.m_file;
		m_mapping = other.m_mapping;
		m_view = other.m_view;
		m_file_bytes = other.m_file_bytes;
		m_image_offset = other.m_image_offset;
		m_header_storage = other.m_header_storage;
		m_header = &m_header_storage;
		m_is_32_bit = other.m_is_32_bit;
		m_segments = std::move(other.m_segments);
		m_sections = std::move(other.m_sections);
		m_dependencies = std::move(other.m_dependencies);
		m_weak_dependencies = std::move(other.m_weak_dependencies);
		m_rpaths = std::move(other.m_rpaths);
		m_symbols = std::move(other.m_symbols);
		m_relocations = std::move(other.m_relocations);
		m_indirect_symbols = std::move(other.m_indirect_symbols);
		m_bind_actions = std::move(other.m_bind_actions);
		m_threaded_bind_targets = std::move(other.m_threaded_bind_targets);
		m_rebase_actions = std::move(other.m_rebase_actions);
		m_chained_fixups = std::move(other.m_chained_fixups);
		m_chained_imports = std::move(other.m_chained_imports);
		m_reexports = std::move(other.m_reexports);
		m_uuid = other.m_uuid;
		m_install_name = std::move(other.m_install_name);
		m_dylinker_path = std::move(other.m_dylinker_path);
		m_entry_offset = other.m_entry_offset;
		other.m_file = INVALID_HANDLE_VALUE;
		other.m_mapping = nullptr;
		other.m_view = nullptr;
		other.m_image_offset = 0;
		other.m_header = &other.m_header_storage;
		other.m_is_32_bit = false;
	}
	return *this;
}

MachOImage::~MachOImage() noexcept
{
	Reset();
}

void MachOImage::Reset() noexcept
{
	if (m_view != nullptr) {
		UnmapViewOfFile(m_view);
		m_view = nullptr;
	}
	if (m_mapping != nullptr) {
		CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
	if (m_file != INVALID_HANDLE_VALUE) {
		CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
	}
	m_header = &m_header_storage;
	m_is_32_bit = false;
}

MachOImage::Mapping::Mapping(void* reservation, std::vector<MappedMachOSegment> segments,
	std::uintptr_t entry_address, std::intptr_t slide, std::size_t reservation_bytes,
	std::size_t thunk_pool_offset) noexcept :
	m_reservation(reservation), m_segments(std::move(segments)), m_entry_address(entry_address),
	m_slide(slide), m_reservation_bytes(reservation_bytes),
	m_thunk_pool_offset(thunk_pool_offset)
{
}

MachOImage::Mapping::Mapping(Mapping&& other) noexcept :
	m_reservation(other.m_reservation), m_segments(std::move(other.m_segments)),
	m_entry_address(other.m_entry_address), m_slide(other.m_slide),
	m_reservation_bytes(other.m_reservation_bytes),
	m_thunk_pool_offset(other.m_thunk_pool_offset),
	m_thunk_pool_used(other.m_thunk_pool_used),
	m_branch_thunks(std::move(other.m_branch_thunks)),
	m_relocation_original_values(std::move(other.m_relocation_original_values)),
	m_relocation_original_sites(std::move(other.m_relocation_original_sites)),
	m_relocation_original_widths(std::move(other.m_relocation_original_widths))
{
	other.m_reservation = nullptr;
	other.m_reservation_bytes = 0;
	other.m_thunk_pool_offset = 0;
	other.m_thunk_pool_used = 0;
}

MachOImage::Mapping& MachOImage::Mapping::operator=(Mapping&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_reservation = other.m_reservation;
		m_segments = std::move(other.m_segments);
		m_entry_address = other.m_entry_address;
		m_slide = other.m_slide;
		m_reservation_bytes = other.m_reservation_bytes;
		m_thunk_pool_offset = other.m_thunk_pool_offset;
		m_thunk_pool_used = other.m_thunk_pool_used;
		m_branch_thunks = std::move(other.m_branch_thunks);
		m_relocation_original_values = std::move(other.m_relocation_original_values);
		m_relocation_original_sites = std::move(other.m_relocation_original_sites);
		m_relocation_original_widths = std::move(other.m_relocation_original_widths);
		other.m_reservation = nullptr;
		other.m_reservation_bytes = 0;
		other.m_thunk_pool_offset = 0;
		other.m_thunk_pool_used = 0;
	}
	return *this;
}

MachOImage::Mapping::~Mapping() noexcept
{
	Reset();
}

void MachOImage::Mapping::Reset() noexcept
{
	if (m_reservation != nullptr) {
		VirtualFree(m_reservation, 0, MEM_RELEASE);
		m_reservation = nullptr;
	}
	m_reservation_bytes = 0;
	m_thunk_pool_offset = 0;
	m_thunk_pool_used = 0;
	m_branch_thunks.clear();
	m_relocation_original_values.clear();
	m_relocation_original_sites.clear();
	m_relocation_original_widths.clear();
}

void MachOLazyBindingSession::ApplyOne(std::size_t action_index) const
{
	m_image.ApplyLazyBindAction(action_index, m_mapping, m_bindings);
}

void MachOLazyBindingSession::ApplyAll() const
{
	m_image.ApplyLazyBindActions(m_mapping, m_bindings);
}

bool MachOLazyBindingSession::ApplyForSlot(std::uintptr_t slot_address) const
{
	const auto action = FindActionForSlot(slot_address);
	if (!action.has_value())
		return false;
	ApplyOne(*action);
	return true;
}

std::optional<std::size_t> MachOLazyBindingSession::FindActionForLazyInfo(
	std::uintptr_t lazy_binding_info) const noexcept
{
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (action.lazy && action.stream_offset == lazy_binding_info)
			return index;
	}
	return std::nullopt;
}

std::optional<std::uintptr_t> MachOLazyBindingSession::ResolveForLazyInfo(
	std::uintptr_t lazy_binding_info) const
{
	const auto same_binding_symbol = [](const std::string& left,
		const std::string& right) {
		return left == right || (left.size() > 1 && left.front() == '_' &&
			left.substr(1) == right);
	};
	std::optional<std::uintptr_t> resolved_target;
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (!action.lazy || action.stream_offset != lazy_binding_info)
			continue;
		ApplyOne(index);
		const auto found = std::find_if(m_bindings.begin(), m_bindings.end(),
			[&action, &same_binding_symbol](const DyldResolvedBinding& binding) {
				return same_binding_symbol(action.symbol, binding.symbol) &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						action.dylib_ordinal);
			});
		if (found != m_bindings.end())
			resolved_target = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(found->address) + action.addend);
		else if (action.weak)
			resolved_target = 0;
	}
	return resolved_target;
}

std::optional<std::size_t> MachOLazyBindingSession::FindActionForSlot(
	std::uintptr_t slot_address) const noexcept
{
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (!action.lazy || action.segment_index >= m_image.Segments().size())
			continue;
		const auto& segment = m_image.Segments()[action.segment_index];
		const auto candidate = static_cast<std::uintptr_t>(segment.vm_address) +
			static_cast<std::uintptr_t>(action.segment_offset) +
			static_cast<std::intptr_t>(m_mapping.Slide());
		if (candidate == slot_address)
			return index;
	}
	return std::nullopt;
}

MachOLazyBindingSession::Activation::Activation(
	const MachOLazyBindingSession* session) noexcept
	: previous_(active_lazy_binding_session), session_(session), active_(true)
{
	active_lazy_binding_session = session;
	try {
		std::lock_guard lock(active_lazy_binding_sessions_mutex);
		active_lazy_binding_sessions.push_back(session);
	} catch (...) {
		// Activation is noexcept. If bookkeeping allocation fails, the binder
		// fails closed instead of terminating the host process.
	}
}

MachOLazyBindingSession::Registration::Registration(
	const MachOLazyBindingSession* session)
	: session_(session)
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	active_lazy_binding_sessions.push_back(session);
}

MachOLazyBindingSession::Registration::Registration(Registration&& other) noexcept
	: session_(other.session_)
{
	other.session_ = nullptr;
}

MachOLazyBindingSession::Registration& MachOLazyBindingSession::Registration::operator=(
	Registration&& other) noexcept
{
	if (this != &other) {
		if (session_ != nullptr)
			RemoveActiveLazyBindingSession(session_);
		session_ = other.session_;
		other.session_ = nullptr;
	}
	return *this;
}

MachOLazyBindingSession::Registration::~Registration() noexcept
{
	if (session_ != nullptr)
		RemoveActiveLazyBindingSession(session_);
}

MachOLazyBindingSession::Activation::Activation(Activation&& other) noexcept
	: previous_(other.previous_), session_(other.session_), active_(other.active_)
{
	other.active_ = false;
	other.session_ = nullptr;
}

MachOLazyBindingSession::Activation& MachOLazyBindingSession::Activation::operator=(
	Activation&& other) noexcept
{
	if (this != &other) {
		if (active_) {
			active_lazy_binding_session = previous_;
			RemoveActiveLazyBindingSession(session_);
		}
		previous_ = other.previous_;
		session_ = other.session_;
		active_ = other.active_;
		other.active_ = false;
		other.session_ = nullptr;
	}
	return *this;
}

MachOLazyBindingSession::Activation::~Activation() noexcept
{
	if (active_) {
		active_lazy_binding_session = previous_;
		RemoveActiveLazyBindingSession(session_);
	}
}

const MachOLazyBindingSession* MachOLazyBindingSession::Current() noexcept
{
	return active_lazy_binding_session;
}

bool MachOLazyBindingSession::HasActiveSessions() noexcept
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	return !active_lazy_binding_sessions.empty();
}

bool MachOLazyBindingSession::OwnsImageCacheAddress(
	std::uintptr_t image_cache) const noexcept
{
	if (image_cache == 0) return false;
	for (const auto& segment : m_mapping.Segments()) {
		if (segment.size != 0 && segment.address != 0 &&
			image_cache >= segment.address &&
			image_cache - segment.address < segment.size)
			return true;
	}
	return false;
}

std::optional<std::uintptr_t> MachOLazyBindingSession::ResolveActiveForImage(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info)
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	const MachOLazyBindingSession* unique_candidate = nullptr;
	for (auto session = active_lazy_binding_sessions.rbegin();
		session != active_lazy_binding_sessions.rend(); ++session) {
		if (!(*session)->FindActionForLazyInfo(lazy_binding_info).has_value())
			continue;
		if ((*session)->OwnsImageCacheAddress(image_cache))
			return (*session)->ResolveForLazyInfo(lazy_binding_info);
		if (unique_candidate != nullptr && unique_candidate != *session)
			return std::nullopt;
		unique_candidate = *session;
	}
	// The cache pointer disambiguates identical lazy-stream offsets across
	// images. If only one active image owns the offset, it is sufficient alone.
	return unique_candidate == nullptr ? std::nullopt :
		unique_candidate->ResolveForLazyInfo(lazy_binding_info);
}

} // namespace darling::windows_host
