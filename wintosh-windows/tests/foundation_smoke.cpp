/* Foundation ABI smoke; GPL-3.0-only. */
#include "darling_windows_foundation.h"
#include "darling_windows_stdio.h"

#include <iostream>

int main()
{
	const auto first = darling_windows_NSMakeRange(4, 6);
	const auto second = darling_windows_NSMakeRange(8, 8);
	const auto intersection = darling_windows_NSIntersectionRange(first, second);
	const auto united = darling_windows_NSUnionRange(first, second);
	if (darling_windows_NSMaxRange(first) != 10 ||
		!darling_windows_NSEqualRanges(first, darling_windows_NSMakeRange(4, 6)) ||
		darling_windows_NSEqualRanges(first, second) ||
		darling_windows_NSLocationInRange(9, first) == false ||
		darling_windows_NSLocationInRange(10, first) ||
		intersection.location != 8 || intersection.length != 2 ||
		united.location != 4 || united.length != 12) {
		std::cerr << "FOUNDATION_NS_RANGE=FAIL\n";
		return 1;
	}
	std::size_t size = 0;
	std::size_t alignment = 0;
	const auto int_end = darling_windows_NSGetSizeAndAlignment("iXYZ", &size, &alignment);
	const bool int_ok = int_end != nullptr && *int_end == 'X' && size == sizeof(int) &&
		alignment == alignof(int);
	const auto pointer_end = darling_windows_NSGetSizeAndAlignment("^iZ", &size, &alignment);
	const bool pointer_ok = pointer_end != nullptr && *pointer_end == 'Z' &&
		size == sizeof(void*) && alignment == alignof(void*);
	const auto block_end = darling_windows_NSGetSizeAndAlignment("@?Z", &size, &alignment);
	const bool block_ok = block_end != nullptr && *block_end == 'Z' &&
		size == sizeof(void*) && alignment == alignof(void*);
	const auto named_object_end = darling_windows_NSGetSizeAndAlignment(
		"@\"NSString\"Z", &size, &alignment);
	const bool named_object_ok = named_object_end != nullptr && *named_object_end == 'Z' &&
		size == sizeof(void*) && alignment == alignof(void*);
	const auto struct_pointer_end = darling_windows_NSGetSizeAndAlignment(
		"^{Point=dd}Z", &size, &alignment);
	const bool struct_pointer_ok = struct_pointer_end != nullptr && *struct_pointer_end == 'Z' &&
		size == sizeof(void*) && alignment == alignof(void*);
	const auto long_double_end = darling_windows_NSGetSizeAndAlignment("DZ", &size, &alignment);
	const bool darwin_long_double_ok = long_double_end != nullptr && *long_double_end == 'Z' &&
		size == 16 && alignment == 16;
	const auto complex_end = darling_windows_NSGetSizeAndAlignment("jdZ", &size, &alignment);
	const bool complex_ok = complex_end != nullptr && *complex_end == 'Z' &&
		size == 2 * sizeof(double) && alignment == alignof(double);
	const auto long_end = darling_windows_NSGetSizeAndAlignment("lZ", &size, &alignment);
	const bool darwin_long_ok = long_end != nullptr && *long_end == 'Z' &&
		size == 8 && alignment == 8;
	const auto array_end = darling_windows_NSGetSizeAndAlignment("[4i]Z", &size, &alignment);
	const bool array_ok = array_end != nullptr && *array_end == 'Z' &&
		size == 4 * sizeof(int) && alignment == alignof(int);
	const auto struct_end = darling_windows_NSGetSizeAndAlignment("{Pair=ic}Z", &size, &alignment);
	const bool struct_ok = struct_end != nullptr && *struct_end == 'Z' &&
		size == ((sizeof(int) + sizeof(char) + alignof(int) - 1) /
			alignof(int) * alignof(int)) && alignment == alignof(int);
	const auto named_struct_end = darling_windows_NSGetSizeAndAlignment(
		"{Point=\"x\"d\"y\"d}Z", &size, &alignment);
	const bool named_struct_ok = named_struct_end != nullptr && *named_struct_end == 'Z' &&
		size == 2 * sizeof(double) && alignment == alignof(double);
	const auto union_end = darling_windows_NSGetSizeAndAlignment("(Value=iq)Z", &size, &alignment);
	const bool union_ok = union_end != nullptr && *union_end == 'Z' &&
		size == sizeof(long long) && alignment == alignof(long long);
	const bool resolver_ok = darling_windows_host_symbol("NSGetSizeAndAlignment") != 0;
	const bool invalid_ok = darling_windows_NSGetSizeAndAlignment("!", &size, &alignment) == nullptr;
	const bool array_overflow_ok = darling_windows_NSGetSizeAndAlignment(
		"[999999999999999999999999999999999999i]", &size, &alignment) == nullptr;
	const bool ok = int_ok && pointer_ok && block_ok && named_object_ok && struct_pointer_ok && darwin_long_ok && darwin_long_double_ok && complex_ok && array_ok && struct_ok && named_struct_ok && union_ok &&
		resolver_ok && invalid_ok && array_overflow_ok;
	std::cout << "FOUNDATION_TYPE_ENCODING_ABI=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
}
