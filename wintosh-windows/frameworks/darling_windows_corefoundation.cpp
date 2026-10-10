/* Minimal CoreFoundation-compatible primitives; GPL-3.0-only. */
#include "darling_windows_corefoundation.h"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>

namespace {
enum class Kind { String, Data, Array, Number, Real, Dictionary, Set, Date, URL, Boolean, Null };
constexpr darling_windows_CFTypeID TypeId(Kind kind)
{
	return static_cast<darling_windows_CFTypeID>(kind) + 1;
}
struct Object {
	std::atomic<std::size_t> references{1};
	bool immortal = false;
	Kind kind;
	std::string string;
	std::vector<unsigned char> data;
	std::vector<const void*> array;
	std::int64_t number = 0;
	double real_number = 0;
	std::vector<std::pair<const void*, const void*>> dictionary;
	double date = 0;
	bool boolean = false;
	~Object();
};
Object* As(const void* value) { return const_cast<Object*>(static_cast<const Object*>(value)); }
darling_windows_CFIndex UTF16Length(const std::string& value)
{
	std::size_t index = 0;
	darling_windows_CFIndex length = 0;
	while (index < value.size()) {
		const auto first = static_cast<unsigned char>(value[index]);
		std::uint32_t codepoint = 0xfffd;
		std::size_t width = 1;
		if (first < 0x80) { codepoint = first; }
		else if (first >= 0xc2 && first <= 0xdf && index + 1 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x1f) << 6) |
				(static_cast<unsigned char>(value[index + 1]) & 0x3f); width = 2;
		} else if (first >= 0xe0 && first <= 0xef && index + 2 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 2]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x0f) << 12) |
				((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 2]) & 0x3f); width = 3;
		} else if (first >= 0xf0 && first <= 0xf4 && index + 3 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 2]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 3]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x07) << 18) |
				((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(value[index + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 3]) & 0x3f); width = 4;
		}
		length += codepoint > 0xffff ? 2 : 1;
		index += width;
	}
	return length;
}
void ReleaseOwned(Object* object)
{
	if (object == nullptr || object->immortal) return;
	if (object->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete object;
}
Object::~Object()
{
	for (const auto* value : array) ReleaseOwned(As(value));
	for (const auto& entry : dictionary) {
		ReleaseOwned(As(entry.first));
		ReleaseOwned(As(entry.second));
	}
}
void RetainOwned(const void* value)
{
	if (value != nullptr) As(value)->references.fetch_add(1, std::memory_order_relaxed);
}
std::string EscapeXML(const std::string& value)
{
	std::string escaped;
	for (const char character : value) {
		switch (character) {
		case '&': escaped += "&amp;"; break;
		case '<': escaped += "&lt;"; break;
		case '>': escaped += "&gt;"; break;
		case '\"': escaped += "&quot;"; break;
		case '\'': escaped += "&apos;"; break;
		default: escaped += character; break;
		}
	}
	return escaped;
}
std::string Base64(const std::vector<unsigned char>& bytes)
{
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	for (std::size_t index = 0; index < bytes.size(); index += 3) {
		const std::size_t remaining = bytes.size() - index;
		const std::uint32_t value = (static_cast<std::uint32_t>(bytes[index]) << 16) |
			(remaining > 1 ? static_cast<std::uint32_t>(bytes[index + 1]) << 8 : 0) |
			(remaining > 2 ? bytes[index + 2] : 0);
		result += alphabet[(value >> 18) & 0x3f];
		result += alphabet[(value >> 12) & 0x3f];
		result += remaining > 1 ? alphabet[(value >> 6) & 0x3f] : '=';
		result += remaining > 2 ? alphabet[value & 0x3f] : '=';
	}
	return result;
}
std::vector<unsigned char> DecodeBase64(const std::string& input)
{
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector<unsigned char> output;
	int value = 0;
	int bits = -8;
	for (const unsigned char character : input) {
		if (character == '=') break;
		const char* found = std::strchr(alphabet, character);
		if (found == nullptr) continue;
		value = (value << 6) | static_cast<int>(found - alphabet);
		bits += 6;
		if (bits >= 0) {
			output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return output;
}
std::string DecodeUTF16BE(const unsigned char* bytes, std::size_t units)
{
	std::string output;
	for (std::size_t index = 0; index < units; ++index) {
		std::uint32_t code = static_cast<std::uint16_t>((bytes[index * 2] << 8) | bytes[index * 2 + 1]);
		if (code >= 0xd800 && code <= 0xdbff && index + 1 < units) {
			const auto low = static_cast<std::uint16_t>((bytes[(index + 1) * 2] << 8) | bytes[(index + 1) * 2 + 1]);
			if (low >= 0xdc00 && low <= 0xdfff) {
				code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00); ++index;
			}
		}
		if (code < 0x80) output.push_back(static_cast<char>(code));
		else if (code < 0x800) {
			output.push_back(static_cast<char>(0xc0 | (code >> 6)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else if (code < 0x10000) {
			output.push_back(static_cast<char>(0xe0 | (code >> 12)));
			output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else {
			output.push_back(static_cast<char>(0xf0 | (code >> 18)));
			output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		}
	}
	return output;
}
std::string PropertyListDate(double absolute_time)
{
	constexpr std::int64_t apple_epoch_unix = 978307200;
	const auto whole_seconds = static_cast<std::int64_t>(std::floor(absolute_time));
	const auto fraction = absolute_time - static_cast<double>(whole_seconds);
	auto unix_seconds = apple_epoch_unix + whole_seconds;
	auto milliseconds = static_cast<int>(std::llround(fraction * 1000.0));
	if (milliseconds >= 1000) { ++unix_seconds; milliseconds -= 1000; }
	const std::time_t timestamp = static_cast<std::time_t>(unix_seconds);
	const std::tm* utc = std::gmtime(&timestamp);
	if (utc == nullptr) return {};
	std::ostringstream result;
	result << std::put_time(utc, "%Y-%m-%dT%H:%M:%S");
	if (milliseconds != 0) result << '.' << std::setw(3) << std::setfill('0') << milliseconds;
	result << 'Z';
	return result.str();
}
void AppendXML(const Object* object, std::ostringstream& output)
{
	if (object == nullptr) { output << "<null/>"; return; }
	switch (object->kind) {
	case Kind::String: output << "<string>" << EscapeXML(object->string) << "</string>"; break;
	case Kind::URL: output << "<string>" << EscapeXML(object->string) << "</string>"; break;
	case Kind::Number: output << "<integer>" << object->number << "</integer>"; break;
	case Kind::Real: output << "<real>" << object->real_number << "</real>"; break;
	case Kind::Data: output << "<data>" << Base64(object->data) << "</data>"; break;
	case Kind::Date: output << "<date>" << PropertyListDate(object->date) << "</date>"; break;
	case Kind::Boolean: output << (object->boolean ? "<true/>" : "<false/>"); break;
	case Kind::Null: output << "<null/>"; break;
	case Kind::Array:
		output << "<array>";
		for (const auto* entry : object->array) AppendXML(static_cast<const Object*>(entry), output);
		output << "</array>";
		break;
	case Kind::Dictionary:
		output << "<dict>";
		for (const auto& entry : object->dictionary) {
			const auto* key = static_cast<const Object*>(entry.first);
			output << "<key>" << (key == nullptr ? "" : EscapeXML(key->string)) << "</key>";
			AppendXML(static_cast<const Object*>(entry.second), output);
		}
		output << "</dict>";
		break;
	default: output << "<unsupported/>"; break;
	}
}
std::string UnescapeXML(std::string value)
{
	const std::pair<const char*, const char*> entities[] = {
		{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
		{"&apos;", "'"}, {"&amp;", "&"}};
	for (const auto& entity : entities) {
		std::size_t position = 0;
		while ((position = value.find(entity.first, position)) != std::string::npos) {
			value.replace(position, std::strlen(entity.first), entity.second);
			position += std::strlen(entity.second);
		}
	}
	return value;
}
Object* ParsePropertyValue(const std::string& xml, std::size_t& position)
{
	while (position < xml.size() && xml[position] != '<') ++position;
	if (position >= xml.size()) return nullptr;
	const auto close = xml.find('>', position);
	if (close == std::string::npos) return nullptr;
	const std::string tag = xml.substr(position + 1, close - position - 1);
	position = close + 1;
	if (tag == "true/") { auto* object = new Object{}; object->kind = Kind::Boolean; object->boolean = true; return object; }
	if (tag == "false/") { auto* object = new Object{}; object->kind = Kind::Boolean; return object; }
	if (tag == "null/") { auto* object = new Object{}; object->kind = Kind::Null; return object; }
	if (tag == "array") {
		auto* object = new Object{}; object->kind = Kind::Array;
		while (position < xml.size() && xml.compare(position, 8, "</array>") != 0) {
			auto* child = ParsePropertyValue(xml, position);
			if (child == nullptr) { delete object; return nullptr; }
			object->array.push_back(child);
		}
		if (xml.compare(position, 8, "</array>") != 0) { delete object; return nullptr; }
		position += 8; return object;
	}
	if (tag == "dict") {
		auto* object = new Object{}; object->kind = Kind::Dictionary;
		while (position < xml.size() && xml.compare(position, 7, "</dict>") != 0) {
			while (position < xml.size() && xml[position] != '<') ++position;
			if (xml.compare(position, 5, "<key>") != 0) { delete object; return nullptr; }
			position += 5;
			const auto key_end = xml.find("</key>", position);
			if (key_end == std::string::npos) { delete object; return nullptr; }
			auto* key = new Object{}; key->kind = Kind::String;
			key->string = UnescapeXML(xml.substr(position, key_end - position));
			position = key_end + 6;
			auto* child = ParsePropertyValue(xml, position);
			if (child == nullptr) { delete key; delete object; return nullptr; }
			object->dictionary.emplace_back(key, child);
		}
		if (xml.compare(position, 7, "</dict>") != 0) { delete object; return nullptr; }
		position += 7; return object;
	}
	const std::string end_tag = "</" + tag + ">";
	const auto end = xml.find(end_tag, position);
	if (end == std::string::npos) return nullptr;
	const auto text = UnescapeXML(xml.substr(position, end - position));
	position = end + end_tag.size();
	auto* object = new Object{};
	try {
		if (tag == "string") { object->kind = Kind::String; object->string = text; }
		else if (tag == "integer") { object->kind = Kind::Number; object->number = std::stoll(text); }
		else if (tag == "real") { object->kind = Kind::Real; object->real_number = std::stod(text); }
		else if (tag == "data") { object->kind = Kind::Data; object->data = DecodeBase64(text); }
		else if (tag == "date") {
			std::tm calendar{};
			std::istringstream input(text);
			input >> std::get_time(&calendar, "%Y-%m-%dT%H:%M:%S");
			if (input.fail()) { delete object; return nullptr; }
			auto unix_seconds = static_cast<std::int64_t>(_mkgmtime(&calendar));
			if (text.size() > 19 && text[19] != 'Z') {
				const auto sign = text[19] == '-' ? -1 : text[19] == '+' ? 1 : 0;
				if (sign != 0 && text.size() >= 25 && text[22] == ':') {
					const auto hours = std::stoi(text.substr(20, 2));
					const auto minutes = std::stoi(text.substr(23, 2));
					unix_seconds -= sign * (hours * 3600 + minutes * 60);
				}
			}
			object->kind = Kind::Date;
			double fraction = 0.0;
			const auto decimal = text.find('.', 19);
			if (decimal != std::string::npos) {
				const auto end = text.find('Z', decimal);
				const auto digits = text.substr(decimal + 1, end == std::string::npos ? std::string::npos : end - decimal - 1);
				if (!digits.empty()) fraction = std::stod("0." + digits);
			}
			object->date = static_cast<double>(unix_seconds - 978307200) + fraction;
		}
		else { delete object; return nullptr; }
	} catch (...) { delete object; return nullptr; }
	return object;
}
struct BinaryPlistContext {
	const unsigned char* bytes;
	std::size_t length;
	std::size_t offset_size;
	std::size_t reference_size;
	std::size_t object_count;
	std::size_t object_table;
	std::size_t offsets_table;
};
std::uint64_t ReadBigEndian(const unsigned char* bytes, std::size_t count)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < count; ++index) value = (value << 8) | bytes[index];
	return value;
}
bool ReadBinaryLength(const BinaryPlistContext& context, std::size_t offset, unsigned char info,
	std::size_t& length, std::size_t& payload)
{
	if (info < 0x0f) { length = info; payload = offset + 1; return payload <= context.length; }
	if (offset + 2 > context.length || (context.bytes[offset + 1] >> 4) != 0x1) return false;
	const auto width = std::size_t{1} << (context.bytes[offset + 1] & 0x0f);
	if (width == 0 || width > sizeof(std::size_t) || offset + 2 + width > context.length) return false;
	const auto encoded = ReadBigEndian(context.bytes + offset + 2, width);
	if (encoded > context.length) return false;
	length = static_cast<std::size_t>(encoded);
	payload = offset + 2 + width;
	return payload <= context.length;
}
Object* ParseBinaryObject(const BinaryPlistContext& context, std::size_t index,
	std::vector<bool>& active)
{
	if (index >= context.object_count || active[index]) return nullptr;
	const auto offset_position = context.offsets_table + index * context.offset_size;
	if (offset_position + context.offset_size > context.length) return nullptr;
	const auto offset = ReadBigEndian(context.bytes + offset_position, context.offset_size);
	if (offset >= context.length) return nullptr;
	const auto descriptor = context.bytes[offset];
	const auto type = descriptor >> 4;
	const auto info = descriptor & 0x0f;
	if (type == 0x0) {
		auto* object = new Object{};
		if (info == 0x0) object->kind = Kind::Null;
		else if (info == 0x8 || info == 0x9) { object->kind = Kind::Boolean; object->boolean = info == 0x9; }
		else { delete object; return nullptr; }
		return object;
	}
	if (type == 0x1) {
		const auto width = std::size_t{1} << info;
		if (width == 0 || width > 8 || offset + 1 + width > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::Number;
		object->number = static_cast<std::int64_t>(ReadBigEndian(context.bytes + offset + 1, width));
		return object;
	}
	if (type == 0x2) {
		const auto width = std::size_t{1} << info;
		if ((width != 4 && width != 8) || offset + 1 + width > context.length) return nullptr;
		const auto bits = ReadBigEndian(context.bytes + offset + 1, width);
		auto* object = new Object{}; object->kind = Kind::Real;
		if (width == 4) {
			const auto narrow_bits = static_cast<std::uint32_t>(bits);
			float narrow = 0;
			std::memcpy(&narrow, &narrow_bits, sizeof(narrow));
			object->real_number = narrow;
		} else {
			std::uint64_t host_bits = bits;
			double value = 0;
			std::memcpy(&value, &host_bits, sizeof(value));
			object->real_number = value;
		}
		return object;
	}
	if (type == 0x3 && offset + 9 <= context.length) {
		// Binary plists store an IEEE-754 double in big-endian byte order.
		// ReadBigEndian yields the host integer bit pattern which can then be
		// copied into the little-endian Windows double representation.
		const std::uint64_t bits = ReadBigEndian(context.bytes + offset + 1, 8);
		double value = 0;
		std::memcpy(&value, &bits, sizeof(value));
		auto* object = new Object{}; object->kind = Kind::Date; object->date = value;
		return object;
	}
	if (type == 0x8) {
		const auto width = static_cast<std::size_t>(info) + 1;
		if (width > 8 || offset + 1 + width > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::Number;
		object->number = static_cast<std::int64_t>(ReadBigEndian(context.bytes + offset + 1, width));
		return object;
	}
	if (type == 0x4) {
		std::size_t length = 0;
		std::size_t payload = 0;
		if (!ReadBinaryLength(context, offset, info, length, payload) || payload + length > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::Data;
		object->data.assign(context.bytes + payload, context.bytes + payload + length);
		return object;
	}
	std::size_t length = 0;
	std::size_t payload = 0;
	if (type == 0x5 && ReadBinaryLength(context, offset, info, length, payload) &&
		payload + length <= context.length) {
		auto* object = new Object{}; object->kind = Kind::String;
		object->string.assign(reinterpret_cast<const char*>(context.bytes + payload), length);
		return object;
	}
	if (type == 0x6 && ReadBinaryLength(context, offset, info, length, payload) &&
		length <= (context.length - payload) / 2) {
		auto* object = new Object{}; object->kind = Kind::String;
		object->string = DecodeUTF16BE(context.bytes + payload, length);
		return object;
	}
	if (type == 0xa) {
		std::size_t count = 0;
		std::size_t refs_start = 0;
		if (!ReadBinaryLength(context, offset, info, count, refs_start) ||
			count > (context.length - refs_start) / context.reference_size) return nullptr;
		active[index] = true;
		auto* object = new Object{}; object->kind = Kind::Array;
		for (std::size_t child = 0; child < count; ++child) {
			const auto ref = ReadBigEndian(context.bytes + refs_start + child * context.reference_size, context.reference_size);
			auto* value = ParseBinaryObject(context, static_cast<std::size_t>(ref), active);
			if (value == nullptr) { delete object; active[index] = false; return nullptr; }
			object->array.push_back(value);
		}
		active[index] = false;
		return object;
	}
	if (type == 0xd) {
		std::size_t count = 0;
		std::size_t refs_start = 0;
		if (!ReadBinaryLength(context, offset, info, count, refs_start) || count >
			(context.length - refs_start) / (context.reference_size * 2)) return nullptr;
		const auto refs_count = count * 2;
		if (refs_start + refs_count * context.reference_size > context.length) return nullptr;
		active[index] = true;
		auto* object = new Object{}; object->kind = Kind::Dictionary;
		for (std::size_t entry = 0; entry < count; ++entry) {
			const auto key_ref = ReadBigEndian(context.bytes + refs_start + entry * context.reference_size, context.reference_size);
			const auto value_ref = ReadBigEndian(context.bytes + refs_start + (entry + count) * context.reference_size, context.reference_size);
			auto* key = ParseBinaryObject(context, static_cast<std::size_t>(key_ref), active);
			auto* value = ParseBinaryObject(context, static_cast<std::size_t>(value_ref), active);
			if (key == nullptr || value == nullptr) { delete key; delete value; delete object; active[index] = false; return nullptr; }
			object->dictionary.emplace_back(key, value);
		}
		active[index] = false;
		return object;
	}
	return nullptr;
}
struct RunLoopState {
	std::mutex mutex;
	std::condition_variable condition;
	bool running = false;
	bool stopped = false;
	std::vector<std::pair<darling_windows_CFRunLoopBlock, void*>> blocks;
};
struct RunLoop {
	std::shared_ptr<RunLoopState> state = std::make_shared<RunLoopState>();
};
// CoreFoundation associates the current run loop with the calling thread.
// Keeping this object thread-local also prevents callbacks posted by one
// thread from being consumed by an unrelated thread's run loop.
thread_local RunLoop current_run_loop;
struct NotificationObserver {
	const void* observer;
	darling_windows_CFNotificationCallback callback;
};
std::mutex notification_mutex;
std::unordered_map<std::string, std::vector<NotificationObserver>> notifications;
constexpr const char* notification_wildcard = "\x01wildcard";
}

extern "C" darling_windows_CFRange darling_windows_CFRangeMake(
	darling_windows_CFIndex location, darling_windows_CFIndex length)
{
	return {location, length};
}

extern "C" darling_windows_CFIndex darling_windows_CFRangeGetMax(
	darling_windows_CFRange range)
{
	const auto maximum = (std::numeric_limits<darling_windows_CFIndex>::max)();
	const auto minimum = (std::numeric_limits<darling_windows_CFIndex>::min)();
	if (range.length > 0 && range.location > maximum - range.length)
		return maximum;
	if (range.length < 0 && range.location < minimum - range.length)
		return minimum;
	return range.location + range.length;
}

extern "C" void* darling_windows_CFRetain(const void* value)
{
	RetainOwned(value);
	return const_cast<void*>(value);
}

extern "C" void darling_windows_CFRelease(const void* value)
{
	ReleaseOwned(As(value));
}

extern "C" darling_windows_CFTypeID darling_windows_CFGetTypeID(const void* value)
{
	const auto* object = static_cast<const Object*>(value);
	return object == nullptr ? 0 : TypeId(object->kind);
}

extern "C" std::uint64_t darling_windows_CFHash(const void* value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr) return 0;
	const auto seed = static_cast<std::uint64_t>(TypeId(object->kind)) * 0x9e3779b97f4a7c15ULL;
	const auto mix = [seed](std::uint64_t hash) {
		hash += seed + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
		return hash;
	};
	switch (object->kind) {
	case Kind::String:
	case Kind::URL: return mix(static_cast<std::uint64_t>(std::hash<std::string>{}(object->string)));
	case Kind::Data: return mix(static_cast<std::uint64_t>(std::hash<std::string_view>{}(
		std::string_view(reinterpret_cast<const char*>(object->data.data()), object->data.size()))));
	case Kind::Number: return mix(static_cast<std::uint64_t>(std::hash<std::int64_t>{}(object->number)));
	case Kind::Real:
		return mix(static_cast<std::uint64_t>(std::hash<double>{}(object->real_number)));
	case Kind::Date: return mix(static_cast<std::uint64_t>(std::hash<double>{}(object->date)));
	case Kind::Boolean: return mix(object->boolean ? 1 : 0);
	case Kind::Null: return mix(0);
	default: return mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value)));
	}
}

extern "C" bool darling_windows_CFEqual(const void* left, const void* right)
{
	if (left == right) return true;
	if (left == nullptr || right == nullptr) return false;
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	const bool lhs_number = lhs->kind == Kind::Number || lhs->kind == Kind::Real;
	const bool rhs_number = rhs->kind == Kind::Number || rhs->kind == Kind::Real;
	if (lhs_number && rhs_number) {
		const auto left_value = lhs->kind == Kind::Number ? static_cast<long double>(lhs->number) : lhs->real_number;
		const auto right_value = rhs->kind == Kind::Number ? static_cast<long double>(rhs->number) : rhs->real_number;
		return left_value == right_value;
	}
	if (lhs->kind != rhs->kind) return false;
	switch (lhs->kind) {
	case Kind::String:
	case Kind::URL: return lhs->string == rhs->string;
	case Kind::Data: return lhs->data == rhs->data;
	case Kind::Number: return lhs->number == rhs->number;
	case Kind::Real: return lhs->real_number == rhs->real_number;
	case Kind::Date: return lhs->date == rhs->date;
	case Kind::Boolean: return lhs->boolean == rhs->boolean;
	case Kind::Null: return true;
	case Kind::Array:
		if (lhs->array.size() != rhs->array.size()) return false;
		for (std::size_t index = 0; index < lhs->array.size(); ++index)
			if (!darling_windows_CFEqual(lhs->array[index], rhs->array[index])) return false;
		return true;
	case Kind::Set:
		if (lhs->array.size() != rhs->array.size()) return false;
		for (const auto* left_value : lhs->array) {
			bool found = false;
			for (const auto* right_value : rhs->array)
				if (darling_windows_CFEqual(left_value, right_value)) { found = true; break; }
			if (!found) return false;
		}
		return true;
	case Kind::Dictionary:
		if (lhs->dictionary.size() != rhs->dictionary.size()) return false;
		for (const auto& left_entry : lhs->dictionary) {
			bool found = false;
			for (const auto& right_entry : rhs->dictionary)
				if (darling_windows_CFEqual(left_entry.first, right_entry.first) &&
					darling_windows_CFEqual(left_entry.second, right_entry.second)) {
					found = true; break;
				}
			if (!found) return false;
		}
		return true;
	default: return false;
	}
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCString(const char* value)
{
	if (value == nullptr) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::String;
	object->string = value;
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetLength(darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::String ?
		UTF16Length(object->string) : 0;
}

extern "C" bool darling_windows_CFStringGetCString(darling_windows_CFStringRef value,
	char* buffer, darling_windows_CFIndex capacity)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || buffer == nullptr || capacity <= 0 ||
		static_cast<darling_windows_CFIndex>(object->string.size() + 1) > capacity) return false;
	std::memcpy(buffer, object->string.c_str(), object->string.size() + 1);
	return true;
}

extern "C" const char* darling_windows_CFStringGetCStringPtr(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::String ? object->string.c_str() : nullptr;
}

extern "C" bool darling_windows_CFStringHasPrefix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef prefix)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(prefix);
	return object != nullptr && candidate != nullptr && object->kind == Kind::String &&
		candidate->kind == Kind::String && object->string.rfind(candidate->string, 0) == 0;
}

extern "C" bool darling_windows_CFStringHasSuffix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef suffix)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(suffix);
	return object != nullptr && candidate != nullptr && object->kind == Kind::String &&
		candidate->kind == Kind::String && object->string.size() >= candidate->string.size() &&
		object->string.compare(object->string.size() - candidate->string.size(),
			candidate->string.size(), candidate->string) == 0;
}

extern "C" int darling_windows_CFStringCompare(darling_windows_CFStringRef left,
	darling_windows_CFStringRef right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr || lhs->kind != Kind::String ||
		rhs->kind != Kind::String) return 0;
	const int comparison = lhs->string.compare(rhs->string);
	return comparison < 0 ? -1 : comparison > 0 ? 1 : 0;
}

extern "C" darling_windows_CFRange darling_windows_CFStringFind(
	darling_windows_CFStringRef value, darling_windows_CFStringRef needle, int flags)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(needle);
	if (object == nullptr || candidate == nullptr || object->kind != Kind::String || candidate->kind != Kind::String)
		return { -1, 0 };
	if ((flags & darling_windows_CFCompareCaseInsensitive) == 0) {
		const auto position = object->string.find(candidate->string);
		return position == std::string::npos ? darling_windows_CFRange{ -1, 0 } : darling_windows_CFRange{
			static_cast<darling_windows_CFIndex>(position), static_cast<darling_windows_CFIndex>(candidate->string.size()) };
	}
	if (candidate->string.empty()) return { 0, 0 };
	for (std::size_t position = 0; position + candidate->string.size() <= object->string.size(); ++position) {
		bool match = true;
		for (std::size_t offset = 0; offset < candidate->string.size(); ++offset) {
			const auto lhs = static_cast<unsigned char>(object->string[position + offset]);
			const auto rhs = static_cast<unsigned char>(candidate->string[offset]);
			if (std::tolower(lhs) != std::tolower(rhs)) { match = false; break; }
		}
		if (match) return { static_cast<darling_windows_CFIndex>(position),
			static_cast<darling_windows_CFIndex>(candidate->string.size()) };
	}
	return { -1, 0 };
}

extern "C" bool darling_windows_CFStringAppendCString(darling_windows_CFStringRef value,
	const char* suffix)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String || suffix == nullptr) return false;
	object->string += suffix; return true;
}

extern "C" bool darling_windows_CFStringReplaceAll(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement)
{
	auto* object = As(value);
	const auto* needle = static_cast<const Object*>(target);
	const auto* substitute = static_cast<const Object*>(replacement);
	if (object == nullptr || object->kind != Kind::String || needle == nullptr ||
		needle->kind != Kind::String || needle->string.empty() || substitute == nullptr ||
		substitute->kind != Kind::String) return false;
	std::size_t position = 0;
	while ((position = object->string.find(needle->string, position)) != std::string::npos) {
		object->string.replace(position, needle->string.size(), substitute->string);
		position += substitute->string.size();
	}
	return true;
}

extern "C" bool darling_windows_CFStringReplaceRange(darling_windows_CFStringRef value,
	darling_windows_CFIndex location, darling_windows_CFIndex length, const char* replacement)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String || location < 0 || length < 0 || replacement == nullptr ||
		static_cast<std::size_t>(location) > object->string.size() ||
		static_cast<std::size_t>(length) > object->string.size() - static_cast<std::size_t>(location)) return false;
	object->string.replace(static_cast<std::size_t>(location), static_cast<std::size_t>(length), replacement);
	return true;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetBytes(
	darling_windows_CFStringRef value, darling_windows_CFIndex location,
	darling_windows_CFIndex length, char* output, darling_windows_CFIndex capacity)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || location < 0 || length < 0 || capacity < 0 ||
		(static_cast<std::size_t>(location) > object->string.size()) ||
		static_cast<std::size_t>(length) > object->string.size() - static_cast<std::size_t>(location) ||
		(static_cast<std::size_t>(capacity) != 0 && output == nullptr)) return -1;
	const auto copied = std::min<std::size_t>(static_cast<std::size_t>(length), static_cast<std::size_t>(capacity));
	if (copied != 0) std::memcpy(output, object->string.data() + location, copied);
	return static_cast<darling_windows_CFIndex>(copied);
}

extern "C" bool darling_windows_CFStringTrimWhitespace(darling_windows_CFStringRef value)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String) return false;
	const auto decode = [](const std::string& input, std::size_t offset, std::uint32_t& codepoint) {
		const auto first = static_cast<unsigned char>(input[offset]);
		if (first < 0x80) { codepoint = first; return std::size_t{1}; }
		if (first >= 0xc2 && first <= 0xdf && offset + 1 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(input[offset + 1]) & 0x3f); return std::size_t{2};
		}
		if (first >= 0xe0 && first <= 0xef && offset + 2 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 2]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x0f) << 12) |
				((static_cast<unsigned char>(input[offset + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(input[offset + 2]) & 0x3f); return std::size_t{3};
		}
		if (first >= 0xf0 && first <= 0xf4 && offset + 3 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 2]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 3]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x07) << 18) |
				((static_cast<unsigned char>(input[offset + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(input[offset + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(input[offset + 3]) & 0x3f); return std::size_t{4};
		}
		codepoint = first; return std::size_t{1};
	};
	const auto is_space = [](std::uint32_t codepoint) {
		return (codepoint >= 0x0009 && codepoint <= 0x000d) || codepoint == 0x0020 ||
			codepoint == 0x0085 || codepoint == 0x00a0 || codepoint == 0x1680 ||
			(codepoint >= 0x2000 && codepoint <= 0x200a) || codepoint == 0x2028 ||
			codepoint == 0x2029 || codepoint == 0x202f || codepoint == 0x205f ||
			codepoint == 0x3000;
	};
	std::size_t first = 0;
	while (first < object->string.size()) {
		std::uint32_t codepoint = 0;
		const auto width = decode(object->string, first, codepoint);
		if (!is_space(codepoint)) break;
		first += width;
	}
	std::size_t last = object->string.size();
	while (last > first) {
		std::size_t start = last - 1;
		while (start > first && (static_cast<unsigned char>(object->string[start]) & 0xc0) == 0x80) --start;
		std::uint32_t codepoint = 0;
		const auto width = decode(object->string, start, codepoint);
		if (start + width != last || !is_space(codepoint)) break;
		last = start;
	}
	object->string = object->string.substr(first, last - first);
	return true;
}

extern "C" darling_windows_CFDataRef darling_windows_CFDataCreate(const void* bytes,
	darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Data;
	if (length != 0) {
		const auto* begin = static_cast<const unsigned char*>(bytes);
		object->data.assign(begin, begin + length);
	}
	return object;
}

extern "C" const unsigned char* darling_windows_CFDataGetBytePtr(darling_windows_CFDataRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Data && !object->data.empty() ?
		object->data.data() : nullptr;
}

extern "C" unsigned char* darling_windows_CFDataGetMutableBytePtr(darling_windows_CFDataRef value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::Data && !object->data.empty() ?
		object->data.data() : nullptr;
}

extern "C" darling_windows_CFIndex darling_windows_CFDataGetLength(darling_windows_CFDataRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Data ?
		static_cast<darling_windows_CFIndex>(object->data.size()) : 0;
}

extern "C" bool darling_windows_CFDataGetBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	unsigned char* output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Data || range_start < 0 ||
		range_count < 0 || (range_count != 0 && output == nullptr) ||
		static_cast<std::size_t>(range_start) > object->data.size() ||
		static_cast<std::size_t>(range_count) > object->data.size() -
			static_cast<std::size_t>(range_start)) return false;
	if (range_count != 0) std::memmove(output, object->data.data() + range_start,
		static_cast<std::size_t>(range_count));
	return true;
}

extern "C" bool darling_windows_CFDataAppendBytes(darling_windows_CFDataRef value,
	const void* bytes, darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || length < 0 ||
		(length != 0 && bytes == nullptr)) return false;
	const auto* input = static_cast<const unsigned char*>(bytes);
	std::vector<unsigned char> copy;
	if (length != 0) copy.assign(input, input + length);
	object->data.insert(object->data.end(), copy.begin(), copy.end()); return true;
}

extern "C" bool darling_windows_CFDataReplaceBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* bytes, darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || range_start < 0 || range_count < 0 || length < 0 ||
		(length != 0 && bytes == nullptr) || static_cast<std::size_t>(range_start) > object->data.size() ||
		static_cast<std::size_t>(range_count) > object->data.size() - static_cast<std::size_t>(range_start)) return false;
	const auto* input = static_cast<const unsigned char*>(bytes);
	std::vector<unsigned char> copy;
	if (length != 0) copy.assign(input, input + length);
	const auto begin = object->data.begin() + range_start;
	object->data.erase(begin, begin + range_count);
	object->data.insert(object->data.begin() + range_start, copy.begin(), copy.end()); return true;
}

extern "C" bool darling_windows_CFDataSetLength(darling_windows_CFDataRef value,
	darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || length < 0) return false;
	object->data.resize(static_cast<std::size_t>(length)); return true;
}

extern "C" bool darling_windows_CFDataClear(darling_windows_CFDataRef value)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data) return false;
	object->data.clear(); return true;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreate(const void* const* values,
	darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && values == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Array;
	if (count != 0) {
		object->array.assign(values, values + count);
		for (const auto* value : object->array) RetainOwned(value);
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCount(darling_windows_CFArrayRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Array ?
		static_cast<darling_windows_CFIndex>(object->array.size()) : 0;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array || index < 0 ||
		static_cast<std::size_t>(index) >= object->array.size()) return nullptr;
	return object->array[static_cast<std::size_t>(index)];
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return -1;
	for (std::size_t index = 0; index < object->array.size(); ++index)
		if (darling_windows_CFEqual(object->array[index], candidate))
			return static_cast<darling_windows_CFIndex>(index);
	return -1;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return 0;
	darling_windows_CFIndex count = 0;
	for (const auto* entry : object->array)
		if (darling_windows_CFEqual(entry, candidate)) ++count;
	return count;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return -1;
	for (std::size_t index = object->array.size(); index > 0; --index)
		if (darling_windows_CFEqual(object->array[index - 1], candidate))
			return static_cast<darling_windows_CFIndex>(index - 1);
	return -1;
}

extern "C" void darling_windows_CFArrayGetValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count, const void** output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array ||
		range_start < 0 || range_count < 0 ||
		(range_count != 0 && output == nullptr) ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() -
			static_cast<std::size_t>(range_start)) return;
	if (range_count != 0) std::memmove(output, object->array.data() + range_start,
		static_cast<std::size_t>(range_count) * sizeof(const void*));
}

extern "C" bool darling_windows_CFArrayAppendValue(darling_windows_CFArrayRef value, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || element == nullptr) return false;
	object->array.push_back(element); RetainOwned(element); return true;
}

extern "C" bool darling_windows_CFArrayInsertValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || element == nullptr || index < 0 ||
		static_cast<std::size_t>(index) > object->array.size()) return false;
	object->array.insert(object->array.begin() + index, element); RetainOwned(element); return true;
}

extern "C" bool darling_windows_CFArrayRemoveValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || index < 0 ||
		static_cast<std::size_t>(index) >= object->array.size()) return false;
	const auto it = object->array.begin() + index; ReleaseOwned(As(*it)); object->array.erase(it); return true;
}

extern "C" bool darling_windows_CFArrayReplaceValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* const* replacement, darling_windows_CFIndex replacement_count)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || range_start < 0 || range_count < 0 ||
		replacement_count < 0 || (replacement_count != 0 && replacement == nullptr) ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() - static_cast<std::size_t>(range_start)) return false;
	for (darling_windows_CFIndex index = 0; index < replacement_count; ++index)
		if (replacement[index] == nullptr) return false;
	std::vector<const void*> replacement_values(replacement, replacement + replacement_count);
	const auto begin = object->array.begin() + range_start;
	for (darling_windows_CFIndex index = 0; index < range_count; ++index) ReleaseOwned(As(object->array[static_cast<std::size_t>(range_start + index)]));
	object->array.erase(begin, begin + range_count);
	object->array.insert(object->array.begin() + range_start,
		replacement_values.begin(), replacement_values.end());
	for (darling_windows_CFIndex index = 0; index < replacement_count; ++index) RetainOwned(replacement[index]);
	return true;
}

extern "C" bool darling_windows_CFArrayRemoveValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || range_start < 0 || range_count < 0 ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() - static_cast<std::size_t>(range_start)) return false;
	for (darling_windows_CFIndex index = 0; index < range_count; ++index)
		ReleaseOwned(As(object->array[static_cast<std::size_t>(range_start + index)]));
	object->array.erase(object->array.begin() + range_start, object->array.begin() + range_start + range_count);
	return true;
}

extern "C" bool darling_windows_CFArrayAppendArray(darling_windows_CFArrayRef value,
	darling_windows_CFArrayRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Array || source_object == nullptr ||
		source_object->kind != Kind::Array) return false;
	const auto source_values = source_object->array;
	object->array.insert(object->array.end(), source_values.begin(), source_values.end());
	for (const auto* element : source_values) RetainOwned(element);
	return true;
}

extern "C" void darling_windows_CFArrayRemoveAllValues(darling_windows_CFArrayRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Array) return;
	for (const auto* element : object->array) ReleaseOwned(As(element)); object->array.clear();
}

extern "C" const void* darling_windows_CFNumberCreateInteger(std::int64_t value)
{
	auto* object = new Object{};
	object->kind = Kind::Number;
	object->number = value;
	return object;
}

extern "C" bool darling_windows_CFNumberGetInteger(const void* value, std::int64_t* result)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || result == nullptr ||
		(object->kind != Kind::Number && object->kind != Kind::Real)) return false;
	*result = object->kind == Kind::Number ? object->number : static_cast<std::int64_t>(object->real_number);
	return true;
}

extern "C" const void* darling_windows_CFNumberCreateDouble(double value)
{
	auto* object = new Object{};
	object->kind = Kind::Real;
	object->real_number = value;
	return object;
}

extern "C" bool darling_windows_CFNumberGetDouble(const void* value, double* result)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || result == nullptr ||
		(object->kind != Kind::Real && object->kind != Kind::Number)) return false;
	*result = object->kind == Kind::Real ? object->real_number : static_cast<double>(object->number);
	return true;
}

extern "C" int darling_windows_CFNumberCompare(const void* left, const void* right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr ||
		(lhs->kind != Kind::Number && lhs->kind != Kind::Real) ||
		(rhs->kind != Kind::Number && rhs->kind != Kind::Real)) return 0;
	const auto lhs_value = lhs->kind == Kind::Number ? static_cast<double>(lhs->number) : lhs->real_number;
	const auto rhs_value = rhs->kind == Kind::Number ? static_cast<double>(rhs->number) : rhs->real_number;
	return lhs_value < rhs_value ? -1 : lhs_value > rhs_value ? 1 : 0;
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreate(
	const void* const* keys, const void* const* values, darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && (keys == nullptr || values == nullptr))) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Dictionary;
	for (darling_windows_CFIndex index = 0; index < count; ++index) {
		const auto found = std::find_if(object->dictionary.begin(), object->dictionary.end(),
			[&](const auto& entry) { return darling_windows_CFEqual(entry.first, keys[index]); });
		if (found != object->dictionary.end()) {
			RetainOwned(values[index]);
			ReleaseOwned(As(found->second));
			found->second = values[index];
			continue;
		}
		object->dictionary.emplace_back(keys[index], values[index]);
		RetainOwned(keys[index]);
		RetainOwned(values[index]);
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Dictionary ?
		static_cast<darling_windows_CFIndex>(object->dictionary.size()) : 0;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, const void* key)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return nullptr;
	for (const auto& entry : object->dictionary)
		if (darling_windows_CFEqual(entry.first, key)) return entry.second;
	return nullptr;
}

extern "C" bool darling_windows_CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, const void* key)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return false;
	for (const auto& entry : object->dictionary)
		if (darling_windows_CFEqual(entry.first, key)) return true;
	return false;
}

extern "C" void darling_windows_CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return;
	for (std::size_t index = 0; index < object->dictionary.size(); ++index) {
		if (keys != nullptr) keys[index] = object->dictionary[index].first;
		if (values != nullptr) values[index] = object->dictionary[index].second;
	}
}

extern "C" bool darling_windows_CFDictionarySetValue(darling_windows_CFDictionaryRef value,
	const void* key, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Dictionary || key == nullptr || element == nullptr) return false;
	for (auto& entry : object->dictionary) if (darling_windows_CFEqual(entry.first, key)) {
		RetainOwned(element); ReleaseOwned(As(entry.second)); entry.second = element; return true;
	}
	RetainOwned(key); RetainOwned(element); object->dictionary.emplace_back(key, element); return true;
}

extern "C" bool darling_windows_CFDictionaryRemoveValue(darling_windows_CFDictionaryRef value, const void* key)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Dictionary || key == nullptr) return false;
	for (auto it = object->dictionary.begin(); it != object->dictionary.end(); ++it) if (darling_windows_CFEqual(it->first, key)) {
		ReleaseOwned(As(it->first)); ReleaseOwned(As(it->second)); object->dictionary.erase(it); return true;
	}
	return false;
}

extern "C" void darling_windows_CFDictionaryRemoveAllValues(darling_windows_CFDictionaryRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Dictionary) return;
	for (const auto& entry : object->dictionary) { ReleaseOwned(As(entry.first)); ReleaseOwned(As(entry.second)); }
	object->dictionary.clear();
}

extern "C" bool darling_windows_CFDictionaryMerge(darling_windows_CFDictionaryRef value,
	darling_windows_CFDictionaryRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Dictionary || source_object == nullptr ||
		source_object->kind != Kind::Dictionary) return false;
	const auto source_entries = source_object->dictionary;
	for (const auto& source_entry : source_entries) {
		bool replaced = false;
		for (auto& entry : object->dictionary) if (darling_windows_CFEqual(entry.first, source_entry.first)) {
			RetainOwned(source_entry.second); ReleaseOwned(As(entry.second)); entry.second = source_entry.second;
			replaced = true; break;
		}
		if (!replaced) {
			RetainOwned(source_entry.first); RetainOwned(source_entry.second);
			object->dictionary.push_back(source_entry);
		}
	}
	return true;
}

extern "C" darling_windows_CFSetRef darling_windows_CFSetCreate(
	const void* const* values, darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && values == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Set;
	for (darling_windows_CFIndex index = 0; index < count; ++index) {
		const auto duplicate = std::find_if(object->array.begin(), object->array.end(),
			[&](const void* existing) { return darling_windows_CFEqual(existing, values[index]); });
		if (duplicate == object->array.end()) {
			object->array.push_back(values[index]);
			RetainOwned(values[index]);
		}
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFSetGetCount(darling_windows_CFSetRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Set ?
		static_cast<darling_windows_CFIndex>(object->array.size()) : 0;
}

extern "C" bool darling_windows_CFSetContainsValue(darling_windows_CFSetRef value,
	const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set) return false;
	for (const auto* entry : object->array)
		if (darling_windows_CFEqual(entry, candidate)) return true;
	return false;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFSetGetValue(
	darling_windows_CFSetRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set) return nullptr;
	for (const auto* entry : object->array)
		if (darling_windows_CFEqual(entry, candidate)) return entry;
	return nullptr;
}

extern "C" void darling_windows_CFSetGetValues(darling_windows_CFSetRef value,
	const void** output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set || output == nullptr) return;
	if (!object->array.empty()) std::memmove(output, object->array.data(),
		object->array.size() * sizeof(const void*));
}

extern "C" bool darling_windows_CFSetAddValue(darling_windows_CFSetRef value, const void* element)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set || element == nullptr) return false;
	for (const auto* entry : object->array) if (darling_windows_CFEqual(entry, element)) return false;
	object->array.push_back(element); RetainOwned(element); return true;
}

extern "C" bool darling_windows_CFSetRemoveValue(darling_windows_CFSetRef value, const void* element)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set || element == nullptr) return false;
	for (auto it = object->array.begin(); it != object->array.end(); ++it) if (darling_windows_CFEqual(*it, element)) {
		ReleaseOwned(As(*it)); object->array.erase(it); return true;
	}
	return false;
}

extern "C" void darling_windows_CFSetRemoveAllValues(darling_windows_CFSetRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set) return;
	for (const auto* element : object->array) ReleaseOwned(As(element)); object->array.clear();
}

extern "C" bool darling_windows_CFSetUnion(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	const auto source_values = source_object->array;
	for (const auto* candidate : source_values) {
		bool found = false;
		for (const auto* existing : object->array)
			if (darling_windows_CFEqual(existing, candidate)) { found = true; break; }
		if (!found) { object->array.push_back(candidate); RetainOwned(candidate); }
	}
	return true;
}

extern "C" bool darling_windows_CFSetIntersect(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	const auto source_values = source_object->array;
	for (auto it = object->array.begin(); it != object->array.end();) {
		bool found = false;
		for (const auto* candidate : source_values)
			if (darling_windows_CFEqual(*it, candidate)) { found = true; break; }
		if (found) { ++it; continue; }
		ReleaseOwned(As(*it)); it = object->array.erase(it);
	}
	return true;
}

extern "C" bool darling_windows_CFSetSubtract(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	const auto source_values = source_object->array;
	for (auto it = object->array.begin(); it != object->array.end();) {
		bool found = false;
		for (const auto* candidate : source_values)
			if (darling_windows_CFEqual(*it, candidate)) { found = true; break; }
		if (!found) { ++it; continue; }
		ReleaseOwned(As(*it)); it = object->array.erase(it);
	}
	return true;
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreate(double absolute_time)
{
	auto* object = new Object{};
	object->kind = Kind::Date;
	object->date = absolute_time;
	return object;
}

extern "C" double darling_windows_CFDateGetAbsoluteTime(darling_windows_CFDateRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Date ? object->date : 0;
}

extern "C" int darling_windows_CFDateCompare(darling_windows_CFDateRef left,
	darling_windows_CFDateRef right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr || lhs->kind != Kind::Date || rhs->kind != Kind::Date) return 0;
	return lhs->date < rhs->date ? -1 : lhs->date > rhs->date ? 1 : 0;
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateByAddingTimeInterval(
    darling_windows_CFDateRef value, double seconds)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Date) return nullptr;
    return darling_windows_CFDateCreate(object->date + seconds);
}

extern "C" double darling_windows_CFDateGetTimeIntervalSinceDate(
    darling_windows_CFDateRef value, darling_windows_CFDateRef reference)
{
    const auto* object = static_cast<const Object*>(value);
    const auto* base = static_cast<const Object*>(reference);
    if (object == nullptr || base == nullptr || object->kind != Kind::Date || base->kind != Kind::Date) {
        return 0.0;
    }
    return object->date - base->date;
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithFileSystemPath(const char* path)
{
	if (path == nullptr || path[0] == '\0') return nullptr;
	auto* object = new Object{};
	object->kind = Kind::URL;
	object->string = path;
	return object;
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.c_str());
}

extern "C" darling_windows_CFBooleanRef darling_windows_CFBooleanGetValue(bool value)
{
	static Object false_value{};
	static Object true_value{};
	static const bool initialized = ((false_value.kind = Kind::Boolean),
		(true_value.kind = Kind::Boolean), (true_value.boolean = true),
		(false_value.immortal = true), (true_value.immortal = true), true);
	(void)initialized;
	return value ? &true_value : &false_value;
}

extern "C" bool darling_windows_CFBooleanIsTrue(darling_windows_CFBooleanRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Boolean && object->boolean;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFNullGetValue()
{
	static Object null_value{};
	static const bool initialized = (null_value.kind = Kind::Null, null_value.immortal = true, true);
	(void)initialized;
	return &null_value;
}

extern "C" darling_windows_CFStringRef darling_windows_CFPropertyListCreateXML(
	darling_windows_CFTypeRef value)
{
	std::ostringstream payload;
	payload << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\">";
	AppendXML(static_cast<const Object*>(value), payload);
	payload << "</plist>";
	return darling_windows_CFStringCreateWithCString(payload.str().c_str());
}

extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromXML(const char* xml)
{
	if (xml == nullptr) return nullptr;
	std::string document(xml);
	const auto plist = document.find("<plist");
	std::size_t position = plist == std::string::npos ? 0 : document.find('>', plist);
	if (position == std::string::npos) return nullptr;
	++position;
	return ParsePropertyValue(document, position);
}

extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromBinary(
	const void* bytes, darling_windows_CFIndex length)
{
	if (bytes == nullptr || length < 40) return nullptr;
	const auto* input = static_cast<const unsigned char*>(bytes);
	if (std::memcmp(input, "bplist00", 8) != 0) return nullptr;
	const auto trailer = input + length - 32;
	BinaryPlistContext context{input, static_cast<std::size_t>(length), trailer[6], trailer[7],
		static_cast<std::size_t>(ReadBigEndian(trailer + 8, 8)), 8,
		static_cast<std::size_t>(ReadBigEndian(trailer + 24, 8))};
	if (context.offset_size == 0 || context.reference_size == 0 || context.object_count == 0 ||
		context.object_count > context.length || context.offsets_table >= context.length ||
		context.offsets_table < context.object_table ||
		context.offsets_table + context.object_count * context.offset_size > context.length) return nullptr;
	std::vector<bool> active(context.object_count, false);
	return ParseBinaryObject(context, static_cast<std::size_t>(ReadBigEndian(trailer + 16, 8)), active);
}

extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetCurrent()
{
	return &current_run_loop;
}

extern "C" int darling_windows_CFRunLoopRunInMode(double seconds, bool return_after_source)
{
	if (seconds < 0) return -1;
	auto& loop = current_run_loop;
	const auto state = loop.state;
	std::unique_lock lock(state->mutex);
	state->running = true;
	state->stopped = false;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(seconds));
	bool returned_after_source = false;
	while (!state->stopped) {
		if (state->blocks.empty() && !state->condition.wait_until(lock, deadline, [&state] {
			return state->stopped || !state->blocks.empty();
		})) break;
		if (state->stopped || state->blocks.empty()) break;
		std::vector<std::pair<darling_windows_CFRunLoopBlock, void*>> pending;
		if (return_after_source) {
			pending.push_back(state->blocks.front());
			state->blocks.erase(state->blocks.begin());
			returned_after_source = true;
		} else {
			pending = std::move(state->blocks);
		}
		lock.unlock();
		for (const auto& entry : pending) if (entry.first != nullptr) entry.first(entry.second);
		lock.lock();
		if (return_after_source || std::chrono::steady_clock::now() >= deadline) break;
	}
	state->running = false;
	return state->stopped || returned_after_source ? 0 : 1;
}

extern "C" void darling_windows_CFRunLoopStop(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return;
	auto* loop = static_cast<RunLoop*>(value);
	{
		std::lock_guard lock(loop->state->mutex);
		loop->state->stopped = true;
	}
	loop->state->condition.notify_all();
}

extern "C" bool darling_windows_CFRunLoopIsRunning(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return false;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	return loop->state->running;
}

extern "C" bool darling_windows_CFRunLoopPerformBlock(darling_windows_CFRunLoopRef value,
	darling_windows_CFRunLoopBlock block, void* context)
{
	if (value == nullptr || block == nullptr) return false;
	auto* loop = static_cast<RunLoop*>(value);
	{
		std::lock_guard lock(loop->state->mutex);
		loop->state->blocks.emplace_back(block, context);
	}
	loop->state->condition.notify_all();
	return true;
}

extern "C" void darling_windows_CFRunLoopWakeUp(darling_windows_CFRunLoopRef value)
{
	if (value != nullptr) static_cast<RunLoop*>(value)->state->condition.notify_all();
}

extern "C" bool darling_windows_CFRunLoopPerformOneShotTimer(
	darling_windows_CFRunLoopRef value, double seconds,
	darling_windows_CFRunLoopBlock block, void* context)
{
	if (value == nullptr || block == nullptr || seconds < 0) return false;
	auto* loop = static_cast<RunLoop*>(value);
	const auto state = loop->state;
	std::thread([state, seconds, block, context] {
		std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
		{
			std::lock_guard lock(state->mutex);
			state->blocks.emplace_back(block, context);
		}
		state->condition.notify_all();
	}).detach();
	return true;
}

extern "C" void* darling_windows_CFNotificationCenterGetLocal()
{
	static int local_center;
	return &local_center;
}

extern "C" bool darling_windows_CFNotificationCenterAddObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name)
{
	if (center == nullptr || callback == nullptr) return false;
	std::lock_guard lock(notification_mutex);
	auto& entries = notifications[name == nullptr ? notification_wildcard : name];
	for (const auto& entry : entries)
		if (entry.observer == observer && entry.callback == callback) return true;
	entries.push_back({observer, callback});
	return true;
}

extern "C" bool darling_windows_CFNotificationCenterRemoveObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name)
{
	if (center == nullptr) return false;
	std::lock_guard lock(notification_mutex);
	if (name == nullptr) {
		bool removed = false;
		for (auto it = notifications.begin(); it != notifications.end();) {
			const auto before = it->second.size();
			it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
				[observer, callback](const NotificationObserver& entry) {
					return entry.observer == observer && (callback == nullptr || entry.callback == callback);
				}), it->second.end());
			removed = removed || before != it->second.size();
			if (it->second.empty()) it = notifications.erase(it); else ++it;
		}
		return removed;
	}
	auto found = notifications.find(name);
	if (found == notifications.end()) return false;
	auto& entries = found->second;
	const auto before = entries.size();
	entries.erase(std::remove_if(entries.begin(), entries.end(),
		[observer, callback](const NotificationObserver& entry) {
			return entry.observer == observer && (callback == nullptr || entry.callback == callback);
		}), entries.end());
	const bool removed = before != entries.size();
	if (entries.empty()) notifications.erase(found);
	return removed;
}

extern "C" void darling_windows_CFNotificationCenterPostNotification(void* center,
	const char* name, const void* object)
{
	if (center == nullptr || name == nullptr) return;
	std::vector<NotificationObserver> snapshot;
	{
		std::lock_guard lock(notification_mutex);
		const auto found = notifications.find(name);
		if (found != notifications.end()) snapshot = found->second;
		const auto wildcard = notifications.find(notification_wildcard);
		if (wildcard != notifications.end())
			snapshot.insert(snapshot.end(), wildcard->second.begin(), wildcard->second.end());
	}
	for (const auto& entry : snapshot) entry.callback(entry.observer, name, object);
}
