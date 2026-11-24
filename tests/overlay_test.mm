#include "overlay_fixture.hpp"
#include "overlay.hpp"
#include <iostream>

int main()
{
    @autoreleasepool {
        try {
            auto empty = qr::loadOverlay("");
            if (!empty.pixels.empty() || !empty.error.empty()) return 1;
            auto missing = qr::loadOverlay("build/does-not-exist.png");
            if (missing.error.empty() || !missing.pixels.empty()) return 2;
            makeOverlayFixture("build/overlay-test.png");
            auto image = qr::loadOverlay("build/overlay-test.png");
            if (!image.error.empty() || image.width != 80 || image.height != 40) return 3;
            for (size_t i = 0; i < size_t(image.width) * image.height; ++i)
                if (image.pixels[i * 4 + 3] != 255) return 4;
            const auto *top = image.pixels.data() + (size_t(5) * image.width + 5) * 4;
            const auto *bottom = image.pixels.data() + (size_t(35) * image.width + 5) * 4;
            const auto *transparent = image.pixels.data() + (size_t(5) * image.width + 65) * 4;
            if (top[0] < 200 || top[2] > 40 || bottom[2] < 200 || bottom[0] > 40) {
                std::cerr << "Overlay image orientation wrong\n"; return 5;
            }
            if (transparent[0] || transparent[1] || transparent[2]) return 6;
            makeOverlayFixture("build/overlay-large.png", 2048, 1024);
            auto large = qr::loadOverlay("build/overlay-large.png");
            if (large.width != 1024 || large.height != 512) return 7;
            std::cout << "PASS overlay: orientation, opaque alpha, transparent black, missing file, size limit\n";
        } catch (const std::exception &error) { std::cerr << error.what() << "\n"; return 8; }
    }
}
