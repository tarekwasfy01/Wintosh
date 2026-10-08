/* Foundation ABI smoke; GPL-3.0-only. */
#include "darling_windows_foundation.h"
#include "darling_windows_stdio.h"

#include <iostream>

int main()
{
	std::size_t size = 0;
	std::size_t alignment = 0;
	const auto int_end = darling_windows_NSGetSizeAndAlignment("iXYZ", &size, &alignment);
	const bool int_ok = int_end != nullptr && *int_end == 'X' && size == sizeof(int) &&
		alignment == alignof(int);
	const auto pointer_end = darling_windows_NSGetSizeAndAlignment("^iZ", &size, &alignment);
	const bool pointer_ok = pointer_end != nullptr && *pointer_end == 'Z' &&
		size == sizeof(void*) && alignment == alignof(void*);
	const auto array_end = darling_windows_NSGetSizeAndAlignment("[4i]Z", &size, &alignment);
	const bool array_ok = array_end != nullptr && *array_end == 'Z' &&
		size == 4 * sizeof(int) && alignment == alignof(int);
	const auto struct_end = darling_windows_NSGetSizeAndAlignment("{Pair=ic}Z", &size, &alignment);
	const bool struct_ok = struct_end != nullptr && *struct_end == 'Z' &&
		size == ((sizeof(int) + sizeof(char) + alignof(int) - 1) /
			alignof(int) * alignof(int)) && alignment == alignof(int);
	const auto union_end = darling_windows_NSGetSizeAndAlignment("(Value=iq)Z", &size, &alignment);
	const bool union_ok = union_end != nullptr && *union_end == 'Z' &&
		size == sizeof(long long) && alignment == alignof(long long);
	const bool resolver_ok = darling_windows_host_symbol("NSGetSizeAndAlignment") != 0;
	const bool invalid_ok = darling_windows_NSGetSizeAndAlignment("?", &size, &alignment) == nullptr;
	const bool ok = int_ok && pointer_ok && array_ok && struct_ok && union_ok &&
		resolver_ok && invalid_ok;
	std::cout << "FOUNDATION_TYPE_ENCODING_ABI=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
}
