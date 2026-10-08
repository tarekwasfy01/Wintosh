/* Minimal Foundation ABI primitives; GPL-3.0-only, see source manifests. */
#include "darling_windows_foundation.h"

#include <cstdint>
#include <algorithm>

namespace {
std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
	return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

const char* SkipQualifiers(const char* type)
{
	while (*type == 'r' || *type == 'n' || *type == 'N' || *type == 'o' ||
		*type == 'O' || *type == 'R' || *type == 'V') ++type;
	return type;
}
}

extern "C" const char* darling_windows_NSGetSizeAndAlignment(
	const char* type, std::size_t* size, std::size_t* alignment)
{
	if (type == nullptr || size == nullptr || alignment == nullptr) return nullptr;
	type = SkipQualifiers(type);
	const std::size_t pointer_size = sizeof(void*);
	const std::size_t pointer_alignment = alignof(void*);
	switch (*type) {
	case 'c': case 'C': *size = sizeof(char); *alignment = alignof(char); return type + 1;
	case 's': case 'S': *size = sizeof(short); *alignment = alignof(short); return type + 1;
	case 'i': case 'I': *size = sizeof(int); *alignment = alignof(int); return type + 1;
	case 'l': case 'L': *size = sizeof(long); *alignment = alignof(long); return type + 1;
	case 'q': case 'Q': *size = sizeof(long long); *alignment = alignof(long long); return type + 1;
	case 'f': *size = sizeof(float); *alignment = alignof(float); return type + 1;
	case 'd': *size = sizeof(double); *alignment = alignof(double); return type + 1;
	case 'B': *size = sizeof(bool); *alignment = alignof(bool); return type + 1;
	case '@': case '#': case ':': case '*': case '^':
		*size = pointer_size; *alignment = pointer_alignment;
		if (*type == '^' && type[1] != '\0') return type + 2;
		return type + 1;
	case '[': {
		const char* cursor = type + 1;
		std::size_t count = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			count = count * 10 + static_cast<std::size_t>(*cursor - '0'); ++cursor;
		}
		std::size_t element_size = 0;
		std::size_t element_alignment = 0;
		const char* end = darling_windows_NSGetSizeAndAlignment(cursor,
			&element_size, &element_alignment);
		if (end == nullptr || *end != ']') return nullptr;
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
			std::size_t field_size = 0;
			std::size_t field_alignment = 0;
			const char* next = darling_windows_NSGetSizeAndAlignment(cursor,
				&field_size, &field_alignment);
			if (next == nullptr) return nullptr;
			if (terminator == '}') {
				aggregate_size = AlignUp(aggregate_size, field_alignment) + field_size;
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
			*size = AlignUp(aggregate_size, aggregate_alignment);
		} else {
			*alignment = union_alignment;
			*size = AlignUp(union_size, union_alignment);
		}
		return cursor + 1;
	}
	default: return nullptr;
	}
}
