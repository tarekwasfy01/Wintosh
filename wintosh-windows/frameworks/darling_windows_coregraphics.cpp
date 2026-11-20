/* Minimal CoreGraphics geometry primitives; GPL-3.0-only. */
#include "darling_windows_coregraphics.h"
#include "darling_windows_corefoundation.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <limits>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <fstream>
#include <filesystem>
#include <utility>
#include <vector>

struct darling_windows_CGColorSpace final {
	std::atomic<std::uint32_t> references{1};
	int components = 0;
};

struct darling_windows_CGColorObject final {
	std::atomic<std::uint32_t> references{1};
	darling_windows_CGColorSpaceRef space = nullptr;
	darling_windows_CGColor color{};
};

struct darling_windows_CGContextObject final {
	struct Contour final {
		std::vector<darling_windows_CGPoint> points;
		bool closed = false;
	};
	struct GraphicsState final {
		darling_windows_CGColor fill{};
		darling_windows_CGColor stroke{};
		double line_width = 1;
		darling_windows_CGLineCap line_cap = darling_windows_kCGLineCapButt;
		darling_windows_CGLineJoin line_join = darling_windows_kCGLineJoinMiter;
		double alpha = 1;
		darling_windows_CGBlendMode blend_mode = darling_windows_kCGBlendModeNormal;
		darling_windows_CGAffineTransform ctm{1, 0, 0, 1, 0, 0};
		std::vector<std::uint8_t> clip_mask;
	};
	std::atomic<std::uint32_t> references{1};
	std::vector<std::uint8_t> owned_data;
	std::uint8_t* data = nullptr;
	std::size_t width = 0;
	std::size_t height = 0;
	std::size_t bytes_per_row = 0;
	darling_windows_CGColorSpaceRef space = nullptr;
	std::size_t red_channel = 0;
	std::size_t green_channel = 1;
	std::size_t blue_channel = 2;
	std::size_t alpha_channel = 3;
	bool premultiplied = true;
	darling_windows_CGBitmapInfo bitmap_info = 0;
	darling_windows_CGColor fill{0, 0, 0, 1};
	darling_windows_CGColor stroke{0, 0, 0, 1};
	double line_width = 1;
	darling_windows_CGLineCap line_cap = darling_windows_kCGLineCapButt;
	darling_windows_CGLineJoin line_join = darling_windows_kCGLineJoinMiter;
	double alpha = 1;
	darling_windows_CGBlendMode blend_mode = darling_windows_kCGBlendModeNormal;
	darling_windows_CGAffineTransform ctm{1, 0, 0, 1, 0, 0};
	std::vector<GraphicsState> saved_states;
	std::vector<std::uint8_t> clip_mask;
	std::vector<Contour> path;
};

struct darling_windows_CGImageObject final {
	std::atomic<std::uint32_t> references{1};
	std::vector<std::uint8_t> data;
	std::size_t width = 0;
	std::size_t height = 0;
	std::size_t bytes_per_row = 0;
	std::size_t red_channel = 0;
	std::size_t green_channel = 1;
	std::size_t blue_channel = 2;
	std::size_t alpha_channel = 3;
	std::size_t bits_per_component = 8;
	std::size_t bits_per_pixel = 32;
	darling_windows_CGBitmapInfo bitmap_info = 0;
	darling_windows_CGColorSpaceRef space = nullptr;
	bool premultiplied = true;
};

namespace {
enum class DataConsumerMode { Callback, MutableData, File };
struct DataConsumerState final {
	DataConsumerMode mode = DataConsumerMode::Callback;
	void* info = nullptr;
	darling_windows_CGDataConsumerPutBytesCallback put_bytes = nullptr;
	darling_windows_CGDataConsumerReleaseInfoCallback release_info = nullptr;
	darling_windows_CFDataRef data = nullptr;
	std::ofstream file;
	std::mutex mutex;
	~DataConsumerState()
	{
		if (release_info != nullptr) {
			try { release_info(info); }
			catch (...) { /* Callback exceptions must not escape CoreFoundation. */ }
		}
		if (data != nullptr) darling_windows_CFRelease(data);
	}
};

struct ImageDestinationState final {
	std::string type_identifier;
	std::size_t expected_images = 0;
	std::vector<darling_windows_CGImageRef> images;
	darling_windows_CFTypeRef consumer = nullptr;
	bool clear_consumer_data_before_write = false;
	bool finalized = false;
	~ImageDestinationState()
	{
		for (auto* image : images) CGImageRelease(image);
		if (consumer != nullptr) darling_windows_CFRelease(consumer);
	}
};

void DestroyDataConsumerState(void* payload)
{
	delete static_cast<DataConsumerState*>(payload);
}

DataConsumerState* AsDataConsumer(darling_windows_CFTypeRef value)
{
	if (value == nullptr || darling_windows_CFGetTypeID(value) !=
		darling_windows_CFDataConsumerGetTypeID()) return nullptr;
	return static_cast<DataConsumerState*>(darling_windows_CFDataConsumerGetOpaque(value));
}

darling_windows_CFTypeRef CreateDataConsumerObject(DataConsumerState* state)
{
	if (state == nullptr) return nullptr;
	auto consumer = darling_windows_CFDataConsumerCreateOpaque(state,
		&DestroyDataConsumerState);
	if (consumer == nullptr) delete state;
	return consumer;
}

std::size_t WriteDataConsumer(DataConsumerState* consumer, const void* bytes,
	std::size_t count)
{
	if (consumer == nullptr || (count != 0 && bytes == nullptr)) return 0;
	std::lock_guard<std::mutex> lock(consumer->mutex);
	try {
		switch (consumer->mode) {
		case DataConsumerMode::Callback: {
			if (consumer->put_bytes == nullptr) return 0;
			const auto written = consumer->put_bytes(consumer->info, bytes, count);
			return written <= count ? written : 0;
		}
		case DataConsumerMode::MutableData:
			if (count > static_cast<std::size_t>(
				(std::numeric_limits<darling_windows_CFIndex>::max)())) return 0;
			return darling_windows_CFDataAppendBytes(consumer->data, bytes,
				static_cast<darling_windows_CFIndex>(count)) ? count : 0;
		case DataConsumerMode::File:
			if (!consumer->file || count > static_cast<std::size_t>(
				(std::numeric_limits<std::streamsize>::max)())) return 0;
			consumer->file.write(static_cast<const char*>(bytes),
				static_cast<std::streamsize>(count));
			return consumer->file ? count : 0;
		}
	} catch (...) { return 0; }
	return 0;
}

bool WriteAllToConsumer(darling_windows_CFTypeRef consumer,
	const unsigned char* bytes, std::size_t count)
{
	auto* state = AsDataConsumer(consumer);
	if (state == nullptr || (count != 0 && bytes == nullptr)) return false;
	std::size_t offset = 0;
	while (offset < count) {
		const auto written = WriteDataConsumer(state, bytes + offset, count - offset);
		if (written == 0) return false;
		offset += written;
	}
	if (state->mode == DataConsumerMode::File) {
		std::lock_guard<std::mutex> lock(state->mutex);
		state->file.flush();
		return static_cast<bool>(state->file);
	}
	return true;
}

bool ClearConsumerData(darling_windows_CFTypeRef consumer)
{
	auto* state = AsDataConsumer(consumer);
	if (state == nullptr || state->mode != DataConsumerMode::MutableData) return false;
	std::lock_guard<std::mutex> lock(state->mutex);
	return darling_windows_CFDataClear(state->data);
}

void DestroyImageDestinationState(void* payload)
{
	delete static_cast<ImageDestinationState*>(payload);
}
ImageDestinationState* AsImageDestination(darling_windows_CFTypeRef value)
{
	if (value == nullptr || darling_windows_CFGetTypeID(value) !=
		darling_windows_CFImageDestinationGetTypeID()) return nullptr;
	return static_cast<ImageDestinationState*>(
		darling_windows_CFImageDestinationGetOpaque(value));
}
}

extern "C" {
const void* kCGImageSourceThumbnailMaxPixelSize =
	darling_windows_CFStringCreateWithCString("ThumbnailMaxPixelSize");
const void* kCGImageSourceCreateThumbnailFromImageAlways =
	darling_windows_CFStringCreateWithCString("CreateThumbnailFromImageAlways");
const void* kCGImageSourceCreateThumbnailFromImageIfAbsent =
	darling_windows_CFStringCreateWithCString("CreateThumbnailFromImageIfAbsent");
const void* kCGImageSourceCreateThumbnailWithTransform =
	darling_windows_CFStringCreateWithCString("CreateThumbnailWithTransform");
const void* kCGImagePropertyImageCount =
	darling_windows_CFStringCreateWithCString("ImageCount");
const void* kCGImagePropertyPixelWidth =
	darling_windows_CFStringCreateWithCString("PixelWidth");
const void* kCGImagePropertyPixelHeight =
	darling_windows_CFStringCreateWithCString("PixelHeight");
}

namespace {

struct ComApartment final {
	HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
	bool ready() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};

std::size_t GetBitmapFrameCount(const std::uint8_t* bytes, std::size_t length)
{
	if (bytes == nullptr || length == 0 || length > (std::numeric_limits<DWORD>::max)()) return 0;
	ComApartment apartment;
	if (!apartment.ready()) return 0;
	try {
		std::vector<BYTE> encoded(bytes, bytes + length);
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return 0;
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result)) return 0;
		result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
		if (FAILED(result)) return 0;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result)) return 0;
		UINT count = 0;
		if (FAILED(decoder->GetFrameCount(&count))) return 0;
		return count;
	} catch (...) { return 0; }
}

bool ReadLocalFileURL(darling_windows_CFURLRef url, std::vector<unsigned char>& bytes)
{
	if (url == nullptr || darling_windows_CFGetTypeID(url) != darling_windows_CFURLGetTypeID())
		return false;
	try {
		std::vector<unsigned char> path(32768);
		if (!darling_windows_CFURLGetFileSystemRepresentation(url, true, path.data(),
			static_cast<darling_windows_CFIndex>(path.size()))) return false;
		std::ifstream file(reinterpret_cast<const char*>(path.data()), std::ios::binary | std::ios::ate);
		if (!file) return false;
		const auto end = file.tellg();
		if (end <= 0 || static_cast<std::uintmax_t>(end) >
			(std::numeric_limits<DWORD>::max)() ||
			static_cast<std::uintmax_t>(end) >
			static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()) ||
			static_cast<std::uintmax_t>(end) >
			static_cast<std::uintmax_t>((std::numeric_limits<darling_windows_CFIndex>::max)()))
			return false;
		bytes.resize(static_cast<std::size_t>(end));
		file.seekg(0, std::ios::beg);
		return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size())));
	} catch (...) { return false; }
}

bool GetBitmapFrameSize(const std::uint8_t* bytes, std::size_t length,
	std::size_t frame_index, std::size_t& width, std::size_t& height)
{
	width = 0;
	height = 0;
	if (bytes == nullptr || length == 0 || length > (std::numeric_limits<DWORD>::max)())
		return false;
	ComApartment apartment;
	if (!apartment.ready()) return false;
	try {
		std::vector<BYTE> encoded(bytes, bytes + length);
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return false;
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result)) return false;
		result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
		if (FAILED(result)) return false;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result) || frame_index > (std::numeric_limits<UINT>::max)()) return false;
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(static_cast<UINT>(frame_index), frame.GetAddressOf());
		if (FAILED(result)) return false;
		UINT decoded_width = 0, decoded_height = 0;
		result = frame->GetSize(&decoded_width, &decoded_height);
		if (FAILED(result) || decoded_width == 0 || decoded_height == 0) return false;
		width = decoded_width;
		height = decoded_height;
		return true;
	} catch (...) { return false; }
}

std::uint16_t GetBitmapExifOrientation(const std::uint8_t* bytes, std::size_t length,
	std::size_t frame_index)
{
	if (bytes == nullptr || length == 0 || length > (std::numeric_limits<DWORD>::max)() ||
		frame_index > (std::numeric_limits<UINT>::max)()) return 1;
	ComApartment apartment;
	if (!apartment.ready()) return 1;
	try {
		std::vector<BYTE> encoded(bytes, bytes + length);
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return 1;
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result)) return 1;
		result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
		if (FAILED(result)) return 1;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result)) return 1;
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(static_cast<UINT>(frame_index), frame.GetAddressOf());
		if (FAILED(result)) return 1;
		Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
		if (FAILED(frame->GetMetadataQueryReader(reader.GetAddressOf()))) return 1;
		const wchar_t* queries[] = {
			L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"/ifd/{ushort=0x0112}"
		};
		for (const auto* query : queries) {
			PROPVARIANT value;
			PropVariantInit(&value);
			result = reader->GetMetadataByName(query, &value);
			std::uint16_t orientation = 1;
			const bool found = SUCCEEDED(result);
			if (found) {
				switch (value.vt) {
				case VT_UI1: orientation = value.bVal; break;
				case VT_UI2: orientation = value.uiVal; break;
				case VT_I2: if (value.iVal > 0) orientation = static_cast<std::uint16_t>(value.iVal); break;
				case VT_UI4: if (value.ulVal <= 8) orientation = static_cast<std::uint16_t>(value.ulVal); break;
				case VT_I4: if (value.lVal > 0 && value.lVal <= 8) orientation = static_cast<std::uint16_t>(value.lVal); break;
				default: break;
				}
			}
			PropVariantClear(&value);
			if (found && orientation >= 1 && orientation <= 8) return orientation;
		}
	} catch (...) { return 1; }
	return 1;
}

const char* ContainerFormatIdentifier(REFGUID format)
{
	if (IsEqualGUID(format, GUID_ContainerFormatPng)) return "public.png";
	if (IsEqualGUID(format, GUID_ContainerFormatJpeg)) return "public.jpeg";
	if (IsEqualGUID(format, GUID_ContainerFormatGif)) return "com.compuserve.gif";
	if (IsEqualGUID(format, GUID_ContainerFormatTiff)) return "public.tiff";
	if (IsEqualGUID(format, GUID_ContainerFormatBmp)) return "com.microsoft.bmp";
	if (IsEqualGUID(format, GUID_ContainerFormatIco)) return "com.microsoft.ico";
	if (IsEqualGUID(format, GUID_ContainerFormatWmp)) return "com.microsoft.jpegxr";
	return nullptr;
}

const char* GetBitmapContainerType(const std::uint8_t* bytes, std::size_t length)
{
	if (bytes == nullptr || length == 0 || length > (std::numeric_limits<DWORD>::max)())
		return nullptr;
	ComApartment apartment;
	if (!apartment.ready()) return nullptr;
	try {
		std::vector<BYTE> encoded(bytes, bytes + length);
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result)) return nullptr;
		result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result)) return nullptr;
		GUID format{};
		if (FAILED(decoder->GetContainerFormat(&format))) return nullptr;
		return ContainerFormatIdentifier(format);
	} catch (...) { return nullptr; }
}

darling_windows_CFArrayRef EnumerateBitmapTypeIdentifiers()
{
	ComApartment apartment;
	if (!apartment.ready()) return nullptr;
	std::vector<darling_windows_CFStringRef> strings;
	try {
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IEnumUnknown> enumerator;
		result = factory->CreateComponentEnumerator(WICDecoder,
			WICComponentEnumerateDefault, enumerator.GetAddressOf());
		if (FAILED(result)) return nullptr;
		for (;;) {
			Microsoft::WRL::ComPtr<IUnknown> component;
			ULONG fetched = 0;
			result = enumerator->Next(1, component.GetAddressOf(), &fetched);
			if (result != S_OK || fetched != 1) break;
			Microsoft::WRL::ComPtr<IWICBitmapDecoderInfo> decoder_info;
			if (FAILED(component.As(&decoder_info))) continue;
			GUID format{};
			if (FAILED(decoder_info->GetContainerFormat(&format))) continue;
			const auto* identifier = ContainerFormatIdentifier(format);
			if (identifier == nullptr) continue;
			bool already_added = false;
			for (const auto existing : strings) {
				const char* existing_cstring = darling_windows_CFStringGetCStringPtr(existing);
				if (existing_cstring != nullptr && std::strcmp(existing_cstring, identifier) == 0) {
					already_added = true;
					break;
				}
			}
			if (already_added) continue;
		using OwnedString = std::unique_ptr<const void, decltype(&darling_windows_CFRelease)>;
		OwnedString string(darling_windows_CFStringCreateWithCString(identifier),
			darling_windows_CFRelease);
		if (!string) throw std::bad_alloc{};
		strings.push_back(string.get());
		string.release();
		}
		const auto result_array = darling_windows_CFArrayCreate(
			reinterpret_cast<const void* const*>(strings.data()),
			static_cast<darling_windows_CFIndex>(strings.size()));
		for (const auto string : strings) darling_windows_CFRelease(string);
		return result_array;
	} catch (...) {
		for (const auto string : strings) darling_windows_CFRelease(string);
		return nullptr;
	}
}

darling_windows_CGImageRef DecodeBitmapImage(const std::uint8_t* bytes,
	std::size_t length, std::size_t frame_index, bool prefer_embedded_thumbnail = false)
{
	if (bytes == nullptr || length == 0 || length > (std::numeric_limits<DWORD>::max)())
		return nullptr;
	ComApartment apartment;
	if (!apartment.ready()) return nullptr;
	darling_windows_CGImageRef image = nullptr;
	try {
		std::vector<BYTE> encoded(bytes, bytes + length);
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(stream.GetAddressOf());
		if (FAILED(result)) return nullptr;
		result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr,
			WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(static_cast<UINT>(frame_index), frame.GetAddressOf());
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmapSource> embedded_thumbnail;
		IWICBitmapSource* pixel_source = frame.Get();
		if (prefer_embedded_thumbnail) {
			result = frame->GetThumbnail(embedded_thumbnail.GetAddressOf());
			if (FAILED(result)) return nullptr;
			pixel_source = embedded_thumbnail.Get();
		}
		UINT width = 0, height = 0;
		result = pixel_source->GetSize(&width, &height);
		if (FAILED(result) || width == 0 || height == 0 ||
			width > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
			height > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
			width > (std::numeric_limits<UINT>::max)() / 4 ||
			static_cast<std::size_t>(height) >
				(std::numeric_limits<std::size_t>::max)() / (static_cast<std::size_t>(width) * 4)) {
			return nullptr;
		}
		Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
		result = factory->CreateFormatConverter(converter.GetAddressOf());
		if (FAILED(result)) return nullptr;
		result = converter->Initialize(pixel_source, GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
		if (FAILED(result)) return nullptr;
		auto decoded = std::unique_ptr<darling_windows_CGImageObject>(
			new (std::nothrow) darling_windows_CGImageObject);
		if (decoded == nullptr) return nullptr;
		const UINT stride = width * 4;
		const std::size_t storage_size = static_cast<std::size_t>(stride) * height;
		if (storage_size > (std::numeric_limits<UINT>::max)()) return nullptr;
		decoded->data.resize(storage_size);
		result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(storage_size),
			decoded->data.data());
		if (FAILED(result)) return nullptr;
		decoded->width = width;
		decoded->height = height;
		decoded->bytes_per_row = stride;
		decoded->red_channel = 2;
		decoded->green_channel = 1;
		decoded->blue_channel = 0;
		decoded->alpha_channel = 3;
		decoded->bitmap_info = darling_windows_kCGImageAlphaPremultipliedFirst |
			darling_windows_kCGBitmapByteOrder32Little;
		decoded->space = CGColorSpaceCreateDeviceRGB();
		if (decoded->space == nullptr) return nullptr;
		image = decoded.release();
	} catch (...) {
		image = nullptr;
	}
	return image;
}

} // namespace

namespace {

darling_windows_CGImageRef ScaleBitmapImage(darling_windows_CGImageRef image,
	std::size_t maximum_dimension)
{
	if (image == nullptr) return nullptr;
	if (maximum_dimension == 0 ||
		(image->width <= maximum_dimension && image->height <= maximum_dimension)) return image;
	const double scale = static_cast<double>(maximum_dimension) /
		static_cast<double>((std::max)(image->width, image->height));
	const UINT target_width = (std::max)(1u, static_cast<UINT>(std::floor(image->width * scale + 0.5)));
	const UINT target_height = (std::max)(1u, static_cast<UINT>(std::floor(image->height * scale + 0.5)));
	if (target_width > (std::numeric_limits<UINT>::max)() / 4) return nullptr;
	const UINT stride = target_width * 4;
	const std::size_t storage_size = static_cast<std::size_t>(stride) * target_height;
	if (storage_size > (std::numeric_limits<UINT>::max)()) return nullptr;
	ComApartment apartment;
	if (!apartment.ready()) return nullptr;
	try {
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
		result = factory->CreateBitmapFromMemory(static_cast<UINT>(image->width),
			static_cast<UINT>(image->height), GUID_WICPixelFormat32bppPBGRA,
			static_cast<UINT>(image->bytes_per_row), static_cast<UINT>(image->data.size()),
			image->data.data(), bitmap.GetAddressOf());
		if (FAILED(result)) return nullptr;
		Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
		result = factory->CreateBitmapScaler(scaler.GetAddressOf());
		if (FAILED(result)) return nullptr;
		result = scaler->Initialize(bitmap.Get(), target_width, target_height,
			WICBitmapInterpolationModeFant);
		if (FAILED(result)) return nullptr;
		auto scaled = std::unique_ptr<darling_windows_CGImageObject>(
			new (std::nothrow) darling_windows_CGImageObject);
		if (scaled == nullptr) return nullptr;
		scaled->data.resize(storage_size);
		result = scaler->CopyPixels(nullptr, stride, static_cast<UINT>(storage_size),
			scaled->data.data());
		if (FAILED(result)) return nullptr;
		scaled->width = target_width;
		scaled->height = target_height;
		scaled->bytes_per_row = stride;
		scaled->red_channel = image->red_channel;
		scaled->green_channel = image->green_channel;
		scaled->blue_channel = image->blue_channel;
		scaled->alpha_channel = image->alpha_channel;
		scaled->bits_per_component = image->bits_per_component;
		scaled->bits_per_pixel = image->bits_per_pixel;
		scaled->bitmap_info = image->bitmap_info;
		scaled->premultiplied = image->premultiplied;
		scaled->space = image->space;
		CGColorSpaceRetain(scaled->space);
		return scaled.release();
	} catch (...) { return nullptr; }
}

darling_windows_CGImageRef ApplyExifOrientation(darling_windows_CGImageRef image,
	std::uint16_t orientation)
{
	if (image == nullptr || orientation < 1 || orientation > 8) return nullptr;
	if (orientation == 1) return image;
	const bool swaps_axes = orientation >= 5;
	const std::size_t output_width = swaps_axes ? image->height : image->width;
	const std::size_t output_height = swaps_axes ? image->width : image->height;
	if (output_width > (std::numeric_limits<std::size_t>::max)() / 4 ||
		output_height > (std::numeric_limits<std::size_t>::max)() / (output_width * 4))
		return nullptr;
	try {
		auto oriented = std::unique_ptr<darling_windows_CGImageObject>(
			new (std::nothrow) darling_windows_CGImageObject);
		if (oriented == nullptr) return nullptr;
		oriented->width = output_width;
		oriented->height = output_height;
		oriented->bytes_per_row = output_width * 4;
		oriented->red_channel = image->red_channel;
		oriented->green_channel = image->green_channel;
		oriented->blue_channel = image->blue_channel;
		oriented->alpha_channel = image->alpha_channel;
		oriented->bits_per_component = image->bits_per_component;
		oriented->bits_per_pixel = image->bits_per_pixel;
		oriented->bitmap_info = image->bitmap_info;
		oriented->premultiplied = image->premultiplied;
		oriented->space = image->space;
		oriented->data.resize(oriented->bytes_per_row * output_height);
		CGColorSpaceRetain(oriented->space);
		for (std::size_t y = 0; y < image->height; ++y) {
			for (std::size_t x = 0; x < image->width; ++x) {
				std::size_t target_x = x;
				std::size_t target_y = y;
				switch (orientation) {
				case 2: target_x = image->width - 1 - x; break;
				case 3: target_x = image->width - 1 - x; target_y = image->height - 1 - y; break;
				case 4: target_y = image->height - 1 - y; break;
				case 5: target_x = y; target_y = x; break;
				case 6: target_x = image->height - 1 - y; target_y = x; break;
				case 7: target_x = image->height - 1 - y; target_y = image->width - 1 - x; break;
				case 8: target_x = y; target_y = image->width - 1 - x; break;
				default: break;
				}
				const auto source_offset = y * image->bytes_per_row + x * 4;
				const auto target_offset = target_y * oriented->bytes_per_row + target_x * 4;
				std::memcpy(oriented->data.data() + target_offset,
					image->data.data() + source_offset, 4);
			}
		}
		return oriented.release();
	} catch (...) { return nullptr; }
}

} // namespace

namespace {

double ClampUnit(double value)
{
	return (std::max)(0.0, (std::min)(1.0, value));
}

darling_windows_CGPoint TransformPoint(darling_windows_CGPoint point,
	darling_windows_CGAffineTransform transform)
{
	return darling_windows_CGPointApplyAffineTransform(point, transform);
}

darling_windows_CGRect TransformBounds(darling_windows_CGRect rect,
	darling_windows_CGAffineTransform transform)
{
	return darling_windows_CGRectApplyAffineTransform(rect, transform);
}

std::size_t PixelBegin(double value, std::size_t limit)
{
	if (!std::isfinite(value) || value <= 0)
		return 0;
	if (value >= static_cast<double>(limit))
		return limit;
	return static_cast<std::size_t>(std::floor(value));
}

std::size_t PixelEnd(double value, std::size_t limit)
{
	if (!std::isfinite(value) || value <= 0)
		return 0;
	if (value >= static_cast<double>(limit))
		return limit;
	return static_cast<std::size_t>(std::ceil(value));
}

std::uint8_t ToByte(double value)
{
	return static_cast<std::uint8_t>(std::lround(ClampUnit(value) * 255.0));
}

std::uint8_t ClipCoverage(darling_windows_CGContextRef context,
	std::size_t x, std::size_t y)
{
	return context->clip_mask.empty() ? 255 : context->clip_mask[y * context->width + x];
}

void CompositePixel(darling_windows_CGContextRef context, std::uint8_t* pixel,
	const double source[3], double source_alpha, double coverage)
{
	const double destination_alpha = pixel[context->alpha_channel] / 255.0;
	const std::size_t channels[] = {
		context->red_channel, context->green_channel, context->blue_channel};
	double destination_premultiplied[3]{};
	double destination[3]{};
	for (std::size_t channel = 0; channel < 3; ++channel)
		destination_premultiplied[channel] = pixel[channels[channel]] / 255.0;
	for (std::size_t channel = 0; channel < 3; ++channel)
		destination[channel] = destination_alpha > 0 ?
			destination_premultiplied[channel] / destination_alpha : 0;
	if ((context->blend_mode >= darling_windows_kCGBlendModeMultiply &&
		context->blend_mode <= darling_windows_kCGBlendModeLighten) ||
		context->blend_mode == darling_windows_kCGBlendModeDifference ||
		context->blend_mode == darling_windows_kCGBlendModeExclusion) {
		double result[3]{};
		for (std::size_t channel = 0; channel < 3; ++channel) {
			double blend = source[channel];
			switch (context->blend_mode) {
			case darling_windows_kCGBlendModeMultiply:
				blend = source[channel] * destination[channel]; break;
			case darling_windows_kCGBlendModeScreen:
				blend = source[channel] + destination[channel] -
					source[channel] * destination[channel]; break;
			case darling_windows_kCGBlendModeOverlay:
				blend = destination[channel] <= 0.5 ?
					2 * source[channel] * destination[channel] :
					1 - 2 * (1 - source[channel]) * (1 - destination[channel]); break;
			case darling_windows_kCGBlendModeDarken:
				blend = (std::min)(source[channel], destination[channel]); break;
			case darling_windows_kCGBlendModeLighten:
				blend = (std::max)(source[channel], destination[channel]); break;
			case darling_windows_kCGBlendModeDifference:
				blend = std::abs(source[channel] - destination[channel]); break;
			case darling_windows_kCGBlendModeExclusion:
				blend = source[channel] + destination[channel] -
					2 * source[channel] * destination[channel]; break;
			default: break;
			}
			result[channel] = source_alpha * destination_alpha * blend +
				source_alpha * (1 - destination_alpha) * source[channel] +
				(1 - source_alpha) * destination_premultiplied[channel];
		}
		const double output_alpha = source_alpha + destination_alpha -
			source_alpha * destination_alpha;
		for (std::size_t channel = 0; channel < 3; ++channel)
			result[channel] = result[channel] * coverage +
				destination_premultiplied[channel] * (1 - coverage);
		const double final_alpha = output_alpha * coverage +
			destination_alpha * (1 - coverage);
		for (std::size_t channel = 0; channel < 3; ++channel)
			pixel[channels[channel]] = ToByte(result[channel]);
		pixel[context->alpha_channel] = ToByte(final_alpha);
		return;
	}
	double source_factor = 1;
	double destination_factor = 1 - source_alpha;
	switch (context->blend_mode) {
	case darling_windows_kCGBlendModeClear:
		source_factor = 0; destination_factor = 1 - source_alpha; break;
	case darling_windows_kCGBlendModeCopy:
		source_factor = 1; destination_factor = 0; break;
	case darling_windows_kCGBlendModeSourceIn:
		source_factor = destination_alpha; destination_factor = 0; break;
	case darling_windows_kCGBlendModeSourceOut:
		source_factor = 1 - destination_alpha; destination_factor = 0; break;
	case darling_windows_kCGBlendModeSourceAtop:
		source_factor = destination_alpha; destination_factor = 1 - source_alpha; break;
	case darling_windows_kCGBlendModeDestinationOver:
		source_factor = 1 - destination_alpha; destination_factor = 1; break;
	case darling_windows_kCGBlendModeDestinationIn:
		source_factor = 0; destination_factor = source_alpha; break;
	case darling_windows_kCGBlendModeDestinationOut:
		source_factor = 0; destination_factor = 1 - source_alpha; break;
	case darling_windows_kCGBlendModeDestinationAtop:
		source_factor = 1 - destination_alpha; destination_factor = source_alpha; break;
	case darling_windows_kCGBlendModeXOR:
		source_factor = 1 - destination_alpha; destination_factor = 1 - source_alpha; break;
	default: break;
	}
	const double output_alpha = source_alpha * source_factor +
		destination_alpha * destination_factor;
	for (std::size_t channel = 0; channel < 3; ++channel)
		pixel[channels[channel]] = ToByte(coverage *
			(source[channel] * source_alpha * source_factor +
			destination_premultiplied[channel] * destination_factor) +
			(1 - coverage) * destination_premultiplied[channel]);
	pixel[context->alpha_channel] = ToByte(coverage * output_alpha +
		(1 - coverage) * destination_alpha);
}

void DrawRect(darling_windows_CGContextRef context,
	darling_windows_CGRect rect, bool clear)
{
	if (context == nullptr || context->data == nullptr ||
		!std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y) ||
		!std::isfinite(rect.size.width) || !std::isfinite(rect.size.height) ||
		rect.size.width <= 0 || rect.size.height <= 0)
		return;
	const auto bounds = TransformBounds(rect, context->ctm);
	const auto x0 = PixelBegin(bounds.origin.x, context->width);
	const auto y0 = PixelBegin(bounds.origin.y, context->height);
	const auto x1 = PixelEnd(bounds.origin.x + bounds.size.width, context->width);
	const auto y1 = PixelEnd(bounds.origin.y + bounds.size.height, context->height);
	bool invertible = false;
	const auto inverse = darling_windows_CGAffineTransformInvert(context->ctm, &invertible);
	if (!invertible) return;
	const double fill_alpha = ClampUnit(context->fill.alpha) * context->alpha;
	const double source[] = {
		ClampUnit(context->fill.red), ClampUnit(context->fill.green),
		ClampUnit(context->fill.blue)};
	for (std::size_t y = y0; y < y1; ++y) {
		for (std::size_t x = x0; x < x1; ++x) {
			const double coverage = ClipCoverage(context, x, y) / 255.0;
			if (coverage == 0) continue;
			const auto user = TransformPoint({x + 0.5, y + 0.5}, inverse);
			if (user.x < rect.origin.x || user.y < rect.origin.y ||
				user.x >= rect.origin.x + rect.size.width ||
				user.y >= rect.origin.y + rect.size.height) continue;
			const auto row = context->height - 1 - y;
			auto* pixel = context->data + row * context->bytes_per_row + x * 4;
			if (clear) {
				const double remaining = 1.0 - coverage;
				pixel[context->red_channel] = ToByte(pixel[context->red_channel] / 255.0 * remaining);
				pixel[context->green_channel] = ToByte(pixel[context->green_channel] / 255.0 * remaining);
				pixel[context->blue_channel] = ToByte(pixel[context->blue_channel] / 255.0 * remaining);
				pixel[context->alpha_channel] = ToByte(pixel[context->alpha_channel] / 255.0 * remaining);
				continue;
			}
			CompositePixel(context, pixel, source, fill_alpha, coverage);
		}
	}
}

int PathWinding(const darling_windows_CGContextObject::Contour& contour,
	double x, double y, bool& parity)
{
	if (contour.points.size() < 3) return 0;
	int winding = 0;
	for (std::size_t index = 0; index < contour.points.size(); ++index) {
		const auto& first = contour.points[index];
		const auto& second = contour.points[(index + 1) % contour.points.size()];
		if ((first.y > y) != (second.y > y)) {
			const double cross_x = first.x + (y - first.y) *
				(second.x - first.x) / (second.y - first.y);
			if (cross_x > x) {
				parity = !parity;
				winding += second.y > first.y ? 1 : -1;
			}
		}
	}
	return winding;
}

void ApplyCurrentPathAsClip(darling_windows_CGContextRef context, bool even_odd)
{
	if (context == nullptr) return;
	if (context->width > (std::numeric_limits<std::size_t>::max)() / context->height) {
		context->path.clear();
		return;
	}
	try {
		const auto area = context->width * context->height;
		std::vector<std::uint8_t> mask(area, 0);
		auto path = context->path;
		for (auto& contour : path)
			for (auto& point : contour.points) point = TransformPoint(point, context->ctm);
		double min_x = (std::numeric_limits<double>::infinity)();
		double min_y = (std::numeric_limits<double>::infinity)();
		double max_x = -(std::numeric_limits<double>::infinity)();
		double max_y = -(std::numeric_limits<double>::infinity)();
		for (const auto& contour : path) {
			for (const auto& point : contour.points) {
				if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
				min_x = (std::min)(min_x, point.x);
				min_y = (std::min)(min_y, point.y);
				max_x = (std::max)(max_x, point.x);
				max_y = (std::max)(max_y, point.y);
			}
		}
		if (std::isfinite(min_x) && std::isfinite(min_y) &&
			std::isfinite(max_x) && std::isfinite(max_y)) {
			const auto x0 = PixelBegin(min_x, context->width);
			const auto y0 = PixelBegin(min_y, context->height);
			const auto x1 = PixelEnd(max_x, context->width);
			const auto y1 = PixelEnd(max_y, context->height);
			for (std::size_t y = y0; y < y1; ++y) {
				for (std::size_t x = x0; x < x1; ++x) {
					bool parity = false;
					int winding = 0;
					for (const auto& contour : path)
						winding += PathWinding(contour, x + 0.5, y + 0.5, parity);
					if (even_odd ? parity : winding != 0)
						mask[y * context->width + x] = 255;
				}
			}
		}
		if (context->clip_mask.empty()) context->clip_mask = std::move(mask);
		else
			for (std::size_t index = 0; index < area; ++index)
				context->clip_mask[index] = static_cast<std::uint8_t>(
					(context->clip_mask[index] * mask[index] + 127) / 255);
	} catch (...) {
	}
	context->path.clear();
}

void FillCurrentPath(darling_windows_CGContextRef context, bool even_odd)
{
	if (context == nullptr || context->path.empty()) return;
	const auto saved_ctm = context->ctm;
	for (auto& contour : context->path)
		for (auto& point : contour.points) point = TransformPoint(point, saved_ctm);
	context->ctm = darling_windows_CGAffineTransformIdentity();
	double min_x = (std::numeric_limits<double>::infinity)();
	double min_y = (std::numeric_limits<double>::infinity)();
	double max_x = -(std::numeric_limits<double>::infinity)();
	double max_y = -(std::numeric_limits<double>::infinity)();
	for (const auto& contour : context->path) {
		for (const auto& point : contour.points) {
			if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
			min_x = (std::min)(min_x, point.x);
			min_y = (std::min)(min_y, point.y);
			max_x = (std::max)(max_x, point.x);
			max_y = (std::max)(max_y, point.y);
		}
	}
	if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
		!std::isfinite(max_x) || !std::isfinite(max_y)) {
		context->path.clear();
		context->ctm = saved_ctm;
		return;
	}
	const auto x0 = PixelBegin(min_x, context->width);
	const auto y0 = PixelBegin(min_y, context->height);
	const auto x1 = PixelEnd(max_x, context->width);
	const auto y1 = PixelEnd(max_y, context->height);
	for (std::size_t y = y0; y < y1; ++y) {
		for (std::size_t x = x0; x < x1; ++x) {
			bool parity = false;
			int winding = 0;
			for (const auto& contour : context->path)
				winding += PathWinding(contour, x + 0.5, y + 0.5, parity);
			const bool inside = even_odd ? parity : winding != 0;
			if (inside)
				DrawRect(context, {{static_cast<double>(x), static_cast<double>(y)}, {1, 1}}, false);
		}
	}
	context->path.clear();
	context->ctm = saved_ctm;
}

double SegmentDistanceSquared(const darling_windows_CGPoint& point,
	const darling_windows_CGPoint& first, const darling_windows_CGPoint& second)
{
	const double dx = second.x - first.x;
	const double dy = second.y - first.y;
	const double length_squared = dx * dx + dy * dy;
	if (length_squared == 0)
		return (point.x - first.x) * (point.x - first.x) +
			(point.y - first.y) * (point.y - first.y);
	const double t = ClampUnit(((point.x - first.x) * dx + (point.y - first.y) * dy) /
		length_squared);
	const double px = first.x + t * dx;
	const double py = first.y + t * dy;
	return (point.x - px) * (point.x - px) + (point.y - py) * (point.y - py);
}

bool IsWithinStrokedSegment(const darling_windows_CGPoint& point,
	const darling_windows_CGPoint& first, const darling_windows_CGPoint& second,
	double radius, bool open_start, bool open_end,
	darling_windows_CGLineCap cap)
{
	const double dx = second.x - first.x;
	const double dy = second.y - first.y;
	const double length = std::hypot(dx, dy);
	if (!std::isfinite(length) || length == 0.0)
		return cap == darling_windows_kCGLineCapRound &&
			SegmentDistanceSquared(point, first, second) <= radius * radius;
	const double along = ((point.x - first.x) * dx +
		(point.y - first.y) * dy) / length;
	const double across = std::abs((point.x - first.x) * dy -
		(point.y - first.y) * dx) / length;
	if (across > radius) return false;
	const double start_extension = !open_start ? radius :
		(cap == darling_windows_kCGLineCapSquare ? radius : 0.0);
	const double end_extension = !open_end ? radius :
		(cap == darling_windows_kCGLineCapSquare ? radius : 0.0);
	if (along >= -start_extension && along <= length + end_extension)
		return true;
	if (cap != darling_windows_kCGLineCapRound) return false;
	if (open_start && along < 0.0)
		return along * along + across * across <= radius * radius;
	const double beyond_end = along - length;
	return open_end && beyond_end > 0.0 &&
		beyond_end * beyond_end + across * across <= radius * radius;
}

double PointCross(darling_windows_CGPoint origin,
	darling_windows_CGPoint first, darling_windows_CGPoint second)
{
	return (first.x - origin.x) * (second.y - origin.y) -
		(first.y - origin.y) * (second.x - origin.x);
}

bool PointInTriangle(darling_windows_CGPoint point,
	darling_windows_CGPoint first, darling_windows_CGPoint second,
	darling_windows_CGPoint third)
{
	const double a = PointCross(first, second, point);
	const double b = PointCross(second, third, point);
	const double c = PointCross(third, first, point);
	return (a >= -1e-9 && b >= -1e-9 && c >= -1e-9) ||
		(a <= 1e-9 && b <= 1e-9 && c <= 1e-9);
}

void FillJoinTriangle(darling_windows_CGContextRef context,
	darling_windows_CGPoint first, darling_windows_CGPoint second,
	darling_windows_CGPoint third)
{
	const double minimum_x = (std::min)({first.x, second.x, third.x});
	const double maximum_x = (std::max)({first.x, second.x, third.x});
	const double minimum_y = (std::min)({first.y, second.y, third.y});
	const double maximum_y = (std::max)({first.y, second.y, third.y});
	const auto x0 = PixelBegin(minimum_x, context->width);
	const auto y0 = PixelBegin(minimum_y, context->height);
	const auto x1 = PixelEnd(maximum_x, context->width);
	const auto y1 = PixelEnd(maximum_y, context->height);
	for (std::size_t y = y0; y < y1; ++y)
		for (std::size_t x = x0; x < x1; ++x)
			if (PointInTriangle({x + 0.5, y + 0.5}, first, second, third))
				DrawRect(context, {{static_cast<double>(x), static_cast<double>(y)}, {1, 1}}, false);
}

void FillJoinDisk(darling_windows_CGContextRef context,
	darling_windows_CGPoint center, double radius)
{
	const auto x0 = PixelBegin(center.x - radius, context->width);
	const auto y0 = PixelBegin(center.y - radius, context->height);
	const auto x1 = PixelEnd(center.x + radius, context->width);
	const auto y1 = PixelEnd(center.y + radius, context->height);
	for (std::size_t y = y0; y < y1; ++y) {
		for (std::size_t x = x0; x < x1; ++x) {
			const double dx = x + 0.5 - center.x;
			const double dy = y + 0.5 - center.y;
			if (dx * dx + dy * dy <= radius * radius)
				DrawRect(context, {{static_cast<double>(x), static_cast<double>(y)}, {1, 1}}, false);
		}
	}
}

void StrokeContourJoins(darling_windows_CGContextRef context,
	const darling_windows_CGContextObject::Contour& contour, double radius)
{
	if (contour.points.size() < 3 || radius <= 0) return;
	const std::size_t first_vertex = contour.closed ? 0 : 1;
	const std::size_t vertex_limit = contour.closed ? contour.points.size() :
		contour.points.size() - 1;
	for (std::size_t index = first_vertex; index < vertex_limit; ++index) {
		const auto vertex = contour.points[index];
		const auto previous = contour.points[(index + contour.points.size() - 1) %
			contour.points.size()];
		const auto next = contour.points[(index + 1) % contour.points.size()];
		darling_windows_CGPoint incoming{vertex.x - previous.x, vertex.y - previous.y};
		darling_windows_CGPoint outgoing{next.x - vertex.x, next.y - vertex.y};
		const double incoming_length = std::hypot(incoming.x, incoming.y);
		const double outgoing_length = std::hypot(outgoing.x, outgoing.y);
		if (incoming_length <= 1e-12 || outgoing_length <= 1e-12) continue;
		incoming.x /= incoming_length;
		incoming.y /= incoming_length;
		outgoing.x /= outgoing_length;
		outgoing.y /= outgoing_length;
		const double cross = incoming.x * outgoing.y - incoming.y * outgoing.x;
		if (std::abs(cross) <= 1e-12) continue;
		if (context->line_join == darling_windows_kCGLineJoinRound) {
			FillJoinDisk(context, vertex, radius);
			continue;
		}
		const double side = cross > 0 ? -1.0 : 1.0;
		const darling_windows_CGPoint outer_in{vertex.x - incoming.y * side * radius,
			vertex.y + incoming.x * side * radius};
		const darling_windows_CGPoint outer_out{vertex.x - outgoing.y * side * radius,
			vertex.y + outgoing.x * side * radius};
		if (context->line_join == darling_windows_kCGLineJoinMiter) {
			const double denominator = incoming.x * outgoing.y - incoming.y * outgoing.x;
			const double t = ((outer_out.x - outer_in.x) * outgoing.y -
				(outer_out.y - outer_in.y) * outgoing.x) / denominator;
			const darling_windows_CGPoint miter{outer_in.x + incoming.x * t,
				outer_in.y + incoming.y * t};
			if (std::isfinite(miter.x) && std::isfinite(miter.y) &&
				std::hypot(miter.x - vertex.x, miter.y - vertex.y) <= 10.0 * radius) {
				FillJoinTriangle(context, outer_in, miter, outer_out);
				continue;
			}
		}
		FillJoinTriangle(context, vertex, outer_in, outer_out);
	}
}

void StrokeCurrentPath(darling_windows_CGContextRef context)
{
	if (context == nullptr || context->path.empty()) return;
	if (context->line_width <= 0) {
		context->path.clear();
		return;
	}
	const auto saved_ctm = context->ctm;
	for (auto& contour : context->path)
		for (auto& point : contour.points) point = TransformPoint(point, saved_ctm);
	const double scale_x = std::hypot(saved_ctm.a, saved_ctm.b);
	const double scale_y = std::hypot(saved_ctm.c, saved_ctm.d);
	const double scale = std::sqrt(scale_x * scale_y);
	if (!std::isfinite(scale) || scale <= 0) {
		context->path.clear();
		context->ctm = saved_ctm;
		return;
	}
	const double saved_line_width = context->line_width;
	context->line_width = saved_line_width * scale;
	context->ctm = darling_windows_CGAffineTransformIdentity();
	const auto old_fill = context->fill;
	context->fill = context->stroke;
	const double radius = context->line_width / 2.0;
	for (const auto& contour : context->path) {
		if (contour.points.empty()) continue;
		const auto segment_count = contour.points.size() - 1 + (contour.closed ? 1 : 0);
		for (std::size_t segment = 0; segment < segment_count; ++segment) {
			const bool open_start = !contour.closed && segment == 0;
			const bool open_end = !contour.closed && segment + 1 == segment_count;
			const auto& first = contour.points[segment];
			const auto& second = contour.points[(segment + 1) % contour.points.size()];
			const auto x0 = PixelBegin((std::min)(first.x, second.x) - radius, context->width);
			const auto y0 = PixelBegin((std::min)(first.y, second.y) - radius, context->height);
			const auto x1 = PixelEnd((std::max)(first.x, second.x) + radius, context->width);
			const auto y1 = PixelEnd((std::max)(first.y, second.y) + radius, context->height);
			for (std::size_t y = y0; y < y1; ++y) {
				for (std::size_t x = x0; x < x1; ++x) {
					if (IsWithinStrokedSegment({x + 0.5, y + 0.5}, first,
						second, radius, open_start, open_end, context->line_cap))
						DrawRect(context, {{static_cast<double>(x), static_cast<double>(y)}, {1, 1}}, false);
				}
			}
		}
	}
	for (const auto& contour : context->path)
		StrokeContourJoins(context, contour, radius);
	context->fill = old_fill;
	context->line_width = saved_line_width;
	context->path.clear();
	context->ctm = saved_ctm;
}

template <typename T>
void ReleaseReference(T* value)
{
	if (value != nullptr && value->references.fetch_sub(1,
		std::memory_order_acq_rel) == 1)
		delete value;
}

} // namespace

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef
CGColorSpaceCreateDeviceRGB()
{
	auto* space = new (std::nothrow) darling_windows_CGColorSpace;
	if (space != nullptr) space->components = 3;
	return space;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef
CGColorSpaceCreateDeviceGray()
{
	auto* space = new (std::nothrow) darling_windows_CGColorSpace;
	if (space != nullptr) space->components = 1;
	return space;
}

extern "C" DARLING_CG_GUEST_ABI void CGColorSpaceRetain(
	darling_windows_CGColorSpaceRef space)
{
	if (space != nullptr) space->references.fetch_add(1, std::memory_order_relaxed);
}

extern "C" DARLING_CG_GUEST_ABI void CGColorSpaceRelease(
	darling_windows_CGColorSpaceRef space)
{
	ReleaseReference(space);
}

extern "C" DARLING_CG_GUEST_ABI int CGColorSpaceGetNumberOfComponents(
	darling_windows_CGColorSpaceRef space)
{
	return space == nullptr ? 0 : space->components;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorRef CGColorCreate(
	darling_windows_CGColorSpaceRef space, const double* components)
{
	if (space == nullptr || components == nullptr)
		return nullptr;
	auto* color = new (std::nothrow) darling_windows_CGColorObject;
	if (color == nullptr) return nullptr;
	color->space = space;
	CGColorSpaceRetain(space);
	if (space->components == 3)
		color->color = {ClampUnit(components[0]), ClampUnit(components[1]),
			ClampUnit(components[2]), ClampUnit(components[3])};
	else if (space->components == 1)
		color->color = {ClampUnit(components[0]), ClampUnit(components[0]),
			ClampUnit(components[0]), ClampUnit(components[1])};
	else {
		CGColorSpaceRelease(space);
		delete color;
		return nullptr;
	}
	return color;
}

extern "C" DARLING_CG_GUEST_ABI void CGColorRetain(darling_windows_CGColorRef color)
{
	if (color != nullptr) color->references.fetch_add(1, std::memory_order_relaxed);
}

extern "C" DARLING_CG_GUEST_ABI void CGColorRelease(darling_windows_CGColorRef color)
{
	if (color != nullptr && color->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		CGColorSpaceRelease(color->space);
		delete color;
	}
}

extern "C" DARLING_CG_GUEST_ABI double CGColorGetAlpha(darling_windows_CGColorRef color)
{
	return color == nullptr ? 0.0 : color->color.alpha;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGContextRef CGBitmapContextCreate(
	void* data, std::size_t width, std::size_t height, std::size_t bits_per_component,
	std::size_t bytes_per_row, darling_windows_CGColorSpaceRef space,
	darling_windows_CGBitmapInfo bitmap_info)
{
	if (space == nullptr || space->components != 3 || width == 0 || height == 0 ||
		bits_per_component != 8 || width > (std::numeric_limits<std::size_t>::max)() / 4)
		return nullptr;
	const auto alpha = bitmap_info & 0x1f;
	const auto byte_order = bitmap_info & 0x7000;
	const bool rgba = (alpha == darling_windows_kCGImageAlphaPremultipliedLast &&
		(byte_order == 0 || byte_order == darling_windows_kCGBitmapByteOrder32Big));
	const bool bgra = (alpha == darling_windows_kCGImageAlphaPremultipliedFirst &&
		byte_order == darling_windows_kCGBitmapByteOrder32Little);
	if (!rgba && !bgra)
		return nullptr;
	const auto minimum_row = width * 4;
	if (bytes_per_row == 0) bytes_per_row = minimum_row;
	if (bytes_per_row < minimum_row ||
		height > (std::numeric_limits<std::size_t>::max)() / bytes_per_row)
		return nullptr;
	auto* context = new (std::nothrow) darling_windows_CGContextObject;
	if (context == nullptr) return nullptr;
	const auto storage_bytes = bytes_per_row * height;
	if (data == nullptr) {
		try { context->owned_data.resize(storage_bytes); }
		catch (...) { delete context; return nullptr; }
		context->data = context->owned_data.data();
	} else {
		context->data = static_cast<std::uint8_t*>(data);
	}
	context->width = width;
	context->height = height;
	context->bytes_per_row = bytes_per_row;
	context->space = space;
	context->bitmap_info = bitmap_info;
	if (bgra) {
		context->red_channel = 2;
		context->green_channel = 1;
		context->blue_channel = 0;
	}
	CGColorSpaceRetain(space);
	return context;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextRetain(darling_windows_CGContextRef context)
{
	if (context != nullptr) context->references.fetch_add(1, std::memory_order_relaxed);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextRelease(darling_windows_CGContextRef context)
{
	if (context != nullptr && context->references.fetch_sub(1,
		std::memory_order_acq_rel) == 1) {
		CGColorSpaceRelease(context->space);
		delete context;
	}
}

extern "C" DARLING_CG_GUEST_ABI void* CGBitmapContextGetData(
	darling_windows_CGContextRef context)
{
	return context == nullptr ? nullptr : context->data;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetWidth(
	darling_windows_CGContextRef context)
{
	return context == nullptr ? 0 : context->width;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetHeight(
	darling_windows_CGContextRef context)
{
	return context == nullptr ? 0 : context->height;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetBytesPerRow(
	darling_windows_CGContextRef context)
{
	return context == nullptr ? 0 : context->bytes_per_row;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef
CGBitmapContextCreateImage(darling_windows_CGContextRef context)
{
	if (context == nullptr || context->data == nullptr || context->space == nullptr)
		return nullptr;
	auto* image = new (std::nothrow) darling_windows_CGImageObject;
	if (image == nullptr) return nullptr;
	try {
		const auto bytes = context->bytes_per_row * context->height;
		image->data.assign(context->data, context->data + bytes);
	} catch (...) {
		delete image;
		return nullptr;
	}
	image->width = context->width;
	image->height = context->height;
	image->bytes_per_row = context->bytes_per_row;
	image->red_channel = context->red_channel;
	image->green_channel = context->green_channel;
	image->blue_channel = context->blue_channel;
	image->alpha_channel = context->alpha_channel;
	image->bitmap_info = context->bitmap_info;
	image->premultiplied = context->premultiplied;
	image->space = context->space;
	CGColorSpaceRetain(image->space);
	return image;
}

extern "C" DARLING_CG_GUEST_ABI void CGImageRetain(darling_windows_CGImageRef image)
{
	if (image != nullptr) image->references.fetch_add(1, std::memory_order_relaxed);
}

extern "C" DARLING_CG_GUEST_ABI void CGImageRelease(darling_windows_CGImageRef image)
{
	if (image != nullptr && image->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		CGColorSpaceRelease(image->space);
		delete image;
	}
}

namespace {
bool CopyProviderBytes(darling_windows_CGDataProviderRef provider,
	std::vector<unsigned char>& bytes)
{
	constexpr auto max_size = static_cast<std::size_t>((std::numeric_limits<DWORD>::max)());
	const auto declared_length = darling_windows_CFDataProviderGetLength(provider);
	if (declared_length < 0 || static_cast<std::uint64_t>(declared_length) > max_size)
		return false;
	if (declared_length == 0 && !darling_windows_CFDataProviderIsSequential(provider))
		return true;
	try {
		std::array<unsigned char, 64 * 1024> chunk{};
		if (declared_length > 0) {
			bytes.resize(static_cast<std::size_t>(declared_length));
			std::size_t offset = 0;
			while (offset < bytes.size()) {
				const auto request = (std::min)(chunk.size(), bytes.size() - offset);
				const auto received = darling_windows_CFDataProviderReadAtPosition(
					provider, bytes.data() + offset, static_cast<std::int64_t>(offset), request);
				if (received == 0 || received > request) return false;
				offset += received;
			}
			return true;
		}
		for (;;) {
			const auto remaining = max_size - bytes.size();
			const auto request = remaining == 0 ? std::size_t{1}
				: (std::min)(remaining, chunk.size());
			const auto received = darling_windows_CFDataProviderReadAtPosition(
				provider, chunk.data(), static_cast<std::int64_t>(bytes.size()), request);
			if (remaining == 0) return received == 0 && !bytes.empty();
			if (received == 0) return !bytes.empty();
			if (received > request) return false;
			bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + received);
		}
	} catch (...) { return false; }
}

bool GetEncoderClass(const std::string& identifier, CLSID& encoder)
{
	if (identifier == "public.png") encoder = CLSID_WICPngEncoder;
	else if (identifier == "public.jpeg" || identifier == "public.jpg") encoder = CLSID_WICJpegEncoder;
	else if (identifier == "public.tiff") encoder = CLSID_WICTiffEncoder;
	else if (identifier == "com.compuserve.gif") encoder = CLSID_WICGifEncoder;
	else if (identifier == "com.microsoft.bmp") encoder = CLSID_WICBmpEncoder;
	else return false;
	return true;
}

bool EncodeImageDestination(const ImageDestinationState& destination,
	std::vector<unsigned char>& encoded)
{
	if (destination.images.empty() || destination.images.size() != destination.expected_images ||
		destination.images.size() > 1 && destination.type_identifier != "public.tiff" &&
		destination.type_identifier != "com.compuserve.gif") return false;
	CLSID encoder_class{};
	if (!GetEncoderClass(destination.type_identifier, encoder_class)) return false;
	ComApartment apartment;
	if (!apartment.ready()) return false;
	try {
		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if (FAILED(result)) return false;
		Microsoft::WRL::ComPtr<IStream> stream;
		result = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
		if (FAILED(result)) return false;
		Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
		result = factory->CreateEncoder(encoder_class, nullptr, &encoder);
		if (FAILED(result)) return false;
		result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		if (FAILED(result)) return false;
		for (const auto* image : destination.images) {
			if (image == nullptr || image->width == 0 || image->height == 0 ||
				image->width > (std::numeric_limits<UINT>::max)() ||
				image->height > (std::numeric_limits<UINT>::max)() ||
				image->width > (std::numeric_limits<std::size_t>::max)() / 4 ||
				image->height > (std::numeric_limits<std::size_t>::max)() / (image->width * 4))
				return false;
			const auto stride = image->width * 4;
			const auto byte_count = stride * image->height;
			if (stride > (std::numeric_limits<UINT>::max)() ||
				byte_count > (std::numeric_limits<UINT>::max)()) return false;
			std::vector<BYTE> top_down(byte_count);
			for (std::size_t y = 0; y < image->height; ++y) {
				const auto source_y = image->height - 1 - y;
				const auto* row = image->data.data() + source_y * image->bytes_per_row;
				auto* target = top_down.data() + y * stride;
				for (std::size_t x = 0; x < image->width; ++x) {
					const auto* pixel = row + x * 4;
					const auto alpha = pixel[image->alpha_channel];
					const auto premultiply = [alpha, image](std::uint8_t value) {
						return image->premultiplied ? value : static_cast<std::uint8_t>(
							(static_cast<unsigned>(value) * alpha + 127) / 255);
					};
				target[x * 4 + 0] = premultiply(pixel[image->blue_channel]);
				target[x * 4 + 1] = premultiply(pixel[image->green_channel]);
				target[x * 4 + 2] = premultiply(pixel[image->red_channel]);
				target[x * 4 + 3] = alpha;
				}
			}
			Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
			result = factory->CreateBitmapFromMemory(static_cast<UINT>(image->width),
				static_cast<UINT>(image->height), GUID_WICPixelFormat32bppPBGRA,
				static_cast<UINT>(stride), static_cast<UINT>(byte_count), top_down.data(), &bitmap);
			if (FAILED(result)) return false;
			Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
			Microsoft::WRL::ComPtr<IPropertyBag2> properties;
			result = encoder->CreateNewFrame(&frame, &properties);
			if (FAILED(result)) return false;
			result = frame->Initialize(properties.Get());
			if (FAILED(result)) return false;
			result = frame->SetSize(static_cast<UINT>(image->width),
				static_cast<UINT>(image->height));
			if (FAILED(result)) return false;
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
			result = frame->SetPixelFormat(&format);
			if (FAILED(result)) return false;
			Microsoft::WRL::ComPtr<IWICBitmapSource> input = bitmap;
			if (!IsEqualGUID(format, GUID_WICPixelFormat32bppPBGRA)) {
				Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
				result = factory->CreateFormatConverter(&converter);
				if (FAILED(result)) return false;
				result = converter->Initialize(bitmap.Get(), format,
					WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
				if (FAILED(result)) return false;
				input = converter;
			}
			result = frame->WriteSource(input.Get(), nullptr);
			if (FAILED(result)) return false;
			result = frame->Commit();
			if (FAILED(result)) return false;
		}
		result = encoder->Commit();
		if (FAILED(result)) return false;
		HGLOBAL global_memory = nullptr;
		result = GetHGlobalFromStream(stream.Get(), &global_memory);
		if (FAILED(result) || global_memory == nullptr) return false;
		STATSTG stream_status{};
		result = stream->Stat(&stream_status, STATFLAG_NONAME);
		if (FAILED(result) || stream_status.cbSize.QuadPart == 0 ||
			stream_status.cbSize.QuadPart > (std::numeric_limits<DWORD>::max)()) return false;
		const auto encoded_size = static_cast<std::size_t>(stream_status.cbSize.QuadPart);
		if (encoded_size > GlobalSize(global_memory)) return false;
		const auto* data = static_cast<const unsigned char*>(GlobalLock(global_memory));
		if (data == nullptr) return false;
		try { encoded.assign(data, data + encoded_size); }
		catch (...) { GlobalUnlock(global_memory); return false; }
		GlobalUnlock(global_memory);
		return true;
	} catch (...) { return false; }
}

bool GetDestinationType(darling_windows_CFStringRef type, std::string& identifier)
{
	if (type == nullptr || darling_windows_CFGetTypeID(type) !=
		darling_windows_CFStringGetTypeID()) return false;
	std::array<char, 256> buffer{};
	if (!darling_windows_CFStringGetCString(type, buffer.data(),
		static_cast<darling_windows_CFIndex>(buffer.size()))) return false;
	identifier = buffer.data();
	CLSID encoder{};
	return GetEncoderClass(identifier, encoder);
}

darling_windows_CFTypeRef CreateImageDestinationObject(ImageDestinationState* state)
{
	if (state == nullptr) return nullptr;
	auto destination = darling_windows_CFImageDestinationCreateOpaque(
		state, &DestroyImageDestinationState);
	if (destination == nullptr) delete state;
	return destination;
}
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef CGDataConsumerCreate(
	void* info, const darling_windows_CGDataConsumerCallbacks* callbacks)
{
	if (callbacks == nullptr || callbacks->put_bytes == nullptr) return nullptr;
	auto* state = new (std::nothrow) DataConsumerState{};
	if (state == nullptr) return nullptr;
	state->mode = DataConsumerMode::Callback;
	state->info = info;
	state->put_bytes = callbacks->put_bytes;
	state->release_info = callbacks->release_consumer;
	return CreateDataConsumerObject(state);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerCreateWithCFData(darling_windows_CFMutableDataRef data)
{
	if (data == nullptr || darling_windows_CFGetTypeID(data) !=
		darling_windows_CFDataGetTypeID()) return nullptr;
	auto* state = new (std::nothrow) DataConsumerState{};
	if (state == nullptr) return nullptr;
	state->mode = DataConsumerMode::MutableData;
	state->data = static_cast<darling_windows_CFDataRef>(darling_windows_CFRetain(data));
	return CreateDataConsumerObject(state);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerCreateWithURL(darling_windows_CFURLRef url)
{
	if (url == nullptr || darling_windows_CFGetTypeID(url) !=
		darling_windows_CFURLGetTypeID()) return nullptr;
	std::array<unsigned char, 32768> path{};
	if (!darling_windows_CFURLGetFileSystemRepresentation(url, true, path.data(),
		static_cast<darling_windows_CFIndex>(path.size()))) return nullptr;
	auto* state = new (std::nothrow) DataConsumerState{};
	if (state == nullptr) return nullptr;
	try {
		state->mode = DataConsumerMode::File;
		state->file.open(std::filesystem::u8path(
			reinterpret_cast<const char*>(path.data())), std::ios::binary | std::ios::trunc);
		if (!state->file) { delete state; return nullptr; }
	} catch (...) { delete state; return nullptr; }
	return CreateDataConsumerObject(state);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerRetain(darling_windows_CFTypeRef consumer)
{
	return AsDataConsumer(consumer) != nullptr ? darling_windows_CFRetain(consumer) : nullptr;
}

extern "C" DARLING_CG_GUEST_ABI void CGDataConsumerRelease(
	darling_windows_CFTypeRef consumer)
{
	if (AsDataConsumer(consumer) != nullptr) darling_windows_CFRelease(consumer);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGDataConsumerGetTypeID()
{
	return darling_windows_CFDataConsumerGetTypeID();
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithDataConsumer(darling_windows_CFTypeRef consumer,
	darling_windows_CFStringRef type, std::size_t count, const void*)
{
	if (AsDataConsumer(consumer) == nullptr || count == 0) return nullptr;
	std::string identifier;
	if (!GetDestinationType(type, identifier)) return nullptr;
	auto* state = new (std::nothrow) ImageDestinationState{};
	if (state == nullptr) return nullptr;
	state->type_identifier = std::move(identifier);
	state->expected_images = count;
	state->consumer = darling_windows_CFRetain(consumer);
	return CreateImageDestinationObject(state);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithCFData(darling_windows_CFDataRef data)
{
	if (data == nullptr || darling_windows_CFGetTypeID(data) !=
		darling_windows_CFDataGetTypeID()) return nullptr;
	return darling_windows_CFDataProviderCreate(darling_windows_CFDataGetBytePtr(data),
		darling_windows_CFDataGetLength(data));
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithData(void* info, const void* data, std::size_t size,
	darling_windows_CGDataProviderReleaseDataCallback release_data)
{
	if (size > static_cast<std::size_t>((std::numeric_limits<darling_windows_CFIndex>::max)()))
		return nullptr;
	return darling_windows_CFDataProviderCreateExternal(info, data,
		static_cast<darling_windows_CFIndex>(size), release_data);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateSequential(void* info,
	const darling_windows_CGDataProviderSequentialCallbacks* callbacks)
{
	if (callbacks == nullptr || callbacks->version != 0 || callbacks->get_bytes == nullptr)
		return nullptr;
	return darling_windows_CFDataProviderCreateSequential(info, callbacks->get_bytes,
		callbacks->skip_forward, callbacks->rewind, callbacks->release_info);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateDirect(void* info, std::int64_t size,
	const darling_windows_CGDataProviderDirectCallbacks* callbacks)
{
	if (callbacks == nullptr || callbacks->version != 0 || size < 0 ||
		(size > (std::numeric_limits<darling_windows_CFIndex>::max)()) ||
		(callbacks->get_byte_pointer == nullptr &&
		 callbacks->get_bytes_at_position == nullptr)) return nullptr;
	return darling_windows_CFDataProviderCreateDirect(info,
		static_cast<darling_windows_CFIndex>(size), callbacks->get_byte_pointer,
		callbacks->release_byte_pointer, callbacks->get_bytes_at_position,
		callbacks->release_info);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithURL(darling_windows_CFURLRef url)
{
	std::vector<unsigned char> bytes;
	if (!ReadLocalFileURL(url, bytes)) return nullptr;
	return darling_windows_CFDataProviderCreate(bytes.data(),
		static_cast<darling_windows_CFIndex>(bytes.size()));
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithFilename(const char* path)
{
	if (path == nullptr) return nullptr;
	auto url = darling_windows_CFURLCreateWithFileSystemPath(path);
	if (url == nullptr) return nullptr;
	auto provider = CGDataProviderCreateWithURL(url);
	darling_windows_CFRelease(url);
	return provider;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderRetain(darling_windows_CGDataProviderRef provider)
{
	if (provider == nullptr || darling_windows_CFGetTypeID(provider) !=
		darling_windows_CFDataProviderGetTypeID()) return nullptr;
	return darling_windows_CFRetain(provider);
}

extern "C" DARLING_CG_GUEST_ABI void CGDataProviderRelease(
	darling_windows_CGDataProviderRef provider)
{
	if (provider != nullptr && darling_windows_CFGetTypeID(provider) ==
		darling_windows_CFDataProviderGetTypeID()) darling_windows_CFRelease(provider);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGDataProviderGetTypeID()
{
	return darling_windows_CFDataProviderGetTypeID();
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGDataProviderGetSize(
	darling_windows_CGDataProviderRef provider)
{
	return provider != nullptr && darling_windows_CFGetTypeID(provider) ==
		darling_windows_CFDataProviderGetTypeID()
		? static_cast<std::size_t>(darling_windows_CFDataProviderGetLength(provider)) : 0;
}

extern "C" DARLING_CG_GUEST_ABI const void* CGDataProviderGetBytePointer(
	darling_windows_CGDataProviderRef provider)
{
	if (provider == nullptr || darling_windows_CFGetTypeID(provider) !=
		darling_windows_CFDataProviderGetTypeID()) return nullptr;
	return darling_windows_CFDataProviderGetBytePtr(provider);
}

extern "C" DARLING_CG_GUEST_ABI void CGDataProviderReleaseBytePointer(
	darling_windows_CGDataProviderRef provider, const void* pointer)
{
	if (provider != nullptr && darling_windows_CFGetTypeID(provider) ==
		darling_windows_CFDataProviderGetTypeID())
		darling_windows_CFDataProviderReleaseBytePointer(provider, pointer);
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGDataProviderGetBytesAtPosition(
	darling_windows_CGDataProviderRef provider, void* buffer, std::int64_t position,
	std::size_t count)
{
	if (provider == nullptr || darling_windows_CFGetTypeID(provider) !=
		darling_windows_CFDataProviderGetTypeID()) return 0;
	return darling_windows_CFDataProviderReadAtPosition(provider, buffer, position, count);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDataRef CGDataProviderCopyData(
	darling_windows_CGDataProviderRef provider)
{
	if (provider == nullptr || darling_windows_CFGetTypeID(provider) !=
		darling_windows_CFDataProviderGetTypeID()) return nullptr;
	std::vector<unsigned char> bytes;
	if (!CopyProviderBytes(provider, bytes) || bytes.size() > static_cast<std::size_t>(
		(std::numeric_limits<darling_windows_CFIndex>::max)())) return nullptr;
	return darling_windows_CFDataCreate(bytes.data(),
		static_cast<darling_windows_CFIndex>(bytes.size()));
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithData(const void* data, const void*)
{
	if (data == nullptr || darling_windows_CFGetTypeID(data) !=
		darling_windows_CFDataGetTypeID()) return nullptr;
	const auto length = darling_windows_CFDataGetLength(data);
	const auto* bytes = darling_windows_CFDataGetBytePtr(data);
	if (length <= 0 || bytes == nullptr) return nullptr;
	auto source = darling_windows_CFImageSourceCreate(bytes, length, true);
	if (source == nullptr || GetBitmapFrameCount(
		darling_windows_CFImageSourceGetBytePtr(source),
		static_cast<std::size_t>(darling_windows_CFImageSourceGetLength(source))) == 0) {
		darling_windows_CFRelease(source);
		return nullptr;
	}
	return source;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithDataProvider(darling_windows_CGDataProviderRef provider, const void*)
{
	if (provider == nullptr || darling_windows_CFGetTypeID(provider) !=
		darling_windows_CFDataProviderGetTypeID()) return nullptr;
	try {
		std::vector<unsigned char> bytes;
		if (!CopyProviderBytes(provider, bytes) || bytes.size() > static_cast<std::size_t>(
			(std::numeric_limits<darling_windows_CFIndex>::max)())) return nullptr;
		auto source = darling_windows_CFImageSourceCreate(bytes.data(),
			static_cast<darling_windows_CFIndex>(bytes.size()), true);
		if (source == nullptr || GetBitmapFrameCount(bytes.data(), bytes.size()) == 0) {
			darling_windows_CFRelease(source);
			return nullptr;
		}
		return source;
	} catch (...) { return nullptr; }
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithData(darling_windows_CFMutableDataRef data,
	darling_windows_CFStringRef type, std::size_t count, const void*)
{
	if (data == nullptr || darling_windows_CFGetTypeID(data) != darling_windows_CFDataGetTypeID() ||
		count == 0) return nullptr;
	auto consumer = CGDataConsumerCreateWithCFData(data);
	if (consumer == nullptr) return nullptr;
	auto destination = CGImageDestinationCreateWithDataConsumer(consumer, type, count, nullptr);
	darling_windows_CFRelease(consumer);
	auto* state = AsImageDestination(destination);
	if (state != nullptr) state->clear_consumer_data_before_write = true;
	return destination;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithURL(darling_windows_CFURLRef url,
	darling_windows_CFStringRef type, std::size_t count, const void*)
{
	if (url == nullptr || darling_windows_CFGetTypeID(url) != darling_windows_CFURLGetTypeID() ||
		count == 0) return nullptr;
	auto consumer = CGDataConsumerCreateWithURL(url);
	if (consumer == nullptr) return nullptr;
	auto destination = CGImageDestinationCreateWithDataConsumer(consumer, type, count, nullptr);
	darling_windows_CFRelease(consumer);
	return destination;
}

extern "C" DARLING_CG_GUEST_ABI void CGImageDestinationAddImage(
	darling_windows_CFTypeRef destination, darling_windows_CGImageRef image, const void*)
{
	auto* state = AsImageDestination(destination);
	if (state == nullptr || state->finalized || image == nullptr ||
		state->images.size() >= state->expected_images) return;
	CGImageRetain(image);
	try { state->images.push_back(image); }
	catch (...) { CGImageRelease(image); }
}

extern "C" DARLING_CG_GUEST_ABI void CGImageDestinationAddImageFromSource(
	darling_windows_CFTypeRef destination, darling_windows_CGImageSourceRef source,
	std::size_t index, const void* properties)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID()) return;
	auto image = CGImageSourceCreateImageAtIndex(source, index, properties);
	if (image == nullptr) return;
	CGImageDestinationAddImage(destination, image, properties);
	CGImageRelease(image);
}

extern "C" DARLING_CG_GUEST_ABI bool CGImageDestinationFinalize(
	darling_windows_CFTypeRef destination)
{
	auto* state = AsImageDestination(destination);
	if (state == nullptr || state->finalized) return false;
	std::vector<unsigned char> encoded;
	if (!EncodeImageDestination(*state, encoded)) return false;
	if (state->clear_consumer_data_before_write &&
		!ClearConsumerData(state->consumer)) return false;
	if (!WriteAllToConsumer(state->consumer, encoded.data(), encoded.size())) return false;
	state->finalized = true;
	return true;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFArrayRef
CGImageDestinationCopyTypeIdentifiers()
{
	const char* identifiers[] = {
		"public.png", "public.jpeg", "public.tiff", "com.compuserve.gif", "com.microsoft.bmp"};
	std::array<darling_windows_CFStringRef, 5> strings{};
	for (std::size_t index = 0; index < strings.size(); ++index) {
		strings[index] = darling_windows_CFStringCreateWithCString(identifiers[index]);
		if (strings[index] == nullptr) {
			for (const auto string : strings) if (string != nullptr) darling_windows_CFRelease(string);
			return nullptr;
		}
	}
	const auto result = darling_windows_CFArrayCreate(strings.data(),
		static_cast<darling_windows_CFIndex>(strings.size()));
	for (const auto string : strings) darling_windows_CFRelease(string);
	return result;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGImageDestinationGetTypeID()
{
	return darling_windows_CFImageDestinationGetTypeID();
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithURL(darling_windows_CFURLRef url, const void*)
{
	try {
		std::vector<unsigned char> bytes;
		if (!ReadLocalFileURL(url, bytes)) return nullptr;
		auto source = darling_windows_CFImageSourceCreate(bytes.data(),
			static_cast<darling_windows_CFIndex>(bytes.size()), true);
		if (source == nullptr || GetBitmapFrameCount(
			darling_windows_CFImageSourceGetBytePtr(source),
			static_cast<std::size_t>(darling_windows_CFImageSourceGetLength(source))) == 0) {
			darling_windows_CFRelease(source);
			return nullptr;
		}
		return source;
	} catch (...) { return nullptr; }
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateIncremental(const void*)
{
	return darling_windows_CFImageSourceCreate(nullptr, 0, false);
}

extern "C" DARLING_CG_GUEST_ABI void CGImageSourceUpdateData(
	darling_windows_CGImageSourceRef source, darling_windows_CFDataRef data, bool is_final)
{
	if (source == nullptr || data == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID() || darling_windows_CFGetTypeID(data) !=
		darling_windows_CFDataGetTypeID()) return;
	darling_windows_CFImageSourceSetData(source, darling_windows_CFDataGetBytePtr(data),
		darling_windows_CFDataGetLength(data), is_final);
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageSourceGetCount(
	darling_windows_CGImageSourceRef source)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID()) return 0;
	const auto length = darling_windows_CFImageSourceGetLength(source);
	return length > 0 ? GetBitmapFrameCount(darling_windows_CFImageSourceGetBytePtr(source),
		static_cast<std::size_t>(length)) : 0;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGImageSourceGetTypeID()
{
	return darling_windows_CFImageSourceGetTypeID();
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceStatus
CGImageSourceGetStatus(darling_windows_CGImageSourceRef source)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID())
		return darling_windows_kCGImageStatusInvalidData;
	if (!darling_windows_CFImageSourceIsFinal(source))
		return darling_windows_kCGImageStatusIncomplete;
	return CGImageSourceGetCount(source) > 0 ? darling_windows_kCGImageStatusComplete :
		darling_windows_kCGImageStatusInvalidData;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceStatus
CGImageSourceGetStatusAtIndex(darling_windows_CGImageSourceRef source, std::size_t index)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID())
		return darling_windows_kCGImageStatusInvalidData;
	const auto length = darling_windows_CFImageSourceGetLength(source);
	if (length <= 0 || index >= CGImageSourceGetCount(source))
		return darling_windows_CFImageSourceIsFinal(source)
			? darling_windows_kCGImageStatusInvalidData : darling_windows_kCGImageStatusIncomplete;
	auto image = DecodeBitmapImage(darling_windows_CFImageSourceGetBytePtr(source),
		static_cast<std::size_t>(length), index);
	if (image == nullptr) return darling_windows_CFImageSourceIsFinal(source)
		? darling_windows_kCGImageStatusInvalidData : darling_windows_kCGImageStatusIncomplete;
	CGImageRelease(image);
	return darling_windows_CFImageSourceIsFinal(source) ? darling_windows_kCGImageStatusComplete :
		darling_windows_kCGImageStatusIncomplete;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFArrayRef
CGImageSourceCopyTypeIdentifiers()
{
	return EnumerateBitmapTypeIdentifiers();
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFStringRef CGImageSourceGetType(
	darling_windows_CGImageSourceRef source)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID()) return nullptr;
	const auto length = darling_windows_CFImageSourceGetLength(source);
	if (length <= 0) return nullptr;
	const auto* type = GetBitmapContainerType(darling_windows_CFImageSourceGetBytePtr(source),
		static_cast<std::size_t>(length));
	if (type == nullptr) return nullptr;
	static const auto png = darling_windows_CFStringCreateWithCString("public.png");
	static const auto jpeg = darling_windows_CFStringCreateWithCString("public.jpeg");
	static const auto gif = darling_windows_CFStringCreateWithCString("com.compuserve.gif");
	static const auto tiff = darling_windows_CFStringCreateWithCString("public.tiff");
	static const auto bmp = darling_windows_CFStringCreateWithCString("com.microsoft.bmp");
	static const auto ico = darling_windows_CFStringCreateWithCString("com.microsoft.ico");
	static const auto wmp = darling_windows_CFStringCreateWithCString("com.microsoft.jpegxr");
	if (std::strcmp(type, "public.png") == 0) return png;
	if (std::strcmp(type, "public.jpeg") == 0) return jpeg;
	if (std::strcmp(type, "com.compuserve.gif") == 0) return gif;
	if (std::strcmp(type, "public.tiff") == 0) return tiff;
	if (std::strcmp(type, "com.microsoft.bmp") == 0) return bmp;
	if (std::strcmp(type, "com.microsoft.ico") == 0) return ico;
	if (std::strcmp(type, "com.microsoft.jpegxr") == 0) return wmp;
	return nullptr;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef CGImageSourceCreateImageAtIndex(
	darling_windows_CGImageSourceRef source, std::size_t index, const void*)
{
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID()) return nullptr;
	const auto length = darling_windows_CFImageSourceGetLength(source);
	const auto* bytes = darling_windows_CFImageSourceGetBytePtr(source);
	if (length <= 0 || bytes == nullptr) return nullptr;
	return index < CGImageSourceGetCount(source)
		? DecodeBitmapImage(bytes, static_cast<std::size_t>(length), index) : nullptr;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef
CGImageSourceCreateThumbnailAtIndex(darling_windows_CGImageSourceRef source,
	std::size_t index, const void* options)
{
	if (options == nullptr || darling_windows_CFGetTypeID(options) !=
		darling_windows_CFDictionaryGetTypeID() || source == nullptr ||
		darling_windows_CFGetTypeID(source) != darling_windows_CFImageSourceGetTypeID() ||
		index >= CGImageSourceGetCount(source)) return nullptr;
	const bool always_create = darling_windows_CFBooleanIsTrue(
		darling_windows_CFDictionaryGetValue(options,
			kCGImageSourceCreateThumbnailFromImageAlways));
	const bool create_if_absent = darling_windows_CFBooleanIsTrue(
		darling_windows_CFDictionaryGetValue(options,
			kCGImageSourceCreateThumbnailFromImageIfAbsent));
	if (!always_create && !create_if_absent) return nullptr;
	std::size_t maximum_dimension = 0;
	const auto* size_value = darling_windows_CFDictionaryGetValue(options,
		kCGImageSourceThumbnailMaxPixelSize);
	if (size_value != nullptr) {
		std::int64_t parsed_size = 0;
		if (darling_windows_CFGetTypeID(size_value) != darling_windows_CFNumberGetTypeID() ||
			!darling_windows_CFNumberGetInteger(size_value, &parsed_size) || parsed_size <= 0)
			return nullptr;
		maximum_dimension = static_cast<std::size_t>(parsed_size);
	}
	const auto length = darling_windows_CFImageSourceGetLength(source);
	if (length <= 0) return nullptr;
	const auto* bytes = darling_windows_CFImageSourceGetBytePtr(source);
	auto image = !always_create && create_if_absent
		? DecodeBitmapImage(bytes, static_cast<std::size_t>(length), index, true) : nullptr;
	if (image == nullptr)
		image = DecodeBitmapImage(bytes, static_cast<std::size_t>(length), index);
	if (image == nullptr) return nullptr;
	if (darling_windows_CFBooleanIsTrue(darling_windows_CFDictionaryGetValue(options,
		kCGImageSourceCreateThumbnailWithTransform))) {
		auto oriented = ApplyExifOrientation(image, GetBitmapExifOrientation(bytes,
			static_cast<std::size_t>(length), index));
		if (oriented != image) CGImageRelease(image);
		image = oriented;
		if (image == nullptr) return nullptr;
	}
	auto thumbnail = ScaleBitmapImage(image, maximum_dimension);
	if (thumbnail != image) CGImageRelease(image);
	return thumbnail;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDictionaryRef
CGImageSourceCopyPropertiesAtIndex(darling_windows_CGImageSourceRef source,
	std::size_t index, const void*)
{
	try {
	if (source == nullptr || darling_windows_CFGetTypeID(source) !=
		darling_windows_CFImageSourceGetTypeID() || index >= CGImageSourceGetCount(source)) return nullptr;
	const auto length = darling_windows_CFImageSourceGetLength(source);
	if (length <= 0) return nullptr;
	std::size_t width = 0;
	std::size_t height = 0;
	if (!GetBitmapFrameSize(darling_windows_CFImageSourceGetBytePtr(source),
		static_cast<std::size_t>(length), index, width, height)) return nullptr;
	using OwnedRef = std::unique_ptr<const void, decltype(&darling_windows_CFRelease)>;
	OwnedRef width_value(darling_windows_CFNumberCreateInteger(static_cast<std::int64_t>(width)),
		darling_windows_CFRelease);
	OwnedRef height_value(darling_windows_CFNumberCreateInteger(static_cast<std::int64_t>(height)),
		darling_windows_CFRelease);
	if (!width_value || !height_value || kCGImagePropertyPixelWidth == nullptr ||
		kCGImagePropertyPixelHeight == nullptr) return nullptr;
	const void* keys[] = {kCGImagePropertyPixelWidth, kCGImagePropertyPixelHeight};
	const void* values[] = {width_value.get(), height_value.get()};
	return darling_windows_CFDictionaryCreate(keys, values, 2);
	} catch (...) { return nullptr; }
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDictionaryRef
CGImageSourceCopyProperties(darling_windows_CGImageSourceRef source, const void*)
{
	try {
		const auto count = CGImageSourceGetCount(source);
		if (count == 0 || kCGImagePropertyImageCount == nullptr) return nullptr;
		using OwnedRef = std::unique_ptr<const void, decltype(&darling_windows_CFRelease)>;
		OwnedRef image_count(darling_windows_CFNumberCreateInteger(
			static_cast<std::int64_t>(count)), darling_windows_CFRelease);
		if (!image_count) return nullptr;
		const void* keys[] = {kCGImagePropertyImageCount};
		const void* values[] = {image_count.get()};
		return darling_windows_CFDictionaryCreate(keys, values, 1);
	} catch (...) { return nullptr; }
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetWidth(darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->width;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetHeight(darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->height;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBytesPerRow(darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->bytes_per_row;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBitsPerComponent(darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->bits_per_component;
}

extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBitsPerPixel(darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->bits_per_pixel;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef CGImageGetColorSpace(
	darling_windows_CGImageRef image)
{
	return image == nullptr ? nullptr : image->space;
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGBitmapInfo CGImageGetBitmapInfo(
	darling_windows_CGImageRef image)
{
	return image == nullptr ? 0 : image->bitmap_info;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextDrawImage(
	darling_windows_CGContextRef context, darling_windows_CGRect rect,
	darling_windows_CGImageRef image)
{
	if (context == nullptr || context->data == nullptr || image == nullptr ||
		!std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y) ||
		!std::isfinite(rect.size.width) || !std::isfinite(rect.size.height) ||
		rect.size.width <= 0 || rect.size.height <= 0 ||
		image->width == 0 || image->height == 0)
		return;
	const auto bounds = TransformBounds(rect, context->ctm);
	const auto x0 = PixelBegin(bounds.origin.x, context->width);
	const auto y0 = PixelBegin(bounds.origin.y, context->height);
	const auto x1 = PixelEnd(bounds.origin.x + bounds.size.width, context->width);
	const auto y1 = PixelEnd(bounds.origin.y + bounds.size.height, context->height);
	bool invertible = false;
	const auto inverse = darling_windows_CGAffineTransformInvert(context->ctm, &invertible);
	if (!invertible) return;
	for (std::size_t y = y0; y < y1; ++y) {
		for (std::size_t x = x0; x < x1; ++x) {
			const double coverage = ClipCoverage(context, x, y) / 255.0;
			if (coverage == 0) continue;
			const auto user = TransformPoint({x + 0.5, y + 0.5}, inverse);
			const double u = (user.x - rect.origin.x) / rect.size.width;
			const double v = (user.y - rect.origin.y) / rect.size.height;
			if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
			const auto source_x = (std::min)(image->width - 1,
				static_cast<std::size_t>(u * image->width));
			const auto source_y = (std::min)(image->height - 1,
				static_cast<std::size_t>(v * image->height));
			const auto source_row = image->height - 1 - source_y;
			const auto* source = image->data.data() + source_row * image->bytes_per_row + source_x * 4;
			const auto target_row = context->height - 1 - y;
			auto* target = context->data + target_row * context->bytes_per_row + x * 4;
			const std::size_t source_channels[] = {
				image->red_channel, image->green_channel, image->blue_channel};
			const double source_alpha_unscaled = source[image->alpha_channel] / 255.0;
			double source_color[3]{};
			if (source_alpha_unscaled > 0)
				for (std::size_t channel = 0; channel < 3; ++channel)
					source_color[channel] = ClampUnit(
						source[source_channels[channel]] / 255.0 / source_alpha_unscaled);
			CompositePixel(context, target, source_color,
				source_alpha_unscaled * context->alpha, coverage);
		}
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetRGBFillColor(
	darling_windows_CGContextRef context, double red, double green, double blue, double alpha)
{
	if (context != nullptr)
		context->fill = {ClampUnit(red), ClampUnit(green), ClampUnit(blue), ClampUnit(alpha)};
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetRGBStrokeColor(
	darling_windows_CGContextRef context, double red, double green, double blue, double alpha)
{
	if (context != nullptr)
		context->stroke = {ClampUnit(red), ClampUnit(green), ClampUnit(blue), ClampUnit(alpha)};
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineWidth(
	darling_windows_CGContextRef context, double width)
{
	if (context != nullptr && std::isfinite(width) && width >= 0)
		context->line_width = width;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineCap(
	darling_windows_CGContextRef context, darling_windows_CGLineCap cap)
{
	if (context != nullptr && cap >= darling_windows_kCGLineCapButt &&
		cap <= darling_windows_kCGLineCapSquare)
		context->line_cap = cap;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineJoin(
	darling_windows_CGContextRef context, darling_windows_CGLineJoin join)
{
	if (context != nullptr && join >= darling_windows_kCGLineJoinMiter &&
		join <= darling_windows_kCGLineJoinBevel)
		context->line_join = join;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetAlpha(
	darling_windows_CGContextRef context, double alpha)
{
	if (context != nullptr && std::isfinite(alpha)) context->alpha = ClampUnit(alpha);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetBlendMode(
	darling_windows_CGContextRef context, darling_windows_CGBlendMode mode)
{
	if (context == nullptr) return;
	switch (mode) {
	case darling_windows_kCGBlendModeNormal:
	case darling_windows_kCGBlendModeMultiply:
	case darling_windows_kCGBlendModeScreen:
	case darling_windows_kCGBlendModeOverlay:
	case darling_windows_kCGBlendModeDarken:
	case darling_windows_kCGBlendModeLighten:
	case darling_windows_kCGBlendModeDifference:
	case darling_windows_kCGBlendModeExclusion:
	case darling_windows_kCGBlendModeClear:
	case darling_windows_kCGBlendModeCopy:
	case darling_windows_kCGBlendModeSourceIn:
	case darling_windows_kCGBlendModeSourceOut:
	case darling_windows_kCGBlendModeSourceAtop:
	case darling_windows_kCGBlendModeDestinationOver:
	case darling_windows_kCGBlendModeDestinationIn:
	case darling_windows_kCGBlendModeDestinationOut:
	case darling_windows_kCGBlendModeDestinationAtop:
	case darling_windows_kCGBlendModeXOR:
		context->blend_mode = mode;
		break;
	default:
		break;
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSaveGState(
	darling_windows_CGContextRef context)
{
	if (context == nullptr) return;
	try {
		context->saved_states.push_back({context->fill, context->stroke,
			context->line_width, context->line_cap, context->line_join,
			context->alpha, context->blend_mode,
			context->ctm, context->clip_mask});
	} catch (...) {
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextRestoreGState(
	darling_windows_CGContextRef context)
{
	if (context == nullptr || context->saved_states.empty()) return;
	auto saved = std::move(context->saved_states.back());
	context->saved_states.pop_back();
	context->fill = saved.fill;
	context->stroke = saved.stroke;
	context->line_width = saved.line_width;
	context->line_cap = saved.line_cap;
	context->line_join = saved.line_join;
	context->alpha = saved.alpha;
	context->blend_mode = saved.blend_mode;
	context->ctm = saved.ctm;
	context->clip_mask = std::move(saved.clip_mask);
}

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGAffineTransform CGContextGetCTM(
	darling_windows_CGContextRef context)
{
	return context == nullptr ? darling_windows_CGAffineTransformIdentity() : context->ctm;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetCTM(
	darling_windows_CGContextRef context, darling_windows_CGAffineTransform transform)
{
	if (context != nullptr && std::isfinite(transform.a) && std::isfinite(transform.b) &&
		std::isfinite(transform.c) && std::isfinite(transform.d) &&
		std::isfinite(transform.tx) && std::isfinite(transform.ty)) context->ctm = transform;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextConcatCTM(
	darling_windows_CGContextRef context, darling_windows_CGAffineTransform transform)
{
	if (context != nullptr)
		context->ctm = darling_windows_CGAffineTransformConcat(transform, context->ctm);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextTranslateCTM(
	darling_windows_CGContextRef context, double tx, double ty)
{
	if (context != nullptr && std::isfinite(tx) && std::isfinite(ty))
		context->ctm = darling_windows_CGAffineTransformConcat(
			darling_windows_CGAffineTransformMakeTranslation(tx, ty), context->ctm);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextScaleCTM(
	darling_windows_CGContextRef context, double sx, double sy)
{
	if (context != nullptr && std::isfinite(sx) && std::isfinite(sy))
		context->ctm = darling_windows_CGAffineTransformConcat(
			darling_windows_CGAffineTransformMakeScale(sx, sy), context->ctm);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextRotateCTM(
	darling_windows_CGContextRef context, double radians)
{
	if (context != nullptr && std::isfinite(radians))
		context->ctm = darling_windows_CGAffineTransformConcat(
			darling_windows_CGAffineTransformMakeRotation(radians), context->ctm);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextSetFillColorWithColor(
	darling_windows_CGContextRef context, darling_windows_CGColorRef color)
{
	if (context != nullptr && color != nullptr) context->fill = color->color;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextFillRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	DrawRect(context, rect, false);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextClearRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	DrawRect(context, rect, true);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextStrokeRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr || !std::isfinite(context->line_width) ||
		context->line_width <= 0 || rect.size.width <= 0 || rect.size.height <= 0)
		return;
	const auto previous = context->fill;
	context->fill = context->stroke;
	const auto width = (std::min)(context->line_width,
		(std::min)(rect.size.width, rect.size.height) / 2.0);
	DrawRect(context, {{rect.origin.x, rect.origin.y}, {rect.size.width, width}}, false);
	DrawRect(context, {{rect.origin.x, rect.origin.y + rect.size.height - width},
		{rect.size.width, width}}, false);
	DrawRect(context, {{rect.origin.x, rect.origin.y + width},
		{width, rect.size.height - 2 * width}}, false);
	DrawRect(context, {{rect.origin.x + rect.size.width - width,
		rect.origin.y + width}, {width, rect.size.height - 2 * width}}, false);
	context->fill = previous;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextBeginPath(
	darling_windows_CGContextRef context)
{
	if (context != nullptr) context->path.clear();
}

extern "C" DARLING_CG_GUEST_ABI void CGContextMoveToPoint(
	darling_windows_CGContextRef context, double x, double y)
{
	if (context == nullptr) return;
	try {
		context->path.push_back({});
		context->path.back().points.push_back({x, y});
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddLineToPoint(
	darling_windows_CGContextRef context, double x, double y)
{
	if (context == nullptr) return;
	try {
		if (context->path.empty() || context->path.back().closed ||
			context->path.back().points.empty()) {
			context->path.push_back({});
			context->path.back().points.push_back({x, y});
		} else {
			context->path.back().points.push_back({x, y});
		}
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextClosePath(
	darling_windows_CGContextRef context)
{
	if (context != nullptr && !context->path.empty() &&
		!context->path.back().points.empty())
		context->path.back().closed = true;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr) return;
	try {
		darling_windows_CGContextObject::Contour contour;
		contour.points = {{rect.origin.x, rect.origin.y},
			{rect.origin.x + rect.size.width, rect.origin.y},
			{rect.origin.x + rect.size.width, rect.origin.y + rect.size.height},
			{rect.origin.x, rect.origin.y + rect.size.height}};
		contour.closed = true;
		context->path.push_back(std::move(contour));
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr || !std::isfinite(rect.origin.x) ||
		!std::isfinite(rect.origin.y) || !std::isfinite(rect.size.width) ||
		!std::isfinite(rect.size.height)) return;
	if (rect.size.width < 0) { rect.origin.x += rect.size.width; rect.size.width = -rect.size.width; }
	if (rect.size.height < 0) { rect.origin.y += rect.size.height; rect.size.height = -rect.size.height; }
	if (rect.size.width == 0 || rect.size.height == 0) return;
	try {
		constexpr std::size_t segments = 64;
		constexpr double tau = 6.283185307179586476925286766559;
		darling_windows_CGContextObject::Contour contour;
		contour.points.reserve(segments);
		const double center_x = rect.origin.x + rect.size.width * 0.5;
		const double center_y = rect.origin.y + rect.size.height * 0.5;
		for (std::size_t index = 0; index < segments; ++index) {
			const double angle = -tau * index / segments;
			contour.points.push_back({center_x + std::cos(angle) * rect.size.width * 0.5,
				center_y + std::sin(angle) * rect.size.height * 0.5});
		}
		contour.closed = true;
		context->path.push_back(std::move(contour));
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddCurveToPoint(
	darling_windows_CGContextRef context, double control1_x, double control1_y,
	double control2_x, double control2_y, double x, double y)
{
	if (context == nullptr || context->path.empty() || context->path.back().closed ||
		context->path.back().points.empty() || !std::isfinite(control1_x) ||
		!std::isfinite(control1_y) || !std::isfinite(control2_x) ||
		!std::isfinite(control2_y) || !std::isfinite(x) || !std::isfinite(y)) return;
	try {
		const auto start = context->path.back().points.back();
		constexpr std::size_t segments = 32;
		for (std::size_t index = 1; index <= segments; ++index) {
			const double t = static_cast<double>(index) / segments;
			const double u = 1 - t;
			const double a = u * u * u;
			const double b = 3 * u * u * t;
			const double c = 3 * u * t * t;
			const double d = t * t * t;
			context->path.back().points.push_back({
				a * start.x + b * control1_x + c * control2_x + d * x,
				a * start.y + b * control1_y + c * control2_y + d * y});
		}
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddQuadCurveToPoint(
	darling_windows_CGContextRef context, double control_x, double control_y,
	double x, double y)
{
	if (context == nullptr || context->path.empty() || context->path.back().closed ||
		context->path.back().points.empty() || !std::isfinite(control_x) ||
		!std::isfinite(control_y) || !std::isfinite(x) || !std::isfinite(y)) return;
	try {
		const auto start = context->path.back().points.back();
		constexpr std::size_t segments = 32;
		for (std::size_t index = 1; index <= segments; ++index) {
			const double t = static_cast<double>(index) / segments;
			const double u = 1 - t;
			context->path.back().points.push_back({
				u * u * start.x + 2 * u * t * control_x + t * t * x,
				u * u * start.y + 2 * u * t * control_y + t * t * y});
		}
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddArc(
	darling_windows_CGContextRef context, double center_x, double center_y,
	double radius, double start_angle, double end_angle, int clockwise)
{
	if (context == nullptr || !std::isfinite(center_x) || !std::isfinite(center_y) ||
		!std::isfinite(radius) || !std::isfinite(start_angle) ||
		!std::isfinite(end_angle) || radius < 0) return;
	constexpr double tau = 6.283185307179586476925286766559;
	double sweep = end_angle - start_angle;
	if (!std::isfinite(sweep)) return;
	if (clockwise) {
		if (sweep > 0) {
			sweep = std::fmod(sweep, tau);
			if (sweep > 0) sweep -= tau;
		} else if (sweep < -tau) {
			sweep = std::fmod(sweep, tau);
			if (sweep == 0) sweep = -tau;
		}
	} else {
		if (sweep < 0) {
			sweep = std::fmod(sweep, tau);
			if (sweep < 0) sweep += tau;
		} else if (sweep > tau) {
			sweep = std::fmod(sweep, tau);
			if (sweep == 0) sweep = tau;
		}
	}
	const darling_windows_CGPoint start{
		center_x + radius * std::cos(start_angle),
		center_y + radius * std::sin(start_angle)};
	if (context->path.empty() || context->path.back().closed ||
		context->path.back().points.empty()) {
		CGContextMoveToPoint(context, start.x, start.y);
	} else {
		const auto current = context->path.back().points.back();
		if (std::hypot(current.x - start.x, current.y - start.y) > 1e-12)
			CGContextAddLineToPoint(context, start.x, start.y);
	}
	if (radius == 0 || sweep == 0) return;
	const double max_step = radius <= 0.25 ? 1.5707963267948966 :
		2 * std::acos((std::max)(-1.0, 1.0 - 0.25 / radius));
	const auto segments = (std::min)(static_cast<std::size_t>(8192),
		(std::max)(std::size_t{1}, static_cast<std::size_t>(std::ceil(
			std::abs(sweep) / (std::isfinite(max_step) && max_step > 0 ? max_step : 0.01)))));
	try {
		context->path.back().points.reserve(context->path.back().points.size() + segments);
		for (std::size_t index = 1; index <= segments; ++index) {
			const double angle = start_angle + sweep * index / segments;
			context->path.back().points.push_back({
				center_x + radius * std::cos(angle),
				center_y + radius * std::sin(angle)});
		}
	} catch (...) {
		context->path.clear();
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextAddArcToPoint(
	darling_windows_CGContextRef context, double x1, double y1,
	double x2, double y2, double radius)
{
	if (context == nullptr || context->path.empty() || context->path.back().closed ||
		context->path.back().points.empty() || !std::isfinite(x1) ||
		!std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2) ||
		!std::isfinite(radius) || radius < 0) return;
	const auto current = context->path.back().points.back();
	const double first_x = current.x - x1;
	const double first_y = current.y - y1;
	const double second_x = x2 - x1;
	const double second_y = y2 - y1;
	const double first_length = std::hypot(first_x, first_y);
	const double second_length = std::hypot(second_x, second_y);
	if (radius == 0 || first_length == 0 || second_length == 0) {
		CGContextAddLineToPoint(context, x1, y1);
		return;
	}
	const double first_unit_x = first_x / first_length;
	const double first_unit_y = first_y / first_length;
	const double second_unit_x = second_x / second_length;
	const double second_unit_y = second_y / second_length;
	const double dot = (std::max)(-1.0, (std::min)(1.0,
		first_unit_x * second_unit_x + first_unit_y * second_unit_y));
	const double half_angle = std::acos(dot) * 0.5;
	const double tangent = std::tan(half_angle);
	const double sine = std::sin(half_angle);
	if (!std::isfinite(tangent) || !std::isfinite(sine) ||
		tangent <= 1e-12 || sine <= 1e-12) {
		CGContextAddLineToPoint(context, x1, y1);
		return;
	}
	const double distance = (std::min)(radius / tangent,
		(std::min)(first_length, second_length));
	const double actual_radius = distance * tangent;
	const double bisector_x = first_unit_x + second_unit_x;
	const double bisector_y = first_unit_y + second_unit_y;
	const double bisector_length = std::hypot(bisector_x, bisector_y);
	if (bisector_length <= 1e-12) {
		CGContextAddLineToPoint(context, x1, y1);
		return;
	}
	const darling_windows_CGPoint tangent1{
		x1 + first_unit_x * distance, y1 + first_unit_y * distance};
	const darling_windows_CGPoint tangent2{
		x1 + second_unit_x * distance, y1 + second_unit_y * distance};
	const darling_windows_CGPoint center{
		x1 + bisector_x / bisector_length * (actual_radius / sine),
		y1 + bisector_y / bisector_length * (actual_radius / sine)};
	const double start = std::atan2(tangent1.y - center.y, tangent1.x - center.x);
	const double end = std::atan2(tangent2.y - center.y, tangent2.x - center.x);
	const double turn = (x1 - current.x) * (y2 - y1) -
		(y1 - current.y) * (x2 - x1);
	CGContextAddLineToPoint(context, tangent1.x, tangent1.y);
	CGContextAddArc(context, center.x, center.y, actual_radius,
		start, end, turn < 0 ? 1 : 0);
	(void)tangent2;
}

extern "C" DARLING_CG_GUEST_ABI void CGContextFillEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr) return;
	auto saved_path = std::move(context->path);
	context->path.clear();
	CGContextAddEllipseInRect(context, rect);
	FillCurrentPath(context, false);
	context->path = std::move(saved_path);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextStrokeEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr) return;
	auto saved_path = std::move(context->path);
	context->path.clear();
	CGContextAddEllipseInRect(context, rect);
	StrokeCurrentPath(context);
	context->path = std::move(saved_path);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextFillPath(
	darling_windows_CGContextRef context)
{
	FillCurrentPath(context, false);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextEOFillPath(
	darling_windows_CGContextRef context)
{
	FillCurrentPath(context, true);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextStrokePath(
	darling_windows_CGContextRef context)
{
	StrokeCurrentPath(context);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextClip(
	darling_windows_CGContextRef context)
{
	ApplyCurrentPathAsClip(context, false);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextEOClip(
	darling_windows_CGContextRef context)
{
	ApplyCurrentPathAsClip(context, true);
}

extern "C" DARLING_CG_GUEST_ABI void CGContextClipToRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect)
{
	if (context == nullptr) return;
	try {
		context->path.clear();
		darling_windows_CGContextObject::Contour contour;
		contour.points = {{rect.origin.x, rect.origin.y},
			{rect.origin.x + rect.size.width, rect.origin.y},
			{rect.origin.x + rect.size.width, rect.origin.y + rect.size.height},
			{rect.origin.x, rect.origin.y + rect.size.height}};
		contour.closed = true;
		context->path.push_back(std::move(contour));
		ApplyCurrentPathAsClip(context, false);
	} catch (...) {
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextClipToMask(
	darling_windows_CGContextRef context, darling_windows_CGRect rect,
	darling_windows_CGImageRef image)
{
	if (context == nullptr || image == nullptr || rect.size.width <= 0 ||
		rect.size.height <= 0 || image->width == 0 || image->height == 0 ||
		!std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y) ||
		!std::isfinite(rect.size.width) || !std::isfinite(rect.size.height) ||
		context->width > (std::numeric_limits<std::size_t>::max)() / context->height)
		return;
	try {
		const auto area = context->width * context->height;
		std::vector<std::uint8_t> mask(area, 0);
		const auto bounds = TransformBounds(rect, context->ctm);
		const auto x0 = PixelBegin(bounds.origin.x, context->width);
		const auto y0 = PixelBegin(bounds.origin.y, context->height);
		const auto x1 = PixelEnd(bounds.origin.x + bounds.size.width, context->width);
		const auto y1 = PixelEnd(bounds.origin.y + bounds.size.height, context->height);
		bool invertible = false;
		const auto inverse = darling_windows_CGAffineTransformInvert(context->ctm, &invertible);
		if (!invertible) { context->path.clear(); return; }
		for (std::size_t y = y0; y < y1; ++y) {
			for (std::size_t x = x0; x < x1; ++x) {
				const auto user = TransformPoint({x + 0.5, y + 0.5}, inverse);
				const double u = (user.x - rect.origin.x) / rect.size.width;
				const double v = (user.y - rect.origin.y) / rect.size.height;
				if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
				const auto source_x = (std::min)(image->width - 1,
					static_cast<std::size_t>(u * image->width));
				const auto source_y = (std::min)(image->height - 1,
					static_cast<std::size_t>(v * image->height));
				const auto source_row = image->height - 1 - source_y;
				const auto* source = image->data.data() + source_row * image->bytes_per_row +
					source_x * 4;
				mask[y * context->width + x] = source[image->alpha_channel];
			}
		}
		if (context->clip_mask.empty()) context->clip_mask = std::move(mask);
		else
			for (std::size_t index = 0; index < area; ++index)
				context->clip_mask[index] = static_cast<std::uint8_t>(
					(context->clip_mask[index] * mask[index] + 127) / 255);
		context->path.clear();
	} catch (...) {
	}
}

extern "C" DARLING_CG_GUEST_ABI void CGContextResetClip(
	darling_windows_CGContextRef context)
{
	if (context != nullptr) context->clip_mask.clear();
}

extern "C" DARLING_CG_GUEST_ABI void CGContextFlush(darling_windows_CGContextRef)
{
}

extern "C" darling_windows_CGPoint darling_windows_CGPointMake(double x, double y)
{
	return {x, y};
}

extern "C" darling_windows_CGSize darling_windows_CGSizeMake(double width, double height)
{
	return {width, height};
}

extern "C" darling_windows_CGRect darling_windows_CGRectMake(double x, double y,
	double width, double height)
{
	return {{x, y}, {width, height}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectStandardize(
	darling_windows_CGRect rect)
{
	if (rect.size.width < 0) { rect.origin.x += rect.size.width; rect.size.width = -rect.size.width; }
	if (rect.size.height < 0) { rect.origin.y += rect.size.height; rect.size.height = -rect.size.height; }
	return rect;
}

extern "C" darling_windows_CGRect darling_windows_CGRectGetBoundingBox(
	darling_windows_CGRect rect)
{
	return darling_windows_CGRectStandardize(rect);
}

extern "C" bool darling_windows_CGRectIsStandardized(
	darling_windows_CGRect rect)
{
	return rect.size.width >= 0.0 && rect.size.height >= 0.0;
}

extern "C" darling_windows_CGVector darling_windows_CGVectorMake(double dx, double dy)
{
	return {dx, dy};
}

extern "C" darling_windows_CGPoint darling_windows_CGPointAdd(
	darling_windows_CGPoint point, darling_windows_CGVector vector)
{
	return {point.x + vector.dx, point.y + vector.dy};
}

extern "C" darling_windows_CGVector darling_windows_CGPointSubtract(
	darling_windows_CGPoint left, darling_windows_CGPoint right)
{
	return {left.x - right.x, left.y - right.y};
}

extern "C" double darling_windows_CGVectorDot(darling_windows_CGVector left,
	darling_windows_CGVector right)
{
	return left.dx * right.dx + left.dy * right.dy;
}

extern "C" double darling_windows_CGVectorLength(darling_windows_CGVector vector)
{
	return std::hypot(vector.dx, vector.dy);
}

extern "C" darling_windows_CGVector darling_windows_CGVectorNormalize(
	darling_windows_CGVector vector, bool* valid)
{
	const double length = darling_windows_CGVectorLength(vector);
	if (length == 0 || !std::isfinite(length)) {
		if (valid != nullptr) *valid = false;
		return {0, 0};
	}
	if (valid != nullptr) *valid = true;
	return {vector.dx / length, vector.dy / length};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMake(
	double a, double b, double c, double d, double tx, double ty)
{
	return {a, b, c, d, tx, ty};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformIdentity()
{
	return {1, 0, 0, 1, 0, 0};
}

extern "C" bool darling_windows_CGAffineTransformIsIdentity(
	darling_windows_CGAffineTransform transform)
{
	return transform.a == 1.0 && transform.b == 0.0 &&
		transform.c == 0.0 && transform.d == 1.0 &&
		transform.tx == 0.0 && transform.ty == 0.0;
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeTranslation(
	double tx, double ty)
{
	return {1, 0, 0, 1, tx, ty};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeScale(
	double sx, double sy)
{
	return {sx, 0, 0, sy, 0, 0};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeRotation(
	double radians)
{
	const double cosine = std::cos(radians);
	const double sine = std::sin(radians);
	return {cosine, sine, -sine, cosine, 0, 0};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformTranslate(
	darling_windows_CGAffineTransform transform, double tx, double ty)
{
	transform.tx += transform.a * tx + transform.c * ty;
	transform.ty += transform.b * tx + transform.d * ty;
	return transform;
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformScale(
	darling_windows_CGAffineTransform transform, double sx, double sy)
{
	transform.a *= sx;
	transform.b *= sx;
	transform.c *= sy;
	transform.d *= sy;
	return transform;
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformRotate(
	darling_windows_CGAffineTransform transform, double radians)
{
	const double cosine = std::cos(radians);
	const double sine = std::sin(radians);
	return darling_windows_CGAffineTransformConcat(transform,
		{cosine, sine, -sine, cosine, 0, 0});
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformConcat(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right)
{
	return {
		left.a * right.a + left.c * right.b,
		left.b * right.a + left.d * right.b,
		left.a * right.c + left.c * right.d,
		left.b * right.c + left.d * right.d,
		left.a * right.tx + left.c * right.ty + left.tx,
		left.b * right.tx + left.d * right.ty + left.ty};
}

extern "C" darling_windows_CGPoint darling_windows_CGPointApplyAffineTransform(
	darling_windows_CGPoint point, darling_windows_CGAffineTransform transform)
{
	return {transform.a * point.x + transform.c * point.y + transform.tx,
		transform.b * point.x + transform.d * point.y + transform.ty};
}

extern "C" darling_windows_CGRect darling_windows_CGRectApplyAffineTransform(
	darling_windows_CGRect rect, darling_windows_CGAffineTransform transform)
{
	const darling_windows_CGPoint corners[] = {
		rect.origin,
		{rect.origin.x + rect.size.width, rect.origin.y},
		{rect.origin.x, rect.origin.y + rect.size.height},
		{rect.origin.x + rect.size.width, rect.origin.y + rect.size.height}};
	const auto first = darling_windows_CGPointApplyAffineTransform(corners[0], transform);
	double min_x = first.x, max_x = first.x, min_y = first.y, max_y = first.y;
	for (int index = 1; index < 4; ++index) {
		const auto point = darling_windows_CGPointApplyAffineTransform(corners[index], transform);
		min_x = (std::min)(min_x, point.x); max_x = (std::max)(max_x, point.x);
		min_y = (std::min)(min_y, point.y); max_y = (std::max)(max_y, point.y);
	}
	return {{min_x, min_y}, {max_x - min_x, max_y - min_y}};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformInvert(
	darling_windows_CGAffineTransform transform, bool* invertible)
{
	const double determinant = transform.a * transform.d - transform.b * transform.c;
	if (!std::isfinite(determinant) || std::abs(determinant) < 1e-15) {
		if (invertible != nullptr) *invertible = false;
		return darling_windows_CGAffineTransformIdentity();
	}
	if (invertible != nullptr) *invertible = true;
	const double inverse = 1.0 / determinant;
	return {transform.d * inverse, -transform.b * inverse, -transform.c * inverse,
		transform.a * inverse,
		(transform.c * transform.ty - transform.d * transform.tx) * inverse,
		(transform.b * transform.tx - transform.a * transform.ty) * inverse};
}

extern "C" bool darling_windows_CGAffineTransformEqualToTransform(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right)
{
	return left.a == right.a && left.b == right.b && left.c == right.c &&
		left.d == right.d && left.tx == right.tx && left.ty == right.ty;
}

extern "C" bool darling_windows_CGRectIsEmpty(darling_windows_CGRect rect)
{
	return rect.size.width <= 0 || rect.size.height <= 0;
}

extern "C" darling_windows_CGRect darling_windows_CGRectNull()
{
	const double null_value = std::numeric_limits<double>::quiet_NaN();
	return {{null_value, null_value}, {null_value, null_value}};
}

extern "C" bool darling_windows_CGRectIsNull(darling_windows_CGRect rect)
{
	return std::isnan(rect.origin.x) && std::isnan(rect.origin.y) &&
		std::isnan(rect.size.width) && std::isnan(rect.size.height);
}

extern "C" bool darling_windows_CGRectIsInfinite(darling_windows_CGRect rect)
{
	return std::isinf(rect.origin.x) || std::isinf(rect.origin.y) ||
		std::isinf(rect.size.width) || std::isinf(rect.size.height);
}

extern "C" darling_windows_CGRect darling_windows_CGRectIntegral(
	darling_windows_CGRect rect)
{
	const double min_x = std::floor(rect.origin.x);
	const double min_y = std::floor(rect.origin.y);
	const double max_x = std::ceil(rect.origin.x + rect.size.width);
	const double max_y = std::ceil(rect.origin.y + rect.size.height);
	return {{min_x, min_y}, {max_x - min_x, max_y - min_y}};
}

extern "C" bool darling_windows_CGRectContainsPoint(darling_windows_CGRect rect,
	darling_windows_CGPoint point)
{
	return !darling_windows_CGRectIsEmpty(rect) && point.x >= rect.origin.x &&
	point.y >= rect.origin.y && point.x <= rect.origin.x + rect.size.width &&
	point.y <= rect.origin.y + rect.size.height;
}

extern "C" bool darling_windows_CGRectContainsRect(darling_windows_CGRect rect,
	darling_windows_CGRect candidate)
{
	return darling_windows_CGRectContainsPoint(rect, candidate.origin) &&
		darling_windows_CGRectContainsPoint(rect,
		{candidate.origin.x + candidate.size.width,
		candidate.origin.y + candidate.size.height});
}

extern "C" darling_windows_CGRect darling_windows_CGRectIntersection(
	darling_windows_CGRect left, darling_windows_CGRect right)
{
	const double x1 = (std::max)(left.origin.x, right.origin.x);
	const double y1 = (std::max)(left.origin.y, right.origin.y);
	const double x2 = (std::min)(left.origin.x + left.size.width,
		right.origin.x + right.size.width);
	const double y2 = (std::min)(left.origin.y + left.size.height,
		right.origin.y + right.size.height);
	if (x2 <= x1 || y2 <= y1) return {{0, 0}, {0, 0}};
	return {{x1, y1}, {x2 - x1, y2 - y1}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectUnion(
	darling_windows_CGRect left, darling_windows_CGRect right)
{
	if (darling_windows_CGRectIsEmpty(left)) return right;
	if (darling_windows_CGRectIsEmpty(right)) return left;
	const double x1 = (std::min)(left.origin.x, right.origin.x);
	const double y1 = (std::min)(left.origin.y, right.origin.y);
	const double x2 = (std::max)(left.origin.x + left.size.width,
		right.origin.x + right.size.width);
	const double y2 = (std::max)(left.origin.y + left.size.height,
		right.origin.y + right.size.height);
	return {{x1, y1}, {x2 - x1, y2 - y1}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectInset(
	darling_windows_CGRect rect, double dx, double dy)
{
	return {{rect.origin.x + dx, rect.origin.y + dy},
		{rect.size.width - 2 * dx, rect.size.height - 2 * dy}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectOffset(
	darling_windows_CGRect rect, double dx, double dy)
{
	rect.origin.x += dx;
	rect.origin.y += dy;
	return rect;
}

extern "C" double darling_windows_CGRectGetMinX(darling_windows_CGRect rect) { return rect.origin.x; }
extern "C" double darling_windows_CGRectGetMidX(darling_windows_CGRect rect)
{ return rect.origin.x + rect.size.width / 2; }
extern "C" double darling_windows_CGRectGetMaxX(darling_windows_CGRect rect)
{ return rect.origin.x + rect.size.width; }
extern "C" double darling_windows_CGRectGetMinY(darling_windows_CGRect rect) { return rect.origin.y; }
extern "C" double darling_windows_CGRectGetMidY(darling_windows_CGRect rect)
{ return rect.origin.y + rect.size.height / 2; }
extern "C" double darling_windows_CGRectGetMaxY(darling_windows_CGRect rect)
{ return rect.origin.y + rect.size.height; }
extern "C" double darling_windows_CGRectGetWidth(darling_windows_CGRect rect)
{ return rect.size.width; }
extern "C" double darling_windows_CGRectGetHeight(darling_windows_CGRect rect)
{ return rect.size.height; }
extern "C" bool darling_windows_CGPointEqualToPoint(darling_windows_CGPoint left,
	darling_windows_CGPoint right)
{ return left.x == right.x && left.y == right.y; }
extern "C" bool darling_windows_CGSizeEqualToSize(darling_windows_CGSize left,
	darling_windows_CGSize right)
{ return left.width == right.width && left.height == right.height; }
extern "C" bool darling_windows_CGRectEqualToRect(darling_windows_CGRect left,
	darling_windows_CGRect right)
{ return darling_windows_CGPointEqualToPoint(left.origin, right.origin) &&
	darling_windows_CGSizeEqualToSize(left.size, right.size); }

extern "C" void darling_windows_CGRectDivide(darling_windows_CGRect rect,
	double amount, darling_windows_CGRectEdge edge, darling_windows_CGRect* slice,
	darling_windows_CGRect* remainder)
{
	if (slice == nullptr && remainder == nullptr) return;
	const double clamped = (std::max)(0.0, amount);
	if (slice != nullptr) *slice = {{0, 0}, {0, 0}};
	if (remainder != nullptr) *remainder = rect;
	switch (edge) {
	case darling_windows_CGMinXEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.width));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y},
			{used, rect.size.height}};
		if (remainder != nullptr) { remainder->origin.x += used; remainder->size.width -= used; }
		}
		break;
	case darling_windows_CGMinYEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.height));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y},
			{rect.size.width, used}};
		if (remainder != nullptr) { remainder->origin.y += used; remainder->size.height -= used; }
		}
		break;
	case darling_windows_CGMaxXEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.width));
		if (slice != nullptr) *slice = {{rect.origin.x + rect.size.width - clamped, rect.origin.y},
			{used, rect.size.height}};
		if (slice != nullptr) slice->origin.x = rect.origin.x + rect.size.width - used;
		if (remainder != nullptr) remainder->size.width -= used;
		}
		break;
	case darling_windows_CGMaxYEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.height));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y + rect.size.height - clamped},
			{rect.size.width, used}};
		if (slice != nullptr) slice->origin.y = rect.origin.y + rect.size.height - used;
		if (remainder != nullptr) remainder->size.height -= used;
		}
		break;
	}
	if (remainder != nullptr) {
		if (remainder->size.width < 0) remainder->size.width = 0;
		if (remainder->size.height < 0) remainder->size.height = 0;
	}
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeRGBA(
	double red, double green, double blue, double alpha)
{
	return {red, green, blue, alpha};
}

extern "C" double darling_windows_CGColorGetAlpha(darling_windows_CGColor color)
{
	return color.alpha;
}

extern "C" bool darling_windows_CGColorGetRed(darling_windows_CGColor color,
	double* value)
{
	if (value == nullptr) return false;
	*value = color.red;
	return true;
}

extern "C" bool darling_windows_CGColorGetGreen(darling_windows_CGColor color,
	double* value)
{
	if (value == nullptr) return false;
	*value = color.green;
	return true;
}

extern "C" bool darling_windows_CGColorGetBlue(darling_windows_CGColor color,
	double* value)
{
	if (value == nullptr) return false;
	*value = color.blue;
	return true;
}

extern "C" bool darling_windows_CGColorGetWhite(darling_windows_CGColor color,
	double* value)
{
	if (value == nullptr || color.red != color.green || color.green != color.blue)
		return false;
	*value = color.red;
	return true;
}

extern "C" int darling_windows_CGColorGetNumberOfComponents(
	darling_windows_CGColor color)
{
	return color.red == color.green && color.green == color.blue ? 2 : 4;
}

extern "C" bool darling_windows_CGColorGetComponents(darling_windows_CGColor color,
	double* components, int capacity)
{
	if (components == nullptr) return false;
	if (color.red == color.green && color.green == color.blue) {
		if (capacity < 2) return false;
		components[0] = color.red;
		components[1] = color.alpha;
		return true;
	}
	if (capacity < 4) return false;
	components[0] = color.red;
	components[1] = color.green;
	components[2] = color.blue;
	components[3] = color.alpha;
	return true;
}

extern "C" bool darling_windows_CGColorEqual(darling_windows_CGColor left,
	darling_windows_CGColor right)
{
	return left.red == right.red && left.green == right.green &&
		left.blue == right.blue && left.alpha == right.alpha;
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeGray(double white, double alpha)
{
	return {white, white, white, alpha};
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeWhite(double alpha)
{
	return darling_windows_CGColorMakeGray(1, alpha);
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeClear()
{
	return {0, 0, 0, 0};
}
