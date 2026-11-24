#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace qr {
struct Overlay {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
    std::string error;
};
// Decode once off the graphics thread. Alpha is composited onto opaque black.
Overlay loadOverlay(const std::string &path);
}
