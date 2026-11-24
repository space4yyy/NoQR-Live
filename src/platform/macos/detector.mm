#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>
#include "detector.hpp"
#include <algorithm>
#include <cmath>

namespace qr {
namespace {
// Each worker owns its request: Vision requests are never shared concurrently.
struct VisionContext {
    VNDetectBarcodesRequest *__strong request;
    VNSequenceRequestHandler *__strong handler;
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    VisionContext() { reset(); }
    void reset()
    {
        request = [[VNDetectBarcodesRequest alloc] init];
        request.symbologies = @[VNBarcodeSymbologyQR];
        handler = [[VNSequenceRequestHandler alloc] init];
    }
    ~VisionContext() { CGColorSpaceRelease(color); }
};
}
Detection detect(const uint8_t *rgba, uint32_t width, uint32_t height)
{
    @autoreleasepool {
        Detection result;
        if (!rgba || !width || !height) {
            result.error = "Invalid image";
            return result;
        }
        thread_local VisionContext context;
        @try {
            CGDataProviderRef provider = CGDataProviderCreateWithData(
                nullptr, rgba, size_t(width) * height * 4, nullptr);
            CGImageRef image = CGImageCreate(width, height, 8, 32, size_t(width) * 4,
                context.color, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast,
                provider, nullptr, false, kCGRenderingIntentDefault);
            CGDataProviderRelease(provider);
            if (!image) { result.error = "CGImageCreate failed"; return result; }
            VNDetectBarcodesRequest *request = context.request;
            NSError *error = nil;
            BOOL success = NO;
            @try {
                // Sequence handlers retain the image for no longer than this call.
                success = [context.handler performRequests:@[request] onCGImage:image
                    orientation:kCGImagePropertyOrientationUp error:&error];
            } @finally {
                CGImageRelease(image);
            }
            if (!success) {
                result.error = error.localizedDescription.UTF8String ?: "Vision failed";
                context.reset();
                return result;
            }
            for (VNBarcodeObservation *observation in request.results) {
                const CGRect box = observation.boundingBox;
                const int x0 = int(std::floor(CGRectGetMinX(box) * width));
                const int y0 = int(std::floor((1.0 - CGRectGetMaxY(box)) * height));
                const int x1 = int(std::ceil(CGRectGetMaxX(box) * width));
                const int y1 = int(std::ceil((1.0 - CGRectGetMinY(box)) * height));
                result.boxes.push_back({x0, y0, x1 - x0, y1 - y0});
            }
            result.ok = true;
        } @catch (NSException *exception) {
            result.error = exception.reason.UTF8String ?: "Vision exception";
            context.reset();
        }
        return result;
    }
}
}
