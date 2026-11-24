#include "fixtures.hpp"
#include "geometry.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static void verify(int width, int height, const std::vector<qr::Rect> &positions)
{
    auto pixels = fixture::background(width, height);
    std::vector<qr::Rect> expected;
    for (const auto &p : positions) expected.push_back(fixture::place(pixels, width, height, p.x, p.y, p.width));
    auto start = std::chrono::steady_clock::now();
    auto result = qr::detect(pixels.data(), width, height);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    require(result.ok, result.error.c_str());
    require(result.boxes.size() == expected.size(), "Incorrect QR count");
    for (const auto &box : expected) {
        bool matched = false;
        for (const auto &actual : result.boxes)
            if (std::abs(actual.x - box.x) <= 5 && std::abs(actual.y - box.y) <= 5 &&
                std::abs(actual.width - box.width) <= 12 && std::abs(actual.height - box.height) <= 12) matched = true;
        if (!matched) {
            std::cerr << "Expected " << box.x << "," << box.y << " " << box.width << "x" << box.height << "\n";
            for (const auto &actual : result.boxes)
                std::cerr << "Actual " << actual.x << "," << actual.y << " " << actual.width << "x" << actual.height << "\n";
        }
        require(matched, "QR coordinate or vertical orientation mismatch");
    }
    qr::redact(pixels.data(), width, height, result.boxes, 16);
    for (const auto &box : expected)
        for (int y = box.y; y < box.y + box.height; ++y)
            for (int x = box.x; x < box.x + box.width; ++x) {
                const auto *p = pixels.data() + (size_t(y) * width + x) * 4;
                require(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255, "QR not fully opaque black");
            }
    auto after = qr::detect(pixels.data(), width, height);
    require(after.ok && after.boxes.empty(), "QR still detectable after redaction");
    std::cout << "PASS " << width << "x" << height << " QR=" << expected.size() << " detect=" << ms << "ms\n";
}
int main()
{
    @autoreleasepool {
        try {
            require(!qr::detect(nullptr, 0, 0).ok, "Invalid input accepted");
            verify(640, 480, {});
            verify(640, 480, {{5, 7, 4, 0}});
            verify(640, 480, {{470, 305, 4, 0}});
            verify(1920, 1080, {{60, 55, 5, 0}, {1610, 810, 5, 0}});
            verify(1920, 1080, {{900, 400, 2, 0}});
            verify(3840, 2160, {{3000, 1500, 5, 0}});
            // Reusing a request must not retain detections from the prior image.
            verify(640, 480, {});
            const auto unchanged = qr::detectionSize(640, 480, 1280);
            require(unchanged.width == 640 && unchanged.height == 480, "Small input upscaled");
            const auto scaled = qr::detectionSize(1919, 1079, 1280);
            require(scaled.width == 1280 && scaled.height == 720, "Aspect ratio rounding wrong");
            const auto portrait = qr::detectionSize(1080, 1920, 1280);
            require(portrait.width == 720 && portrait.height == 1280, "Portrait resize wrong");
            auto mapped = qr::sourceBoxes({{10, 20, 7, 9}, {1279, 719, 1, 1}}, scaled, {1919, 1079});
            require(mapped[0].x == 14 && mapped[0].y == 29 && mapped[0].width == 12 && mapped[0].height == 15,
                    "Scaled box does not round outwards");
            require(mapped[1].x == 1917 && mapped[1].y == 1077 && mapped[1].width == 2 && mapped[1].height == 2,
                    "Bottom-right edge mapping wrong");
            auto small = fixture::background(8, 8);
            qr::redact(small.data(), 8, 8, {{-10, -10, 15, 15}, {7, 7, 50, 50}}, 16);
            qr::blacken(small.data(), 8, 8);
            for (size_t i = 0; i < 64; ++i)
                require(small[i*4] == 0 && small[i*4+3] == 255, "Failure frame not opaque");
        } catch (const std::exception &e) {
            std::cerr << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }
}
