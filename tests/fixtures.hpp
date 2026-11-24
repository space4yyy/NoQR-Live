#pragma once
#import <CoreImage/CoreImage.h>
#include "detector.hpp"
#include <stdexcept>

namespace fixture {
inline std::vector<uint8_t> background(int width, int height)
{
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        pixels[4*i] = 30; pixels[4*i+1] = 90; pixels[4*i+2] = 150; pixels[4*i+3] = 255;
    }
    return pixels;
}
inline qr::Rect place(std::vector<uint8_t> &pixels, int width, int height, int x, int y, int scale = 5)
{
    CIFilter *filter = [CIFilter filterWithName:@"CIQRCodeGenerator"];
    [filter setValue:[@"obs-qrcode-test-offline" dataUsingEncoding:NSUTF8StringEncoding] forKey:@"inputMessage"];
    [filter setValue:@"H" forKey:@"inputCorrectionLevel"];
    CIImage *image = [filter.outputImage imageByApplyingTransform:CGAffineTransformMakeScale(scale, scale)];
    const int side = int(image.extent.size.width);
    const int border = 4 * scale;
    const int total = side + border * 2;
    if (x < 0 || y < 0 || x + total > width || y + total > height) throw std::runtime_error("Fixture outside frame");
    std::vector<uint8_t> bitmap(size_t(side) * side * 4);
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    [[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer: @YES}]
        render:image toBitmap:bitmap.data() rowBytes:side * 4 bounds:image.extent format:kCIFormatRGBA8 colorSpace:color];
    CGColorSpaceRelease(color);
    for (int j = 0; j < total; ++j) {
        for (int i = 0; i < total; ++i) {
            auto *p = pixels.data() + (size_t(y + j) * width + x + i) * 4;
            uint8_t value = 255;
            if (i >= border && j >= border && i < side + border && j < side + border)
                value = bitmap[(size_t(j - border) * side + i - border) * 4];
            p[0] = p[1] = p[2] = value; p[3] = 255;
        }
    }
    return {x + border, y + border, side, side};
}
}
