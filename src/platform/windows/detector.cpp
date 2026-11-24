#include "detector.hpp"
#include <ImageView.h>
#include <ReadBarcode.h>
#include <algorithm>
#include <exception>
#include <limits>

namespace qr {
Detection detect(const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (!rgba || !width || !height || width > std::numeric_limits<int>::max() / 4u ||
        height > uint32_t(std::numeric_limits<int>::max())) {
        return {false, {}, "Invalid image"};
    }

    try {
        const ZXing::ImageView frame(rgba, int(width), int(height), ZXing::ImageFormat::RGBA);
        const auto options = ZXing::ReaderOptions()
            .setFormats(ZXing::BarcodeFormat::QRCode)
            .setTryDownscale(false)
            .setReturnErrors(true);
        Detection result;
        for (const auto &barcode : ZXing::ReadBarcodes(frame, options)) {
            const auto &corners = barcode.position();
            const auto [left, right] = std::minmax_element(corners.begin(), corners.end(),
                [](auto a, auto b) { return a.x < b.x; });
            const auto [top, bottom] = std::minmax_element(corners.begin(), corners.end(),
                [](auto a, auto b) { return a.y < b.y; });
            // Include both endpoint pixels. The shared masking code applies padding and clips to the frame.
            if (right->x > left->x && bottom->y > top->y) {
                result.boxes.push_back({left->x, top->y, right->x - left->x + 1, bottom->y - top->y + 1});
            }
        }
        result.ok = true;
        return result;
    } catch (const std::exception &error) {
        return {false, {}, error.what()};
    }
}
}
