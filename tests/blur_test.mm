#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include "blur.hpp"
#include "detector.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (qr::blurStrength(-100) != 60 || qr::blurStrength(500) != 100 ||
            qr::blurFraction(60) < 0.139f || qr::blurFraction(100) < qr::blurFraction(60)) return 1;
        bool positive = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--positive") { positive = true; continue; }
            NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[i]]];
            auto source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
            if (!source) return 2;
            auto image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
            CFRelease(source);
            if (!image) return 3;
            uint32_t width = uint32_t(CGImageGetWidth(image)), height = uint32_t(CGImageGetHeight(image));
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            auto color = CGColorSpaceCreateDeviceRGB();
            auto context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, color,
                kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
            CGColorSpaceRelease(color);
            if (!context) return 4;
            CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
            CGContextRelease(context); CGImageRelease(image);
            auto result = qr::detect(pixels.data(), width, height);
            if (!result.ok || (positive ? result.boxes.size() != 2 : !result.boxes.empty())) {
                std::cerr << "FAIL scan " << argv[i] << " count=" << result.boxes.size() << " " << result.error << "\n";
                return 5;
            }
            std::cout << "PASS scan " << argv[i] << " count=" << result.boxes.size() << "\n";
            positive = false;
        }
        std::cout << "PASS blur minimum and relative strength\n";
    }
}
