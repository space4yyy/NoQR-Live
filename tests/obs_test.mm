#import <AppKit/AppKit.h>
#include <obs-module.h>
#include <util/platform.h>
#include "fixtures.hpp"
#include "overlay_fixture.hpp"
#include "blur.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
int W = 640, H = 360;
std::vector<uint8_t> inputs[4];
qr::Rect boxes[2];
std::atomic<int> frames{0}, visible{0}, masked[2], leaks{0}, bothMasked{0}, blank{0};
std::atomic<int> pictures[2], bothPictures{0}, blackOnly{0};
std::atomic<int> blurs[2], bothBlurs{0};
bool expectBlur = false;
std::mutex snapshotMutex;
std::vector<std::vector<uint8_t>> blurSnapshots;
struct Source {
    gs_texture_t *textures[4] = {};
    uint64_t start = os_gettime_ns();
};
void *makeSource(obs_data_t *, obs_source_t *) { return new Source; }
void drawSource(void *data, gs_effect_t *)
{
    auto &s = *static_cast<Source *>(data);
    const int index = int((os_gettime_ns() - s.start) / 700000000) % 4;
    if (!s.textures[index]) {
        const uint8_t *pixels = inputs[index].data();
        s.textures[index] = gs_texture_create(W, H, GS_RGBA, 1, &pixels, 0);
    }
    auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), s.textures[index]);
    while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(s.textures[index], 0, W, H);
}
void receive(void *, video_data *frame)
{
    ++frames;
    const auto *bg = frame->data[0] + size_t(H / 2) * frame->linesize[0] + W / 2 * 4;
    if (bg[2] < 20) return; // startup / fail-closed black
    ++visible;
    bool isMasked[2] = {};
    bool hasPicture[2] = {};
    bool hasBlur[2] = {};
    for (int i = 0; i < 2; ++i) {
        const auto &box = boxes[i];
        bool covered = true, allBlack = true;
        int minimum = 255, maximumGradient = 0;
        bool unchangedBackground = true;
        bool white = false;
        for (int y = box.y + 3; y < box.y + box.height - 3; ++y)
            for (int x = box.x + 3; x < box.x + box.width - 3; ++x) {
                const auto *p = frame->data[0] + size_t(y) * frame->linesize[0] + x * 4;
                bool black = p[0] < 5 && p[1] < 5 && p[2] < 5;
                bool colored = (p[0] > 150 && p[1] < 90 && p[2] < 90) ||
                               (p[2] > 150 && p[1] < 90 && p[0] < 90);
                // Interpolation on the picture's border can darken red/blue.
                bool darkColor = p[1] < 90 && ((p[0] > p[2] * 2 && p[0] > 10) || (p[2] > p[0] * 2 && p[2] > 10));
                covered = covered && (black || colored || darkColor);
                for (int c = 0; c < 3; ++c) {
                    minimum = std::min(minimum, int(p[c]));
                    if (x > box.x + 3) maximumGradient = std::max(maximumGradient, std::abs(int(p[c]) - p[c - 4]));
                    if (y > box.y + 3) maximumGradient = std::max(maximumGradient,
                        std::abs(int(p[c]) - *(p + c - frame->linesize[0])));
                    unchangedBackground = unchangedBackground && std::abs(int(p[c]) - bg[c]) <= 4;
                }
                allBlack = allBlack && black;
                if (colored) hasPicture[i] = true;
                white = white || (p[0] > 200 && p[1] > 200 && p[2] > 200);
            }
        if (expectBlur) {
            hasBlur[i] = minimum > 10 && maximumGradient <= 12 && !unchangedBackground;
            covered = allBlack || hasBlur[i];
            if (!allBlack && maximumGradient > 50) ++leaks;
        }
        if (!expectBlur && white) ++leaks;
        if (covered) { ++masked[i]; isMasked[i] = true; }
        if (hasPicture[i]) ++pictures[i];
        if (hasBlur[i]) ++blurs[i];
        if (allBlack) ++blackOnly;
    }
    if (isMasked[0] && isMasked[1]) ++bothMasked;
    if (!isMasked[0] && !isMasked[1]) ++blank;
    if (hasPicture[0] && hasPicture[1]) ++bothPictures;
    if (hasBlur[0] && hasBlur[1]) {
        ++bothBlurs;
        std::lock_guard lock(snapshotMutex);
        if (blurSnapshots.size() < 3) {
            std::vector<uint8_t> pixels(size_t(W) * H * 4);
            for (int y = 0; y < H; ++y)
                std::memcpy(pixels.data() + size_t(y) * W * 4, frame->data[0] + size_t(y) * frame->linesize[0], W * 4);
            blurSnapshots.push_back(std::move(pixels));
        }
    }
}
}
int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc != 3 && argc != 5 && argc != 6) return 2;
        bool expectFailure = argc == 6 && std::string(argv[5]) == "--failure";
        if (argc >= 5) { W = std::stoi(argv[3]); H = std::stoi(argv[4]); }
        if (W < 640 || H < 360 || W > 4096 || H > 4096) return 2;
        [NSApplication sharedApplication];
        inputs[0] = fixture::background(W, H);
        inputs[1] = inputs[0]; inputs[2] = inputs[0]; inputs[3] = inputs[0];
        boxes[0] = fixture::place(inputs[0], W, H, 15, 18, 3);
        boxes[1] = fixture::place(inputs[1], W, H, W - 160, H - 155, 3);
        fixture::place(inputs[2], W, H, 15, 18, 3);
        fixture::place(inputs[2], W, H, W - 160, H - 155, 3);
        writeRGBA("build/blur-positive.png", inputs[2].data(), W, H);
        if (!obs_startup("en-US", nullptr, nullptr)) return 3;
        std::string framework = argv[1];
        std::string resources = framework + "/libobs.framework/Resources";
        obs_add_data_path(resources.c_str());
        obs_video_info video = {};
        const char *backend = std::getenv("QR_TEST_GRAPHICS");
        if (!backend) backend = "opengl";
        std::string graphics = framework + "/libobs-" + backend + ".dylib";
        video.graphics_module = graphics.c_str();
        video.fps_num = 30; video.fps_den = 1;
        video.base_width = video.output_width = W;
        video.base_height = video.output_height = H;
        video.output_format = VIDEO_FORMAT_RGBA;
        video.colorspace = VIDEO_CS_709;
        video.range = VIDEO_RANGE_FULL;
        video.scale_type = OBS_SCALE_BILINEAR;
        int result = obs_reset_video(&video);
        if (result != OBS_VIDEO_SUCCESS) { std::cerr << "Video init failed: " << result << "\n"; obs_shutdown(); return 4; }
        obs_module_t *module = nullptr;
        if (obs_open_module(&module, argv[2], "build") != MODULE_SUCCESS || !obs_init_module(module)) {
            std::cerr << "Module load failed\n"; obs_shutdown(); return 5;
        }
        obs_source_info info = {};
        info.id = "qr_test_input";
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
        info.get_name = [](void *) { return "test"; };
        info.create = makeSource;
        info.destroy = [](void *data) {
            auto *s = static_cast<Source *>(data);
            obs_enter_graphics();
            for (auto *texture : s->textures) gs_texture_destroy(texture);
            obs_leave_graphics();
            delete s;
        };
        info.get_width = [](void *) -> uint32_t { return W; };
        info.get_height = [](void *) -> uint32_t { return H; };
        info.video_render = drawSource;
        obs_register_source(&info);
        auto *source = obs_source_create("qr_test_input", "Synthetic QR test", nullptr, nullptr);
        auto *settings = obs_data_create();
        obs_data_set_int(settings, "delay_ms", 100);
        const char *edgeSetting = std::getenv("QR_TEST_DETECTION_EDGE");
        const int detectionEdge = edgeSetting ? std::stoi(edgeSetting) : 0;
        obs_data_set_int(settings, "detection_edge", detectionEdge);
        const char *overlay = std::getenv("QR_TEST_OVERLAY");
        const char *mode = std::getenv("QR_TEST_MODE");
        expectBlur = mode && std::stoi(mode) == 1;
        if (expectBlur) {
            for (int i = 0; i < 3; ++i) std::remove(("build/blur-output-" + std::to_string(i) + ".png").c_str());
        }
        bool fixture = overlay && std::string(overlay) == "fixture";
        bool expectPicture = fixture && (!mode || std::stoi(mode) == 2);
        if (fixture) { makeOverlayFixture("build/overlay-test.png"); overlay = "build/overlay-test.png"; }
        if (overlay) obs_data_set_string(settings, "overlay_image", overlay);
        if (mode) obs_data_set_int(settings, "mask_mode", std::stoi(mode));
        const char *strength = std::getenv("QR_TEST_BLUR_STRENGTH");
        if (strength) obs_data_set_int(settings, "blur_strength", std::stoi(strength));
        auto *filter = obs_source_create("macos_qr_shield", "Shield", settings, nullptr);
        obs_data_release(settings);
        if (!source || !filter) return 6;
        obs_source_filter_add(source, filter);
        obs_set_output_source(0, source);
        obs_add_raw_video_callback(nullptr, receive, nullptr);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        if (expectPicture || expectBlur) {
            auto *props = obs_source_properties(filter);
            if (expectPicture) {
                auto *reload = obs_properties_get(props, "reload_overlay");
                if (!reload) return 10;
                obs_property_button_clicked(reload, filter);
            }
            if (expectBlur) {
                auto *s = obs_source_get_settings(filter);
                if (obs_data_get_int(s, "blur_strength") < qr::MinimumBlurStrength ||
                    obs_property_int_min(obs_properties_get(props, "blur_strength")) != qr::MinimumBlurStrength) return 11;
                obs_data_release(s);
            }
            obs_properties_destroy(props);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // Invalidate worker results while it is active, then verify resumed output.
        settings = obs_source_get_settings(filter);
        obs_data_set_int(settings, "padding", 24);
        if (expectBlur) obs_data_set_int(settings, "mask_mode", 0);
        // Legacy reduced-resolution settings must be ignored in favor of full resolution.
        obs_data_set_int(settings, "detection_edge", detectionEdge ? 0 : 1280);
        // Clear a valid picture, or switch to an unreadable file to check the fallback.
        if (overlay) {
            const char *after = std::getenv("QR_TEST_OVERLAY_AFTER");
            obs_data_set_string(settings, "overlay_image", after ? after : "");
        }
        obs_source_update(filter, settings);
        obs_data_release(settings);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        obs_remove_raw_video_callback(receive, nullptr);
        if (expectBlur && !expectFailure) {
            std::lock_guard lock(snapshotMutex);
            for (size_t i = 0; i < blurSnapshots.size(); ++i)
                writeRGBA("build/blur-output-" + std::to_string(i) + ".png", blurSnapshots[i].data(), W, H);
        }
        double renderMs = double(obs_get_average_frame_time_ns()) / 1000000;
        obs_set_output_source(0, nullptr);
        obs_source_release(filter);
        obs_source_release(source);
        obs_shutdown();
        std::cout << "Backend=" << backend << " DetectionEdge=" << detectionEdge << " Resolution=" << W << "x" << H << " Frames=" << frames << " visible=" << visible
            << " masks=" << masked[0] << "," << masked[1]
            << " both=" << bothMasked << " blank=" << blank
            << " pictures=" << pictures[0] << "," << pictures[1] << " bothPictures=" << bothPictures
            << " blurs=" << blurs[0] << "," << blurs[1] << " bothBlurs=" << bothBlurs
            << " leaked=" << leaks << " render_ms=" << renderMs << "\n";
        if (expectFailure) return frames >= 60 && visible == 0 ? 0 : 1;
        if (expectPicture && (pictures[0] < 5 || pictures[1] < 5 || bothPictures < 5 || blackOnly < 5)) return 9;
        if (expectBlur && (blurs[0] < 5 || blurs[1] < 5 || bothBlurs < 5 || blackOnly < 5)) return 12;
        return frames >= 60 && visible >= 30 && masked[0] >= 5 && masked[1] >= 5 &&
            bothMasked >= 5 && blank >= 5 && leaks == 0 ? 0 : 1;
    }
}
