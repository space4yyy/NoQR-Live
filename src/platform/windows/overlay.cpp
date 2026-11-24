#include "overlay.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <utility>

namespace qr {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT MaxOverlayEdge = 1024;

struct ComApartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

std::wstring widePath(const std::string &path)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (!size) return {};
    std::wstring converted(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, converted.data(), size)) return {};
    converted.pop_back();
    return converted;
}

WICBitmapTransformOptions orientation(IWICBitmapFrameDecode *frame)
{
    ComPtr<IWICMetadataQueryReader> metadata;
    if (FAILED(frame->GetMetadataQueryReader(metadata.GetAddressOf()))) return WICBitmapTransformRotate0;

    PROPVARIANT value{};
    HRESULT status = metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
    if (FAILED(status)) status = metadata->GetMetadataByName(L"/ifd/{ushort=274}", &value);
    const auto exif = SUCCEEDED(status) && value.vt == VT_UI2 ? value.uiVal : 1;
    PropVariantClear(&value);
    switch (exif) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return WICBitmapTransformOptions(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return WICBitmapTransformOptions(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}
}

Overlay loadOverlay(const std::string &path)
{
    if (path.empty()) return {};
    const auto filename = widePath(path);
    if (filename.empty()) return {0, 0, {}, "Invalid image path"};

    ComApartment apartment;
    if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
        return {0, 0, {}, "Cannot initialize COM"};

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(factory.GetAddressOf()))))
        return {0, 0, {}, "Cannot initialize Windows Imaging Component"};

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())))
        return {0, 0, {}, "Cannot open overlay image"};

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return {0, 0, {}, "Cannot decode overlay image"};
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height)
        return {0, 0, {}, "Invalid overlay dimensions"};

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0, WICBitmapPaletteTypeCustom)))
        return {0, 0, {}, "Cannot convert overlay pixels"};
    ComPtr<IWICBitmapSource> image = converter;

    const UINT edge = std::max(width, height);
    if (edge > MaxOverlayEdge) {
        const UINT scaledWidth = std::max(1u, UINT(uint64_t(width) * MaxOverlayEdge / edge));
        const UINT scaledHeight = std::max(1u, UINT(uint64_t(height) * MaxOverlayEdge / edge));
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(image.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
            return {0, 0, {}, "Cannot resize overlay image"};
        image = scaler;
    }

    const auto transform = orientation(frame.Get());
    if (transform != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (FAILED(factory->CreateBitmapFlipRotator(rotator.GetAddressOf())) ||
            FAILED(rotator->Initialize(image.Get(), transform)))
            return {0, 0, {}, "Cannot orient overlay image"};
        image = rotator;
    }

    if (FAILED(image->GetSize(&width, &height))) return {0, 0, {}, "Cannot read overlay dimensions"};
    Overlay result{width, height, std::vector<uint8_t>(size_t(width) * height * 4), {}};
    if (FAILED(image->CopyPixels(nullptr, width * 4, UINT(result.pixels.size()), result.pixels.data())))
        return {0, 0, {}, "Cannot read overlay pixels"};
    for (size_t offset = 0; offset < result.pixels.size(); offset += 4) {
        std::swap(result.pixels[offset], result.pixels[offset + 2]);
        result.pixels[offset + 3] = 255; // Premultiplied channels are already composited over black.
    }
    return result;
}
}
