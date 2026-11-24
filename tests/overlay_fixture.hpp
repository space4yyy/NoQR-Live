#pragma once
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <string>
#include <vector>
#include <stdexcept>

inline void writeRGBA(const std::string &path, const uint8_t *pixels, uint32_t width, uint32_t height)
{
    auto color = CGColorSpaceCreateDeviceRGB();
    auto provider = CGDataProviderCreateWithData(nullptr, pixels, size_t(width) * height * 4, nullptr);
    auto image = CGImageCreate(width, height, 8, 32, width * 4, color,
        kCGBitmapByteOrder32Big | kCGImageAlphaLast, provider, nullptr, false, kCGRenderingIntentDefault);
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    auto destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    if (!destination || !image) throw std::runtime_error("Cannot create PNG");
    CGImageDestinationAddImage(destination, image, nullptr);
    bool ok = CGImageDestinationFinalize(destination);
    CFRelease(destination); CGImageRelease(image); CGDataProviderRelease(provider); CGColorSpaceRelease(color);
    if (!ok) throw std::runtime_error("Cannot write PNG");
}

inline void makeOverlayFixture(const std::string &path, uint32_t width = 80, uint32_t height = 40)
{
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            auto *p = pixels.data() + (size_t(y) * width + x) * 4;
            p[0] = x >= width / 2 ? 255 : y < height / 2 ? 220 : 20;
            p[1] = x >= width / 2 ? 255 : 40;
            p[2] = x >= width / 2 ? 255 : y < height / 2 ? 20 : 220;
            p[3] = x >= width / 2 ? 0 : 255;
        }
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, pixels.data(), pixels.size(), nullptr);
    CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, color,
        kCGBitmapByteOrder32Big | kCGImageAlphaLast, provider, nullptr, false, kCGRenderingIntentDefault);
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    if (!destination || !image) throw std::runtime_error("Cannot create overlay test PNG");
    CGImageDestinationAddImage(destination, image, nullptr);
    bool ok = CGImageDestinationFinalize(destination);
    CFRelease(destination); CGImageRelease(image); CGDataProviderRelease(provider); CGColorSpaceRelease(color);
    if (!ok) throw std::runtime_error("Cannot write overlay test PNG");
}
