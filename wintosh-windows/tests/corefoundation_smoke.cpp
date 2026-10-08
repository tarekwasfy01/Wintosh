/* Stage 1 CoreFoundation value-semantics boundary proof; GPL-3.0-only. */
#include "darling_windows_corefoundation.h"

#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {
int notification_count = 0;
void NotificationCallback(const void*, const char* name, const void* object)
{
	if (std::strcmp(name, "WintoshNotification") == 0 && object != nullptr) ++notification_count;
}
int runloop_callback_value = 0;
void RunLoopCallback(void* context) { runloop_callback_value = *static_cast<int*>(context); }
}

int main()
{
	const auto string = darling_windows_CFStringCreateWithCString("Wintosh");
	char buffer[32]{};
	const bool string_ok = string != nullptr &&
		darling_windows_CFStringGetLength(string) == 7 &&
		 darling_windows_CFStringGetCString(string, buffer, sizeof(buffer)) &&
		std::strcmp(buffer, "Wintosh") == 0 &&
		std::strcmp(darling_windows_CFStringGetCStringPtr(string), "Wintosh") == 0;
	const auto prefix = darling_windows_CFStringCreateWithCString("Win");
	const auto suffix = darling_windows_CFStringCreateWithCString("tosh");
	const bool string_match_ok = darling_windows_CFStringHasPrefix(string, prefix) &&
		darling_windows_CFStringHasSuffix(string, suffix) &&
		darling_windows_CFStringCompare(string, string) == 0 &&
		darling_windows_CFStringCompare(prefix, suffix) < 0 &&
		darling_windows_CFStringFind(string, suffix, 0).location == 3;
	const auto folded_needle = darling_windows_CFStringCreateWithCString("TOSH");
	const bool string_find_ok = darling_windows_CFStringFind(string, folded_needle,
		darling_windows_CFCompareCaseInsensitive).location == 3;
	const unsigned char bytes[] = {1, 2, 3, 4};
	const auto data = darling_windows_CFDataCreate(bytes, 4);
	const bool data_ok = data != nullptr && darling_windows_CFDataGetLength(data) == 4 &&
		darling_windows_CFDataGetBytePtr(data)[2] == 3;
	unsigned char data_slice[2]{};
	const bool data_slice_ok = darling_windows_CFDataGetBytes(data, 1, 2, data_slice) &&
		data_slice[0] == 2 && data_slice[1] == 3;
	const void* values[] = {string, data};
	const auto array = darling_windows_CFArrayCreate(values, 2);
	const bool array_ok = array != nullptr && darling_windows_CFArrayGetCount(array) == 2 &&
		darling_windows_CFArrayGetValueAtIndex(array, 0) == string &&
		darling_windows_CFArrayGetValueAtIndex(array, 1) == data;
	const void* copied_values[2]{};
	darling_windows_CFArrayGetValues(array, 0, 2, copied_values);
	const bool array_values_ok = copied_values[0] == string && copied_values[1] == data;
	const bool array_search_ok = darling_windows_CFArrayGetFirstIndexOfValue(array,
		data) == 1 && darling_windows_CFArrayGetFirstIndexOfValue(array, prefix) == -1 &&
		darling_windows_CFArrayGetCountOfValue(array, string) == 1 &&
		darling_windows_CFArrayGetLastIndexOfValue(array, data) == 1;
	const auto equal_array = darling_windows_CFArrayCreate(values, 2);
	const bool collection_equal_ok = darling_windows_CFEqual(array, equal_array);
	const auto number = darling_windows_CFNumberCreateInteger(42);
	std::int64_t integer = 0;
	const bool number_ok = darling_windows_CFNumberGetInteger(number, &integer) && integer == 42;
	double integer_as_real = 0;
	const bool number_conversion_ok = darling_windows_CFNumberGetDouble(number, &integer_as_real) &&
		integer_as_real == 42;
	const auto real_number = darling_windows_CFNumberCreateDouble(3.25);
	double real_value = 0;
	const bool real_number_ok = darling_windows_CFNumberGetDouble(real_number, &real_value) &&
		real_value == 3.25;
	std::int64_t real_as_integer = 0;
	const bool real_conversion_ok = darling_windows_CFNumberGetInteger(real_number, &real_as_integer) &&
		real_as_integer == 3;
	const bool number_compare_ok = darling_windows_CFNumberCompare(number, real_number) > 0 &&
		darling_windows_CFNumberCompare(real_number, real_number) == 0 &&
		darling_windows_CFNumberCompare(real_number, number) < 0;
	const void* keys[] = {string};
	const void* mapped[] = {number};
	const auto dictionary = darling_windows_CFDictionaryCreate(keys, mapped, 1);
	const bool dictionary_ok = dictionary != nullptr &&
		darling_windows_CFDictionaryGetCount(dictionary) == 1 &&
		darling_windows_CFDictionaryGetValue(dictionary, string) == number;
	const void* copied_keys[1]{};
	const void* copied_dictionary_values[1]{};
	darling_windows_CFDictionaryGetKeysAndValues(dictionary, copied_keys,
		copied_dictionary_values);
	const bool dictionary_values_ok = copied_keys[0] == string &&
		copied_dictionary_values[0] == number &&
		darling_windows_CFDictionaryContainsKey(dictionary, string);
	const void* set_values[] = {string, string, data};
	const auto set = darling_windows_CFSetCreate(set_values, 3);
	const auto equal_data = darling_windows_CFDataCreate(bytes, 4);
	const bool set_ok = set != nullptr && darling_windows_CFSetGetCount(set) == 2 &&
		darling_windows_CFSetContainsValue(set, equal_data);
	const void* copied_set_values[2]{};
	darling_windows_CFSetGetValues(set, copied_set_values);
	const bool set_values_ok = (copied_set_values[0] == string || copied_set_values[1] == string) &&
		(copied_set_values[0] == data || copied_set_values[1] == data) &&
		darling_windows_CFSetGetValue(set, equal_data) == data;
	const auto date = darling_windows_CFDateCreate(1234.5);
	const auto earlier_date = darling_windows_CFDateCreate(1234.0);
	const auto later_date = darling_windows_CFDateCreate(1235.0);
	const auto url = darling_windows_CFURLCreateWithFileSystemPath("C:/Wintosh/app");
	const auto url_path = darling_windows_CFURLCopyFileSystemPath(url);
	char url_buffer[64]{};
	const bool date_url_ok = date != nullptr &&
		darling_windows_CFDateGetAbsoluteTime(date) == 1234.5 && url != nullptr &&
		url_path != nullptr && darling_windows_CFStringGetCString(url_path, url_buffer, sizeof(url_buffer)) &&
		std::strcmp(url_buffer, "C:/Wintosh/app") == 0;
	const bool date_compare_ok = darling_windows_CFDateCompare(earlier_date, date) < 0 &&
		darling_windows_CFDateCompare(date, date) == 0 && darling_windows_CFDateCompare(later_date, date) > 0;
  const auto shifted_date = darling_windows_CFDateCreateByAddingTimeInterval(date, 60.5);
  const bool date_interval_ok = shifted_date != nullptr &&
      darling_windows_CFDateGetAbsoluteTime(shifted_date) == 1295.0 &&
      darling_windows_CFDateGetTimeIntervalSinceDate(shifted_date, date) == 60.5 &&
      darling_windows_CFDateGetTimeIntervalSinceDate(date, shifted_date) == -60.5;
	const auto true_value = darling_windows_CFBooleanGetValue(true);
	const auto false_value = darling_windows_CFBooleanGetValue(false);
	const auto null_value = darling_windows_CFNullGetValue();
	const bool scalar_ok = true_value != false_value && darling_windows_CFBooleanIsTrue(true_value) &&
		!darling_windows_CFBooleanIsTrue(false_value) && null_value != nullptr;
	const auto equal_string = darling_windows_CFStringCreateWithCString("Wintosh");
	const bool equal_ok = darling_windows_CFEqual(string, equal_string) &&
		darling_windows_CFEqual(data, data) && !darling_windows_CFEqual(string, prefix) &&
		darling_windows_CFEqual(null_value, null_value);
	const auto mutable_string = darling_windows_CFStringCreateWithCString("mutable");
	const auto mutable_target = darling_windows_CFStringCreateWithCString("mutable");
	const auto mutable_replacement = darling_windows_CFStringCreateWithCString("darwin");
	const auto whitespace_string = darling_windows_CFStringCreateWithCString("\t Wintosh \n");
	char mutable_string_buffer[32]{};
	const bool mutable_string_ok = darling_windows_CFStringAppendCString(mutable_string, " string") &&
		darling_windows_CFStringReplaceAll(mutable_string, mutable_target, mutable_replacement) &&
		darling_windows_CFStringReplaceRange(mutable_string, 0, 6, "Wintosh") &&
		darling_windows_CFStringGetCString(mutable_string, mutable_string_buffer, sizeof(mutable_string_buffer)) &&
		std::strcmp(mutable_string_buffer, "Wintosh string") == 0;
	char string_bytes[8]{};
	const bool string_bytes_ok = darling_windows_CFStringGetBytes(mutable_string, 0, 7,
		string_bytes, sizeof(string_bytes)) == 7 && std::memcmp(string_bytes, "Wintosh", 7) == 0;
	const bool trim_ok = darling_windows_CFStringTrimWhitespace(whitespace_string) &&
		darling_windows_CFStringGetCString(whitespace_string, mutable_string_buffer, sizeof(mutable_string_buffer)) &&
		std::strcmp(mutable_string_buffer, "Wintosh") == 0;
	const bool semantic_lookup_ok = darling_windows_CFDictionaryGetValue(dictionary,
		equal_string) == number;
	auto loop = darling_windows_CFRunLoopGetCurrent();
	bool loop_started = false;
	std::thread loop_thread([&] {
		darling_windows_CFRunLoopRunInMode(5.0, false);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	loop_started = darling_windows_CFRunLoopIsRunning(loop);
	darling_windows_CFRunLoopStop(loop);
	loop_thread.join();
	const bool runloop_ok = loop_started && !darling_windows_CFRunLoopIsRunning(loop);
	int callback_value = 7;
	runloop_callback_value = 0;
	const bool queued = darling_windows_CFRunLoopPerformBlock(loop, RunLoopCallback, &callback_value);
	darling_windows_CFRunLoopWakeUp(loop);
	std::thread callback_thread([&] { darling_windows_CFRunLoopRunInMode(1.0, true); });
	callback_thread.join();
	const bool callback_ok = queued && runloop_callback_value == 7;
	int timer_value = 0;
	const bool timer_queued = darling_windows_CFRunLoopPerformOneShotTimer(
		loop, 0.01, RunLoopCallback, &callback_value);
	std::thread timer_thread([&] { darling_windows_CFRunLoopRunInMode(1.0, true); });
	timer_thread.join();
	const bool timer_ok = timer_queued && runloop_callback_value == 7;
	const auto center = darling_windows_CFNotificationCenterGetLocal();
	const int notification_object = 1;
	const bool notification_ok = darling_windows_CFNotificationCenterAddObserver(
		center, &notification_count, NotificationCallback, "WintoshNotification");
	darling_windows_CFNotificationCenterPostNotification(center, "WintoshNotification",
		&notification_object);
	const bool delivered = notification_count == 1;
	darling_windows_CFNotificationCenterRemoveObserver(center, &notification_count,
		NotificationCallback, "WintoshNotification");
	const bool notification_global_remove_ok = darling_windows_CFNotificationCenterAddObserver(
		center, &notification_count, NotificationCallback, "WintoshNotificationA") &&
		darling_windows_CFNotificationCenterAddObserver(
		center, &notification_count, NotificationCallback, "WintoshNotificationB") &&
		darling_windows_CFNotificationCenterRemoveObserver(
		center, &notification_count, NotificationCallback, nullptr);
	const auto plist = darling_windows_CFPropertyListCreateXML(dictionary);
	char plist_buffer[512]{};
	const bool plist_ok = plist != nullptr && darling_windows_CFStringGetCString(
		plist, plist_buffer, sizeof(plist_buffer)) &&
		std::strstr(plist_buffer, "<dict>") != nullptr &&
		std::strstr(plist_buffer, "<integer>42</integer>") != nullptr;
	const auto array_plist = darling_windows_CFPropertyListCreateXML(array);
	char array_plist_buffer[512]{};
	const bool array_plist_ok = array_plist != nullptr &&
		darling_windows_CFStringGetCString(array_plist, array_plist_buffer,
			sizeof(array_plist_buffer)) &&
		std::strstr(array_plist_buffer, "<array>") != nullptr &&
		std::strstr(array_plist_buffer, "<string>Wintosh</string>") != nullptr;
	const auto data_plist = darling_windows_CFPropertyListCreateXML(data);
	char data_plist_buffer[512]{};
	const bool data_plist_ok = data_plist != nullptr &&
		darling_windows_CFStringGetCString(data_plist, data_plist_buffer,
			sizeof(data_plist_buffer)) &&
		std::strstr(data_plist_buffer, "<data>AQIDBA==</data>") != nullptr;
	const auto date_plist = darling_windows_CFPropertyListCreateXML(date);
	char date_plist_buffer[512]{};
	const bool date_plist_ok = date_plist != nullptr &&
		darling_windows_CFStringGetCString(date_plist, date_plist_buffer,
			sizeof(date_plist_buffer)) &&
		std::strstr(date_plist_buffer, "<date>2001-01-01T00:20:34.500Z</date>") != nullptr;
	const auto real_plist = darling_windows_CFPropertyListCreateXML(real_number);
	char real_plist_buffer[512]{};
	const bool real_plist_ok = real_plist != nullptr &&
		darling_windows_CFStringGetCString(real_plist, real_plist_buffer,
			sizeof(real_plist_buffer)) &&
		std::strstr(real_plist_buffer, "<real>3.25</real>") != nullptr;
	const auto escaped_string = darling_windows_CFStringCreateWithCString("<&\"'");
	const void* escaped_keys[] = {escaped_string};
	const void* escaped_values[] = {escaped_string};
	const auto escaped_dictionary = darling_windows_CFDictionaryCreate(escaped_keys,
		escaped_values, 1);
	const auto escaped_plist = darling_windows_CFPropertyListCreateXML(escaped_dictionary);
	char escaped_buffer[512]{};
	const bool escaping_ok = escaped_plist != nullptr &&
		darling_windows_CFStringGetCString(escaped_plist, escaped_buffer, sizeof(escaped_buffer)) &&
		std::strstr(escaped_buffer, "&lt;&amp;&quot;&apos;") != nullptr;
	const auto parsed = darling_windows_CFPropertyListCreateFromXML(
		"<?xml version=\"1.0\"?><plist version=\"1.0\"><array><string>A&amp;B</string><integer>7</integer><true/></array></plist>");
	const auto parsed_xml = darling_windows_CFPropertyListCreateXML(parsed);
	char parsed_buffer[512]{};
	const bool plist_parse_ok = parsed != nullptr && parsed_xml != nullptr &&
		darling_windows_CFStringGetCString(parsed_xml, parsed_buffer, sizeof(parsed_buffer)) &&
		std::strstr(parsed_buffer, "<string>A&amp;B</string>") != nullptr &&
		std::strstr(parsed_buffer, "<integer>7</integer>") != nullptr;
	const auto parsed_dict = darling_windows_CFPropertyListCreateFromXML(
		"<plist><dict><key>Name</key><string>Wintosh</string><key>Enabled</key><true/></dict></plist>");
	const auto parsed_dict_xml = darling_windows_CFPropertyListCreateXML(parsed_dict);
	char parsed_dict_buffer[512]{};
	const bool plist_dict_parse_ok = parsed_dict != nullptr && parsed_dict_xml != nullptr &&
		darling_windows_CFStringGetCString(parsed_dict_xml, parsed_dict_buffer, sizeof(parsed_dict_buffer)) &&
		std::strstr(parsed_dict_buffer, "<key>Name</key>") != nullptr &&
		std::strstr(parsed_dict_buffer, "<string>Wintosh</string>") != nullptr;
	const auto parsed_data = darling_windows_CFPropertyListCreateFromXML(
		"<plist><data>AQIDBA==</data></plist>");
	const bool plist_data_parse_ok = parsed_data != nullptr &&
		darling_windows_CFDataGetLength(static_cast<darling_windows_CFDataRef>(parsed_data)) == 4 &&
		darling_windows_CFDataGetBytePtr(static_cast<darling_windows_CFDataRef>(parsed_data))[2] == 3;
	const auto parsed_date = darling_windows_CFPropertyListCreateFromXML(
		"<plist><date>2001-01-01T00:00:42Z</date></plist>");
	const bool plist_date_parse_ok = parsed_date != nullptr &&
		darling_windows_CFDateGetAbsoluteTime(static_cast<darling_windows_CFDateRef>(parsed_date)) == 42;
	const auto parsed_fractional_date = darling_windows_CFPropertyListCreateFromXML(
		"<plist><date>2001-01-01T00:00:42.125Z</date></plist>");
	const bool plist_fractional_date_ok = parsed_fractional_date != nullptr &&
		std::abs(darling_windows_CFDateGetAbsoluteTime(
			static_cast<darling_windows_CFDateRef>(parsed_fractional_date)) - 42.125) < 1e-9;
	const auto parsed_offset_date = darling_windows_CFPropertyListCreateFromXML(
		"<plist><date>2001-01-01T01:00:42+01:00</date></plist>");
	const bool plist_offset_date_ok = parsed_offset_date != nullptr &&
		darling_windows_CFDateGetAbsoluteTime(static_cast<darling_windows_CFDateRef>(parsed_offset_date)) == 42;
	std::vector<unsigned char> binary_string_plist(43, 0);
	std::memcpy(binary_string_plist.data(), "bplist00", 8);
	binary_string_plist[8] = 0x51;
	binary_string_plist[9] = 'X';
	binary_string_plist[10] = 8; // one-byte offset table entry
	const auto trailer = binary_string_plist.data() + 11;
	trailer[6] = 1; // offset integer size
	trailer[7] = 1; // object reference size
	trailer[15] = 1; // one object
	trailer[31] = 10; // offset table starts after object and offset entry
	const auto parsed_binary = darling_windows_CFPropertyListCreateFromBinary(
		binary_string_plist.data(), static_cast<darling_windows_CFIndex>(binary_string_plist.size()));
	char binary_buffer[16]{};
	const bool binary_plist_ok = parsed_binary != nullptr &&
		darling_windows_CFStringGetCString(static_cast<darling_windows_CFStringRef>(parsed_binary),
		binary_buffer, sizeof(binary_buffer)) && std::strcmp(binary_buffer, "X") == 0;
	std::vector<unsigned char> binary_dictionary_plist(50, 0);
	std::memcpy(binary_dictionary_plist.data(), "bplist00", 8);
	binary_dictionary_plist[8] = 0xD1; binary_dictionary_plist[9] = 1; binary_dictionary_plist[10] = 2;
	binary_dictionary_plist[11] = 0x51; binary_dictionary_plist[12] = 'K';
	binary_dictionary_plist[13] = 0x51; binary_dictionary_plist[14] = 'V';
	binary_dictionary_plist[15] = 8; binary_dictionary_plist[16] = 11; binary_dictionary_plist[17] = 13;
	const auto dictionary_trailer = binary_dictionary_plist.data() + 18;
	dictionary_trailer[6] = 1; dictionary_trailer[7] = 1; dictionary_trailer[15] = 3;
	dictionary_trailer[31] = 15;
	const auto parsed_binary_dictionary = darling_windows_CFPropertyListCreateFromBinary(
		binary_dictionary_plist.data(), static_cast<darling_windows_CFIndex>(binary_dictionary_plist.size()));
	const auto binary_dictionary_key = darling_windows_CFStringCreateWithCString("K");
	const auto binary_dictionary_value = darling_windows_CFDictionaryGetValue(
		static_cast<darling_windows_CFDictionaryRef>(parsed_binary_dictionary), binary_dictionary_key);
	const bool binary_dictionary_ok = parsed_binary_dictionary != nullptr && binary_dictionary_value != nullptr &&
		darling_windows_CFStringGetCString(static_cast<darling_windows_CFStringRef>(binary_dictionary_value),
			binary_buffer, sizeof(binary_buffer)) && std::strcmp(binary_buffer, "V") == 0;
	std::vector<unsigned char> binary_utf16_plist(46, 0);
	std::memcpy(binary_utf16_plist.data(), "bplist00", 8);
	binary_utf16_plist[8] = 0x62; binary_utf16_plist[9] = 0xd8; binary_utf16_plist[10] = 0x3d;
	binary_utf16_plist[11] = 0xde; binary_utf16_plist[12] = 0x00; binary_utf16_plist[13] = 8;
	const auto utf16_trailer = binary_utf16_plist.data() + 14;
	utf16_trailer[6] = 1; utf16_trailer[7] = 1; utf16_trailer[15] = 1; utf16_trailer[31] = 13;
	const auto parsed_binary_utf16 = darling_windows_CFPropertyListCreateFromBinary(
		binary_utf16_plist.data(), static_cast<darling_windows_CFIndex>(binary_utf16_plist.size()));
	const bool binary_utf16_ok = parsed_binary_utf16 != nullptr &&
		darling_windows_CFStringGetCString(static_cast<darling_windows_CFStringRef>(parsed_binary_utf16),
		binary_buffer, sizeof(binary_buffer)) && std::strcmp(binary_buffer, "\xF0\x9F\x98\x80") == 0;
	std::vector<unsigned char> binary_extended_string(47, 0);
	std::memcpy(binary_extended_string.data(), "bplist00", 8);
	binary_extended_string[8] = 0x5f; binary_extended_string[9] = 0x10; binary_extended_string[10] = 3;
	binary_extended_string[11] = 'A'; binary_extended_string[12] = 'B'; binary_extended_string[13] = 'C';
	binary_extended_string[14] = 8;
	const auto extended_trailer = binary_extended_string.data() + 15;
	extended_trailer[6] = 1; extended_trailer[7] = 1; extended_trailer[15] = 1; extended_trailer[31] = 14;
	const auto parsed_binary_extended = darling_windows_CFPropertyListCreateFromBinary(
		binary_extended_string.data(), static_cast<darling_windows_CFIndex>(binary_extended_string.size()));
	const bool binary_extended_ok = parsed_binary_extended != nullptr &&
		darling_windows_CFStringGetCString(static_cast<darling_windows_CFStringRef>(parsed_binary_extended),
			binary_buffer, sizeof(binary_buffer)) && std::strcmp(binary_buffer, "ABC") == 0;
	std::vector<unsigned char> binary_data_plist(45, 0);
	std::memcpy(binary_data_plist.data(), "bplist00", 8);
	binary_data_plist[8] = 0x43; binary_data_plist[9] = 1; binary_data_plist[10] = 2; binary_data_plist[11] = 3;
	binary_data_plist[12] = 8;
	const auto data_trailer = binary_data_plist.data() + 13;
	data_trailer[6] = 1; data_trailer[7] = 1; data_trailer[15] = 1; data_trailer[31] = 12;
	const auto parsed_binary_data = darling_windows_CFPropertyListCreateFromBinary(
		binary_data_plist.data(), static_cast<darling_windows_CFIndex>(binary_data_plist.size()));
	const bool binary_data_ok = parsed_binary_data != nullptr &&
		darling_windows_CFDataGetLength(static_cast<darling_windows_CFDataRef>(parsed_binary_data)) == 3 &&
		darling_windows_CFDataGetBytePtr(static_cast<darling_windows_CFDataRef>(parsed_binary_data))[2] == 3;
	std::vector<unsigned char> binary_date_plist(50, 0);
	std::memcpy(binary_date_plist.data(), "bplist00", 8);
	binary_date_plist[8] = 0x33;
	// 42.0 as an IEEE-754 double, stored big-endian by bplist.
	const unsigned char binary_date_bytes[] = {0x40, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	std::memcpy(binary_date_plist.data() + 9, binary_date_bytes, sizeof(binary_date_bytes));
	binary_date_plist[17] = 8;
	const auto binary_date_trailer = binary_date_plist.data() + 18;
	binary_date_trailer[6] = 1; binary_date_trailer[7] = 1;
	binary_date_trailer[15] = 1; binary_date_trailer[31] = 17;
	const auto parsed_binary_date = darling_windows_CFPropertyListCreateFromBinary(
		binary_date_plist.data(), static_cast<darling_windows_CFIndex>(binary_date_plist.size()));
	const bool binary_date_ok = parsed_binary_date != nullptr &&
		std::abs(darling_windows_CFDateGetAbsoluteTime(
			static_cast<darling_windows_CFDateRef>(parsed_binary_date)) - 42.0) < 1e-9;
	std::vector<unsigned char> binary_real_plist(50, 0);
	std::memcpy(binary_real_plist.data(), "bplist00", 8);
	binary_real_plist[8] = 0x23;
	const unsigned char binary_real_bytes[] = {0x40, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	std::memcpy(binary_real_plist.data() + 9, binary_real_bytes, sizeof(binary_real_bytes));
	binary_real_plist[17] = 8;
	const auto binary_real_trailer = binary_real_plist.data() + 18;
	binary_real_trailer[6] = 1; binary_real_trailer[7] = 1;
	binary_real_trailer[15] = 1; binary_real_trailer[31] = 17;
	const auto parsed_binary_real = darling_windows_CFPropertyListCreateFromBinary(
		binary_real_plist.data(), static_cast<darling_windows_CFIndex>(binary_real_plist.size()));
	double binary_real_value = 0;
	const bool binary_real_ok = parsed_binary_real != nullptr &&
		darling_windows_CFNumberGetDouble(parsed_binary_real, &binary_real_value) &&
		std::abs(binary_real_value - 42.0) < 1e-9;
	std::vector<unsigned char> binary_uid_plist(43, 0);
	std::memcpy(binary_uid_plist.data(), "bplist00", 8);
	binary_uid_plist[8] = 0x80; binary_uid_plist[9] = 7;
	binary_uid_plist[10] = 8;
	const auto binary_uid_trailer = binary_uid_plist.data() + 11;
	binary_uid_trailer[6] = 1; binary_uid_trailer[7] = 1;
	binary_uid_trailer[15] = 1; binary_uid_trailer[31] = 10;
	const auto parsed_binary_uid = darling_windows_CFPropertyListCreateFromBinary(
		binary_uid_plist.data(), static_cast<darling_windows_CFIndex>(binary_uid_plist.size()));
	std::int64_t binary_uid_value = 0;
	const bool binary_uid_ok = parsed_binary_uid != nullptr &&
		darling_windows_CFNumberGetInteger(parsed_binary_uid, &binary_uid_value) && binary_uid_value == 7;
	const auto mutable_array = darling_windows_CFArrayCreate(nullptr, 0);
	const void* mutable_array_replacement[] = {data};
	const void* mutable_array_alias_replacement = nullptr;
	const auto mutable_dictionary = darling_windows_CFDictionaryCreate(nullptr, nullptr, 0);
	const auto mutable_set = darling_windows_CFSetCreate(nullptr, 0);
	const bool mutable_array_ok = darling_windows_CFArrayAppendValue(mutable_array, string) &&
		darling_windows_CFArrayInsertValueAtIndex(mutable_array, 0, prefix) &&
		darling_windows_CFArrayGetCount(mutable_array) == 2 &&
		darling_windows_CFArrayRemoveValueAtIndex(mutable_array, 0) &&
		darling_windows_CFArrayGetCount(mutable_array) == 1 &&
		darling_windows_CFArrayReplaceValues(mutable_array, 0, 1, mutable_array_replacement, 1) &&
		darling_windows_CFArrayGetValueAtIndex(mutable_array, 0) == data &&
		darling_windows_CFArrayAppendValue(mutable_array, string) &&
		(mutable_array_alias_replacement = darling_windows_CFArrayGetValueAtIndex(mutable_array, 1), true) &&
		darling_windows_CFArrayReplaceValues(mutable_array, 0, 1,
		&mutable_array_alias_replacement, 1) &&
		darling_windows_CFArrayRemoveValues(mutable_array, 0, 2) &&
		darling_windows_CFArrayGetCount(mutable_array) == 0 &&
		darling_windows_CFArrayAppendValue(mutable_array, string) &&
		darling_windows_CFArrayAppendArray(mutable_array, mutable_array) &&
		darling_windows_CFArrayGetCount(mutable_array) == 2;
	const bool mutable_dictionary_ok = darling_windows_CFDictionarySetValue(mutable_dictionary, string, number) &&
		darling_windows_CFDictionaryContainsKey(mutable_dictionary, string) &&
		darling_windows_CFDictionarySetValue(mutable_dictionary, string, real_number) &&
		darling_windows_CFDictionaryRemoveValue(mutable_dictionary, string) &&
		darling_windows_CFDictionaryGetCount(mutable_dictionary) == 0 &&
		darling_windows_CFDictionarySetValue(mutable_dictionary, string, number) &&
		darling_windows_CFDictionaryMerge(mutable_dictionary, mutable_dictionary) &&
		darling_windows_CFDictionaryGetCount(mutable_dictionary) == 1 &&
		darling_windows_CFDictionaryGetValue(mutable_dictionary, string) == number;
	const bool mutable_set_ok = darling_windows_CFSetAddValue(mutable_set, string) &&
		!darling_windows_CFSetAddValue(mutable_set, equal_string) &&
		darling_windows_CFSetRemoveValue(mutable_set, equal_string) &&
		darling_windows_CFSetGetCount(mutable_set) == 0 &&
		darling_windows_CFSetAddValue(mutable_set, string) &&
		darling_windows_CFSetUnion(mutable_set, mutable_set) &&
		darling_windows_CFSetGetCount(mutable_set) == 1 &&
		darling_windows_CFSetIntersect(mutable_set, mutable_set) &&
		darling_windows_CFSetGetCount(mutable_set) == 1 &&
		darling_windows_CFSetSubtract(mutable_set, mutable_set) &&
		darling_windows_CFSetGetCount(mutable_set) == 0;
	const unsigned char replacement_bytes[] = {9, 8};
	const bool mutable_data_ok = darling_windows_CFDataAppendBytes(data, replacement_bytes, 2) &&
		darling_windows_CFDataReplaceBytes(data, 1, 2, replacement_bytes, 1) &&
		darling_windows_CFDataGetLength(data) == 5 &&
		darling_windows_CFDataGetBytePtr(data)[1] == 9 &&
		darling_windows_CFDataSetLength(data, 2) && darling_windows_CFDataGetLength(data) == 2 &&
		darling_windows_CFDataSetLength(data, 6) && darling_windows_CFDataGetLength(data) == 6 &&
		darling_windows_CFDataGetBytePtr(data)[5] == 0 &&
		darling_windows_CFDataGetMutableBytePtr(data)[0] == 1 &&
		darling_windows_CFDataClear(data) && darling_windows_CFDataGetLength(data) == 0 &&
		darling_windows_CFDataAppendBytes(data, replacement_bytes, 2) &&
		darling_windows_CFDataGetLength(data) == 2;
	darling_windows_CFRetain(string);
	darling_windows_CFRelease(string);
	darling_windows_CFRelease(prefix);
	darling_windows_CFRelease(suffix);
	darling_windows_CFRelease(folded_needle);
	darling_windows_CFRelease(equal_string);
	darling_windows_CFRelease(mutable_string);
	darling_windows_CFRelease(mutable_target);
	darling_windows_CFRelease(mutable_replacement);
	darling_windows_CFRelease(whitespace_string);
	darling_windows_CFRelease(array);
	darling_windows_CFRelease(equal_array);
	darling_windows_CFRelease(data);
	darling_windows_CFRelease(set);
	darling_windows_CFRelease(equal_data);
	darling_windows_CFRelease(dictionary);
	darling_windows_CFRelease(number);
	darling_windows_CFRelease(real_number);
	darling_windows_CFRelease(url_path);
	darling_windows_CFRelease(url);
	darling_windows_CFRelease(date);
	darling_windows_CFRelease(earlier_date);
	darling_windows_CFRelease(later_date);
	darling_windows_CFRelease(shifted_date);
	darling_windows_CFRelease(plist);
	darling_windows_CFRelease(array_plist);
	darling_windows_CFRelease(data_plist);
	darling_windows_CFRelease(date_plist);
	darling_windows_CFRelease(real_plist);
	darling_windows_CFRelease(parsed_binary_date);
	darling_windows_CFRelease(parsed_binary_real);
	darling_windows_CFRelease(parsed_binary_uid);
	darling_windows_CFRelease(mutable_array);
	darling_windows_CFRelease(mutable_dictionary);
	darling_windows_CFRelease(mutable_set);
	darling_windows_CFRelease(escaped_plist);
	darling_windows_CFRelease(escaped_dictionary);
	darling_windows_CFRelease(escaped_string);
	darling_windows_CFRelease(parsed);
	darling_windows_CFRelease(parsed_xml);
	darling_windows_CFRelease(parsed_dict);
	darling_windows_CFRelease(parsed_dict_xml);
	darling_windows_CFRelease(parsed_data);
	darling_windows_CFRelease(parsed_date);
	darling_windows_CFRelease(parsed_fractional_date);
	darling_windows_CFRelease(parsed_offset_date);
	darling_windows_CFRelease(parsed_binary);
	darling_windows_CFRelease(binary_dictionary_key);
	darling_windows_CFRelease(parsed_binary_dictionary);
	darling_windows_CFRelease(parsed_binary_utf16);
	darling_windows_CFRelease(parsed_binary_extended);
	darling_windows_CFRelease(parsed_binary_data);
	darling_windows_CFRelease(string);
	std::cout << "DARWIN_COREF_FOUNDATION=\"" <<
		(string_ok && string_match_ok && string_find_ok && equal_ok && semantic_lookup_ok && collection_equal_ok && array_search_ok && data_ok && data_slice_ok && array_ok && array_values_ok && number_ok && real_number_ok && number_conversion_ok && real_conversion_ok && dictionary_ok && dictionary_values_ok && set_ok && set_values_ok && date_url_ok && scalar_ok && runloop_ok && callback_ok && timer_ok && notification_ok && delivered && plist_ok && array_plist_ok && data_plist_ok && date_plist_ok && real_plist_ok && escaping_ok && plist_parse_ok && plist_dict_parse_ok && plist_data_parse_ok && plist_date_parse_ok && plist_fractional_date_ok && plist_offset_date_ok && binary_plist_ok && binary_dictionary_ok && binary_utf16_ok && binary_extended_ok && binary_data_ok && binary_date_ok && binary_real_ok && binary_uid_ok && mutable_array_ok && mutable_dictionary_ok && mutable_set_ok && mutable_data_ok ? "PASS" : "FAIL") << "\n";
	return string_ok && string_match_ok && string_find_ok && equal_ok && mutable_string_ok && string_bytes_ok && trim_ok && semantic_lookup_ok && array_search_ok && data_ok && data_slice_ok && array_ok && array_values_ok && number_ok && real_number_ok && number_conversion_ok && real_conversion_ok && number_compare_ok && dictionary_ok && dictionary_values_ok && set_ok && set_values_ok && date_url_ok && date_compare_ok && date_interval_ok && scalar_ok && runloop_ok && callback_ok && timer_ok && notification_ok && notification_global_remove_ok && delivered && plist_ok && array_plist_ok && data_plist_ok && date_plist_ok && real_plist_ok && escaping_ok && plist_parse_ok && plist_dict_parse_ok && plist_data_parse_ok && plist_date_parse_ok && plist_fractional_date_ok && plist_offset_date_ok && binary_plist_ok && binary_dictionary_ok && binary_utf16_ok && binary_extended_ok && binary_data_ok && binary_date_ok && binary_real_ok && binary_uid_ok && mutable_array_ok && mutable_dictionary_ok && mutable_set_ok && mutable_data_ok ? 0 : 1;
}
