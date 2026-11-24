#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include "overlay.hpp"

namespace qr {
Overlay loadOverlay(const std::string &path)
{
    @autoreleasepool {
        Overlay result;
        if (path.empty()) return result;
        NSString *file = [NSString stringWithUTF8String:path.c_str()];
        if (!file) { result.error = "Invalid image path"; return result; }
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:file], nullptr);
        if (!source) { result.error = "Cannot open overlay image"; return result; }
        // Bound decode/upload size and normalize EXIF orientation. Use the first image only.
        NSDictionary *options = @{(id)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
            (id)kCGImageSourceCreateThumbnailWithTransform: @YES,
            (id)kCGImageSourceThumbnailMaxPixelSize: @1024};
        CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
        CFRelease(source);
        if (!image) { result.error = "Cannot decode overlay image"; return result; }
        result.width = uint32_t(CGImageGetWidth(image));
        result.height = uint32_t(CGImageGetHeight(image));
        result.pixels.resize(size_t(result.width) * result.height * 4);
        CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(result.pixels.data(), result.width, result.height, 8,
            size_t(result.width) * 4, color, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(color);
        if (context) {
            CGRect bounds = CGRectMake(0, 0, result.width, result.height);
            CGContextSetRGBFillColor(context, 0, 0, 0, 1);
            CGContextFillRect(context, bounds);
            CGContextDrawImage(context, bounds, image);
            CGContextRelease(context);
        } else {
            result.pixels.clear();
            result.error = "Cannot allocate overlay bitmap";
        }
        CGImageRelease(image);
        return result;
    }
}
}
