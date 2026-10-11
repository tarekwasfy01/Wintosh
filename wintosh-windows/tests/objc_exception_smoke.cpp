/* Wintosh-owned Objective-C exception bridge boundary; GPL-3.0-only. */

#include "darling_windows_objc.h"

#include <iostream>

namespace {
void ThrowObject(void* context)
{
	 darling_objc_exception_throw(static_cast<id>(context));
}

void ReturnNormally(void*) {}
}

int main()
{
	const auto selector = sel_registerName("WintoshException");
	const auto exception_class = objc_allocateClassPair(nullptr,
		"WintoshExceptionObject", 0);
	if (selector == nullptr || exception_class == nullptr)
		return 1;
	objc_registerClassPair(exception_class);
	const auto object = class_createInstance(exception_class, 0);
	if (object == nullptr)
		return 2;
	id caught = nullptr;
	const bool caught_ok = darling_objc_exception_try(&ThrowObject, object, &caught) &&
		caught == object;
	const bool normal_ok = !darling_objc_exception_try(&ReturnNormally, nullptr, &caught) &&
		caught == nullptr;
	std::cout << "OBJC_EXCEPTION_BRIDGE="
		<< ((caught_ok && normal_ok) ? "PASS" : "FAIL") << "\n";
	return caught_ok && normal_ok ? 0 : 3;
}
