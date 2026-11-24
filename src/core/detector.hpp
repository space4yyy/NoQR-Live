#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace qr {
struct Rect { int x, y, width, height; };
struct Detection {
    bool ok = false;
    std::vector<Rect> boxes;
    std::string error;
};
// Pixels use top-left origin, tightly packed RGBA8. Payloads are never retained.
Detection detect(const uint8_t *rgba, uint32_t width, uint32_t height);
std::vector<Rect> maskRects(uint32_t width, uint32_t height,
                            const std::vector<Rect> &boxes, int padding);
void redact(uint8_t *rgba, uint32_t width, uint32_t height,
            const std::vector<Rect> &boxes, int padding);
void blacken(uint8_t *rgba, uint32_t width, uint32_t height);
}
