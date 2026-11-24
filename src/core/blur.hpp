#pragma once
#include <algorithm>
#include <cstdint>
namespace qr {
enum class MaskMode : int { Black = 0, Blur = 1, Image = 2 };
constexpr int MinimumBlurStrength = 60;
inline int blurStrength(int value) { return std::clamp(value, MinimumBlurStrength, 100); }
// Standard deviation relative to the protected rectangle, on each axis.
inline float blurFraction(int strength) { return 0.14f + (blurStrength(strength) - 60) * 0.004f; }
constexpr uint32_t BlurPatchSize = 64;
}
