#pragma once
#include "detector.hpp"
#include <algorithm>
#include <cmath>

namespace qr {
struct Size { uint32_t width, height; };
inline Size detectionSize(uint32_t width, uint32_t height, uint32_t maxEdge)
{
    if (!width || !height) return {0, 0};
    if (!maxEdge || std::max(width, height) <= maxEdge) return {width, height};
    const double scale = double(maxEdge) / std::max(width, height);
    return {std::max(1u, uint32_t(std::lround(width * scale))),
            std::max(1u, uint32_t(std::lround(height * scale)))};
}

inline std::vector<Rect> sourceBoxes(const std::vector<Rect> &boxes, Size detection, Size source)
{
    std::vector<Rect> mapped;
    if (!detection.width || !detection.height || !source.width || !source.height) return mapped;
    const double sx = double(source.width) / detection.width;
    const double sy = double(source.height) / detection.height;
    for (const auto &box : boxes) {
        // Round outwards independently on each axis, including odd-sized sources.
        const int x0 = int(std::floor(box.x * sx));
        const int y0 = int(std::floor(box.y * sy));
        const int x1 = int(std::ceil((double(box.x) + box.width) * sx));
        const int y1 = int(std::ceil((double(box.y) + box.height) * sy));
        mapped.push_back({x0, y0, x1 - x0, y1 - y0});
    }
    return mapped;
}
}
