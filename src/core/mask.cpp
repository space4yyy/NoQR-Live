#include "detector.hpp"
#include <algorithm>
#include <cmath>

namespace qr {
std::vector<Rect> maskRects(uint32_t width, uint32_t height,
                            const std::vector<Rect> &boxes, int padding)
{
    std::vector<Rect> masks;
    for (const auto &box : boxes) {
        // Include the quiet zone and give moving/antialiased edges extra coverage.
        const int margin = std::max(padding, int(std::ceil(std::max(box.width, box.height) * 0.12)));
        const int x0 = std::clamp(box.x - margin, 0, int(width));
        const int y0 = std::clamp(box.y - margin, 0, int(height));
        const int x1 = std::clamp(box.x + box.width + margin, 0, int(width));
        const int y1 = std::clamp(box.y + box.height + margin, 0, int(height));
        if (x1 > x0 && y1 > y0) masks.push_back({x0, y0, x1 - x0, y1 - y0});
    }
    return masks;
}

void redact(uint8_t *rgba, uint32_t width, uint32_t height,
            const std::vector<Rect> &boxes, int padding)
{
    for (const auto &box : maskRects(width, height, boxes, padding)) {
        for (int y = box.y; y < box.y + box.height; ++y) {
            uint8_t *pixel = rgba + (size_t(y) * width + box.x) * 4;
            for (int x = 0; x < box.width; ++x, pixel += 4) {
                pixel[0] = pixel[1] = pixel[2] = 0;
                pixel[3] = 255;
            }
        }
    }
}

void blacken(uint8_t *rgba, uint32_t width, uint32_t height)
{
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 0;
        rgba[4 * i + 3] = 255;
    }
}
}
