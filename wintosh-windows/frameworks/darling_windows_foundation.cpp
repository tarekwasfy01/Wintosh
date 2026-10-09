/* Minimal Foundation ABI primitives; GPL-3.0-only, see source manifests. */
#include "darling_windows_foundation.h"

#include <cstdint>
#include <algorithm>
#include <limits>

namespace {
bool AlignUp(std::size_t value, std::size_t alignment, std::size_t* result)
{
	if (result == nullptr || alignment == 0) return false;
	const auto remainder = value % alignment;
	if (remainder == 0) {
		*result = value;
		return true;
	}
	const auto increment = alignment - remainder;
	if (value > (std::numeric_limits<std::size_t>::max)() - increment)
		return false;
	*result = value + increment;
	return true;
}

const char* SkipQualifiers(const char* type)
{
	while (*type == 'r' || *type == 'n' || *type == 'N' || *type == 'o' ||
		*type == 'O' || *type == 'R' || *type == 'V') ++type;
	return type;
}

const char* SkipQuotedFieldName(const char* type)
{
	if (*type != '"') return type;
	++type;
	while (*type != '\0' && *type != '"') ++type;
	return *type == '"' ? type + 1 : nullptr;
}
}

extern "C" darling_windows_NSRange darling_windows_NSMakeRange(
	std::size_t location, std::size_t length)
{
	return { location, length };
}

extern "C" std::size_t darling_windows_NSMaxRange(darling_windows_NSRange range)
{
	if (range.length > static_cast<std::size_t>(-1) - range.location)
		return static_cast<std::size_t>(-1);
	return range.location + range.length;
}

extern "C" bool darling_windows_NSLocationInRange(
	std::size_t location, darling_windows_NSRange range)
{
	return location >= range.location && location < darling_windows_NSMaxRange(range);
}

extern "C" bool darling_windows_NSEqualRanges(
	darling_windows_NSRange left, darling_windows_NSRange right)
{
	return left.location == right.location && left.length == right.length;
}

extern "C" darling_windows_NSRange darling_windows_NSIntersectionRange(
	darling_windows_NSRange left, darling_windows_NSRange right)
{
	const auto start = std::max(left.location, right.location);
	const auto end = std::min(darling_windows_NSMaxRange(left), darling_windows_NSMaxRange(right));
	return end > start ? darling_windows_NSRange{ start, end - start }
		: darling_windows_NSRange{ 0, 0 };
}

extern "C" darling_windows_NSRange darling_windows_NSUnionRange(
	darling_windows_NSRange left, darling_windows_NSRange right)
{
	if (left.length == 0) return right;
	if (right.length == 0) return left;
	const auto start = std::min(left.location, right.location);
	const auto end = std::max(darling_windows_NSMaxRange(left), darling_windows_NSMaxRange(right));
	return { start, end >= start ? end - start : 0 };
}

extern "C" const char* darling_windows_NSGetSizeAndAlignment(
	const char* type, std::size_t* size, std::size_t* alignment)
{
	if (type == nullptr || size == nullptr || alignment == nullptr) return nullptr;
	type = SkipQualifiers(type);
	const std::size_t pointer_size = sizeof(void*);
	const std::size_t pointer_alignment = alignof(void*);
	if (*type == 'j') {
		std::size_t component_size = 0;
		std::size_t component_alignment = 0;
		const char* end = darling_windows_NSGetSizeAndAlignment(type + 1,
			&component_size, &component_alignment);
		if (end == nullptr) return nullptr;
		*size = component_size * 2;
		*alignment = component_alignment;
		return end;
	}
	switch (*type) {
	case 'v': *size = 0; *alignment = 1; return type + 1;
	case 'c': case 'C': *size = sizeof(char); *alignment = alignof(char); return type + 1;
	case 's': case 'S': *size = sizeof(short); *alignment = alignof(short); return type + 1;
	case 'i': case 'I': *size = sizeof(int); *alignment = alignof(int); return type + 1;
	// Darwin's 64-bit Objective-C ABI uses LP64: long is 64-bit even when
	// the host Windows compiler uses LLP64 and sizeof(long) is only 4.
	case 'l': case 'L': *size = sizeof(std::int64_t); *alignment = alignof(std::int64_t); return type + 1;
	case 'q': case 'Q': *size = sizeof(long long); *alignment = alignof(long long); return type + 1;
	case 'f': *size = sizeof(float); *alignment = alignof(float); return type + 1;
	case 'd': *size = sizeof(double); *alignment = alignof(double); return type + 1;
	case 'D': *size = 16; *alignment = 16; return type + 1;
	case 'B': *size = sizeof(bool); *alignment = alignof(bool); return type + 1;
	case '?': *size = pointer_size; *alignment = pointer_alignment; return type + 1;
	case 'b': {
		const char* cursor = type + 1;
		if (*cursor < '0' || *cursor > '9') return nullptr;
		while (*cursor >= '0' && *cursor <= '9') ++cursor;
		// Objective-C bitfields occupy the smallest addressable word here.
		*size = sizeof(unsigned int);
		*alignment = alignof(unsigned int);
		return cursor;
	}
	case '@': case '#': case ':': case '*':
		*size = pointer_size; *alignment = pointer_alignment;
		if (*type == '@' && type[1] == '?') return type + 2;
		if (*type == '@' && type[1] == '"') {
			const char* cursor = type + 2;
			while (*cursor != '\0' && *cursor != '"') ++cursor;
			return *cursor == '"' ? cursor + 1 : nullptr;
		}
		return type + 1;
	case '^': {
		std::size_t pointee_size = 0;
		std::size_t pointee_alignment = 0;
		const char* end = darling_windows_NSGetSizeAndAlignment(type + 1,
			&pointee_size, &pointee_alignment);
		if (end == nullptr) return nullptr;
		*size = pointer_size;
		*alignment = pointer_alignment;
		return end;
	}
	case '[': {
		const char* cursor = type + 1;
		std::size_t count = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			const auto digit = static_cast<std::size_t>(*cursor - '0');
			if (count > ((std::numeric_limits<std::size_t>::max)() - digit) / 10)
				return nullptr;
			count = count * 10 + digit; ++cursor;
		}
		std::size_t element_size = 0;
		std::size_t element_alignment = 0;
		const char* end = darling_windows_NSGetSizeAndAlignment(cursor,
			&element_size, &element_alignment);
		if (end == nullptr || *end != ']') return nullptr;
		if (element_size != 0 && count > (std::numeric_limits<std::size_t>::max)() / element_size)
			return nullptr;
		*size = count * element_size;
		*alignment = element_alignment;
		return end + 1;
	}
	case '{': case '(': {
		const char terminator = *type == '{' ? '}' : ')';
		const char* cursor = type + 1;
		while (*cursor != '\0' && *cursor != '=' && *cursor != terminator) ++cursor;
		if (*cursor == '\0') return nullptr;
		if (*cursor == terminator) {
			*size = 0;
			*alignment = 1;
			return cursor + 1;
		}
		++cursor;
		std::size_t aggregate_size = 0;
		std::size_t aggregate_alignment = 1;
		std::size_t union_size = 0;
		std::size_t union_alignment = 1;
		while (*cursor != '\0' && *cursor != terminator) {
			cursor = SkipQuotedFieldName(cursor);
			if (cursor == nullptr) return nullptr;
			std::size_t field_size = 0;
			std::size_t field_alignment = 0;
			const char* next = darling_windows_NSGetSizeAndAlignment(cursor,
				&field_size, &field_alignment);
			if (next == nullptr) return nullptr;
			if (terminator == '}') {
				std::size_t aligned_size = 0;
				if (!AlignUp(aggregate_size, field_alignment, &aligned_size) ||
					aligned_size > (std::numeric_limits<std::size_t>::max)() - field_size)
					return nullptr;
				aggregate_size = aligned_size + field_size;
				aggregate_alignment = (std::max)(aggregate_alignment, field_alignment);
			} else {
				union_size = (std::max)(union_size, field_size);
				union_alignment = (std::max)(union_alignment, field_alignment);
			}
			cursor = next;
		}
		if (*cursor != terminator) return nullptr;
		if (terminator == '}') {
			*alignment = aggregate_alignment;
			if (!AlignUp(aggregate_size, aggregate_alignment, size)) return nullptr;
		} else {
			*alignment = union_alignment;
			if (!AlignUp(union_size, union_alignment, size)) return nullptr;
		}
		return cursor + 1;
	}
	default: return nullptr;
	}
}
