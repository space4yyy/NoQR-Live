#include "fixtures.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>

int main()
{
    @autoreleasepool {
        auto original = fixture::background(1920, 1080);
        fixture::place(original, 1920, 1080, 15, 18, 5);
        fixture::place(original, 1920, 1080, 1760, 925, 3);
        // Benchmark Vision alone: no OBS, readback, render or CPU-resize cost included.
        for (int edge : {1920, 1280, 960}) {
            int height = edge * 9 / 16;
            std::vector<uint8_t> pixels(size_t(edge) * height * 4);
            CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
            CIImage *image = [CIImage imageWithBitmapData:[NSData dataWithBytes:original.data() length:original.size()]
                bytesPerRow:1920 * 4 size:CGSizeMake(1920, 1080) format:kCIFormatRGBA8 colorSpace:color];
            image = [image imageByApplyingTransform:CGAffineTransformMakeScale(double(edge) / 1920, double(edge) / 1920)];
            [[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer: @YES}] render:image toBitmap:pixels.data()
                rowBytes:edge * 4 bounds:CGRectMake(0, 0, edge, height) format:kCIFormatRGBA8 colorSpace:color];
            CGColorSpaceRelease(color);
            std::vector<double> samples;
            size_t found = 0;
            for (int i = 0; i < 12; ++i) {
                auto start = std::chrono::steady_clock::now();
                auto result = qr::detect(pixels.data(), edge, height);
                if (!result.ok) { std::cerr << result.error << "\n"; return 1; }
                found = result.boxes.size();
                if (i >= 2) samples.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count());
            }
            std::sort(samples.begin(), samples.end());
            std::cout << edge << "x" << height << " median_ms=" << std::fixed << std::setprecision(2)
                << (samples[4] + samples[5]) / 2 << " p90_ms=" << samples[8] << " detected=" << found << "/2\n";
        }
    }
}
