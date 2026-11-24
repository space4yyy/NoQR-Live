// Test-only replacement, linked into a separate module, never into the installed plugin.
#include "detector.hpp"
namespace qr {
Detection detect(const uint8_t *, uint32_t, uint32_t)
{
    return {false, {}, "Injected Vision error (test-only)"};
}
}
