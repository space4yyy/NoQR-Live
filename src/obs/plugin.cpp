#include <obs-module.h>
#include <util/platform.h>
#include "detector.hpp"
#include "geometry.hpp"
#include "overlay.hpp"
#include "blur.hpp"
#include "blur-effect.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void)
{
    return "Local QR redaction for OBS Studio";
}

namespace {
struct Frame {
    uint32_t width, height, detectionWidth, detectionHeight;
    uint64_t timestamp, generation, id;
    int padding;
    qr::MaskMode mode;
    int blurStrength;
    std::vector<uint8_t> pixels;
};

struct Result {
    uint64_t timestamp, generation, id;
    bool ok;
    std::vector<qr::Rect> masks;
    qr::MaskMode mode;
    int blurStrength;
};

struct Filter {
    obs_source_t *context;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, busy = false;
    std::unique_ptr<Frame> pending;
    // Only metadata crosses back to the graphics thread, never RGBA pixels.
    std::deque<std::unique_ptr<Result>> completed;
    uint64_t generation = 1;
    int padding = 16, delayMs = 100, maxFps = 30;
    qr::MaskMode mode = qr::MaskMode::Black;
    int blurStrength = 80;
    std::string overlayPath;
    int64_t overlayReload = 0;
    uint64_t overlayRevision = 0;
    bool overlayNeedsLoad = false;
    std::unique_ptr<qr::Overlay> pendingOverlay;
    std::vector<uint8_t> reusablePixels;
    std::thread worker;
    // Graphics-thread-owned resources below.
    // Retain the exact source texture until its matching detection completes.
    // These objects are created, recycled and destroyed only on the graphics thread.
    std::unordered_map<uint64_t, gs_texrender_t *> retained;
    std::vector<gs_texrender_t *> pool;
    gs_stagesurf_t *stage = nullptr;
    gs_texture_t *overlayTexture = nullptr;
    uint32_t overlayWidth = 0, overlayHeight = 0;
    uint64_t graphicsOverlayRevision = 0;
    gs_texrender_t *output = nullptr;
    gs_effect_t *blurEffect = nullptr;
    gs_texrender_t *blurHorizontal = nullptr, *blurVertical = nullptr;
    bool blurFailed = false;
    uint32_t width = 0, height = 0;
    uint64_t graphicsGeneration = 0, lastCapture = 0, outputTimestamp = 0;
    uint64_t nextId = 1;
    bool rendered = false;

    explicit Filter(obs_source_t *source) : context(source), worker([this] { work(); }) {}

    void work()
    {
        bool loggedError = false;
        for (;;) {
            std::unique_ptr<Frame> frame;
            std::string imagePath;
            uint64_t imageRevision = 0;
            bool loadImage = false;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || pending != nullptr || overlayNeedsLoad; });
                if (stopping) return;
                if (overlayNeedsLoad) {
                    loadImage = true;
                    overlayNeedsLoad = false;
                    imagePath = overlayPath;
                    imageRevision = overlayRevision;
                } else frame = std::move(pending);
            }
            if (loadImage) {
                auto image = std::make_unique<qr::Overlay>(qr::loadOverlay(imagePath));
                std::lock_guard lock(mutex);
                if (imageRevision == overlayRevision) pendingOverlay = std::move(image);
                continue;
            }
            auto result = qr::detect(frame->pixels.data(), frame->detectionWidth, frame->detectionHeight);
            auto completedResult = std::make_unique<Result>(Result{
                frame->timestamp, frame->generation, frame->id, result.ok, {}, frame->mode, frame->blurStrength});
            if (result.ok) {
                auto mapped = qr::sourceBoxes(result.boxes, {frame->detectionWidth, frame->detectionHeight},
                                               {frame->width, frame->height});
                completedResult->masks = qr::maskRects(frame->width, frame->height, mapped, frame->padding);
                loggedError = false;
            } else {
                if (!loggedError) blog(LOG_ERROR, "[obs-qrcode] Detection failed; output blocked: %s", result.error.c_str());
                loggedError = true;
            }
            {
                std::lock_guard lock(mutex);
                // Hand the allocation back before allowing the next capture.
                reusablePixels = std::move(frame->pixels);
                if (completedResult->generation == generation) completed.push_back(std::move(completedResult));
                busy = false;
            }
        }
    }

    gs_texrender_t *acquireTexture()
    {
        if (pool.empty()) return gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        auto *texture = pool.back();
        pool.pop_back();
        return texture;
    }

    gs_texrender_t *takeTexture(uint64_t id)
    {
        auto found = retained.find(id);
        if (found == retained.end()) return nullptr;
        auto *texture = found->second;
        retained.erase(found);
        return texture;
    }

    void discardTexture(uint64_t id)
    {
        if (auto *texture = takeTexture(id)) pool.push_back(texture);
    }

    void clearGraphics()
    {
        for (auto &[id, texture] : retained) gs_texrender_destroy(texture);
        retained.clear();
        for (auto *texture : pool) gs_texrender_destroy(texture);
        pool.clear();
        gs_stagesurface_destroy(stage); stage = nullptr;
        gs_texrender_destroy(output); output = nullptr;
        lastCapture = outputTimestamp = 0;
    }

    ~Filter()
    {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_one();
        worker.join();
        obs_enter_graphics();
        clearGraphics();
        gs_texture_destroy(overlayTexture);
        gs_effect_destroy(blurEffect);
        gs_texrender_destroy(blurHorizontal);
        gs_texrender_destroy(blurVertical);
        obs_leave_graphics();
    }
};

const char *name(void *) { return "二维码自动遮挡 / QR Shield"; }
void defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "padding", 16);
    obs_data_set_default_int(settings, "delay_ms", 100);
    obs_data_set_default_int(settings, "max_fps", 30);
    obs_data_set_default_int(settings, "detection_edge", 0);
    obs_data_set_default_string(settings, "overlay_image", "");
    obs_data_set_default_int(settings, "mask_mode", 0);
    obs_data_set_default_int(settings, "blur_strength", 80);
}
void update(void *data, obs_data_t *settings)
{
    auto &f = *static_cast<Filter *>(data);
    std::lock_guard lock(f.mutex);
    f.padding = std::clamp(int(obs_data_get_int(settings, "padding")), 0, 128);
    f.delayMs = std::clamp(int(obs_data_get_int(settings, "delay_ms")), 0, 500);
    f.maxFps = std::clamp(int(obs_data_get_int(settings, "max_fps")), 5, 60);
    // Always inspect the original image, including configurations saved by 0.3.
    obs_data_set_int(settings, "detection_edge", 0);
    std::string path = obs_data_get_string(settings, "overlay_image");
    // Preserve custom pictures configured before explicit modes were introduced.
    int mode = obs_data_has_user_value(settings, "mask_mode") ? int(obs_data_get_int(settings, "mask_mode")) : path.empty() ? 0 : 2;
    if (mode < 0 || mode > 2) mode = 0;
    f.mode = static_cast<qr::MaskMode>(mode);
    const int strength = int(obs_data_has_user_value(settings, "blur_strength") ? obs_data_get_int(settings, "blur_strength") :
        obs_data_has_user_value(settings, "mosaic_strength") ? obs_data_get_int(settings, "mosaic_strength") : 80);
    f.blurStrength = qr::blurStrength(strength);
    obs_data_set_int(settings, "mask_mode", mode);
    obs_data_set_int(settings, "blur_strength", f.blurStrength);
    int64_t reload = obs_data_get_int(settings, "overlay_reload");
    if (path != f.overlayPath || reload != f.overlayReload) {
        f.overlayPath = std::move(path);
        f.overlayReload = reload;
        ++f.overlayRevision;
        f.pendingOverlay.reset();
        f.overlayNeedsLoad = true;
        f.wake.notify_one();
    }
    ++f.generation;
    f.completed.clear();
}
void *create(obs_data_t *settings, obs_source_t *source)
{
    auto *f = new Filter(source);
    update(f, settings);
    return f;
}
obs_properties_t *properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_set_param(props, data, nullptr);
    obs_properties_add_text(props, "info",
        "原分辨率检测，同屏多个二维码分别遮挡。未处理画面不会放行；启动及故障时可能黑屏。\n"
        "检测仍可能漏报，请先录制测试。仅支持 SDR；音频需另设同步偏移。", OBS_TEXT_INFO);
    obs_properties_add_int_slider(props, "padding", "额外遮挡边距（像素）", 0, 128, 1);
    obs_properties_add_int_slider(props, "delay_ms", "画面缓冲（毫秒）", 0, 500, 10);
    obs_properties_add_int_slider(props, "max_fps", "最高检测帧率（跟不上时保持已处理画面）", 5, 60, 1);
    auto *mode = obs_properties_add_list(props, "mask_mode", "遮挡方式", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "黑块", 0);
    obs_property_list_add_int(mode, "高斯模糊 / Blur", 1);
    obs_property_list_add_int(mode, "自定义图片", 2);
    obs_properties_add_int_slider(props, "blur_strength", "模糊强度（最低 60）", qr::MinimumBlurStrength, 100, 1);
    obs_properties_add_text(props, "blur_info", "对原画面进行高斯模糊。下限按遮挡区大小计算；仍不保证所有扫码器均无法识别。", OBS_TEXT_INFO);
    obs_properties_add_path(props, "overlay_image", "自定义遮挡图片（留空使用黑块）", OBS_PATH_FILE,
        "图片 (*.png *.jpg *.jpeg);;所有文件 (*.*)", nullptr);
    obs_properties_add_button(props, "reload_overlay", "重新加载图片", [](obs_properties_t *props, obs_property_t *, void *) {
        auto *f = static_cast<Filter *>(obs_properties_get_param(props));
        if (!f) return false;
        auto *settings = obs_source_get_settings(f->context);
        obs_data_set_int(settings, "overlay_reload", obs_data_get_int(settings, "overlay_reload") + 1);
        obs_source_update(f->context, settings);
        obs_data_release(settings);
        return false;
    });
    obs_properties_add_text(props, "overlay_info", "图片等比完整显示，透明部分和周围补黑底。加载失败时使用黑块。", OBS_TEXT_INFO);
    obs_property_set_modified_callback(mode, [](obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
        const int mode = int(obs_data_get_int(settings, "mask_mode"));
        for (const char *key : {"blur_strength", "blur_info"}) obs_property_set_visible(obs_properties_get(props, key), mode == 1);
        for (const char *key : {"overlay_image", "reload_overlay", "overlay_info"}) obs_property_set_visible(obs_properties_get(props, key), mode == 2);
        return true;
    });
    return props;
}

void drawBlack(uint32_t width, uint32_t height)
{
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
    struct vec4 color;
    vec4_set(&color, 0, 0, 0, 1);
    gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
    while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, width, height);
}

void setVector(gs_effect_t *effect, const char *name, float x, float y)
{
    struct vec2 value;
    vec2_set(&value, x, y);
    gs_effect_set_vec2(gs_effect_get_param_by_name(effect, name), &value);
}

bool blurPass(Filter &f, gs_texrender_t *target, gs_texture_t *source,
              float offsetX, float offsetY, float scaleX, float scaleY,
              float minX, float minY, float maxX, float maxY, float stepX, float stepY, float sigma)
{
    gs_texrender_reset(target);
    if (!gs_texrender_begin_with_color_space(target, qr::BlurPatchSize, qr::BlurPatchSize, GS_CS_SRGB)) return false;
    gs_ortho(0, float(qr::BlurPatchSize), 0, float(qr::BlurPatchSize), -100, 100);
    gs_effect_set_texture(gs_effect_get_param_by_name(f.blurEffect, "image"), source);
    setVector(f.blurEffect, "uv_offset", offsetX, offsetY);
    setVector(f.blurEffect, "uv_scale", scaleX, scaleY);
    setVector(f.blurEffect, "uv_min", minX, minY);
    setVector(f.blurEffect, "uv_max", maxX, maxY);
    setVector(f.blurEffect, "sample_step", stepX, stepY);
    gs_effect_set_float(gs_effect_get_param_by_name(f.blurEffect, "blur_sigma"), sigma);
    while (gs_effect_loop(f.blurEffect, "Draw")) gs_draw_sprite(source, 0, qr::BlurPatchSize, qr::BlurPatchSize);
    gs_texrender_end(target);
    return true;
}

bool blurMasks(Filter &f, gs_texrender_t *target, uint32_t width, uint32_t height, const Result &result)
{
    if (f.blurFailed) return false;
    if (!f.blurEffect) {
        char *error = nullptr;
        f.blurEffect = gs_effect_create(QrBlurEffect, "qr-gaussian-blur", &error);
        if (!f.blurEffect) {
            blog(LOG_ERROR, "[obs-qrcode] Blur unavailable; blocking output: %s", error ? error : "Shader initialization failed");
            f.blurFailed = true;
        }
        bfree(error);
    }
    if (!f.blurEffect) return false;
    if (!f.blurHorizontal) f.blurHorizontal = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!f.blurVertical) f.blurVertical = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!f.blurHorizontal || !f.blurVertical) return false;
    const float fraction = qr::blurFraction(result.blurStrength);
    bool ok = true;
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    for (const auto &box : result.masks) {
        auto *source = gs_texrender_get_texture(target);
        if (!source) { ok = false; break; }
        // Blur the original pixels into two separate scratch targets before
        // writing back. Never sample a texture while it is the render target.
        ok = blurPass(f, f.blurHorizontal, source, float(box.x) / width, float(box.y) / height,
            float(box.width) / width, float(box.height) / height,
            (box.x + 0.5f) / width, (box.y + 0.5f) / height,
            (box.x + box.width - 0.5f) / width, (box.y + box.height - 0.5f) / height,
            0, 0, 0);
        if (!ok) break;
        const float halfPixel = 0.5f / qr::BlurPatchSize;
        const float step = 1.0f / qr::BlurPatchSize;
        const float sigma = fraction * qr::BlurPatchSize;
        ok = blurPass(f, f.blurVertical, gs_texrender_get_texture(f.blurHorizontal), 0, 0, 1, 1,
            halfPixel, halfPixel, 1 - halfPixel, 1 - halfPixel, step, 0, sigma);
        if (!ok) break;
        ok = blurPass(f, f.blurHorizontal, gs_texrender_get_texture(f.blurVertical), 0, 0, 1, 1,
            halfPixel, halfPixel, 1 - halfPixel, 1 - halfPixel, 0, step, sigma);
        if (!ok) break;
        gs_texrender_reset(target);
        if (!gs_texrender_begin_with_color_space(target, width, height, GS_CS_SRGB)) { ok = false; break; }
        gs_ortho(0, float(width), 0, float(height), -100, 100);
        gs_matrix_translate3f(float(box.x), float(box.y), 0);
        auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        auto *blurred = gs_texrender_get_texture(f.blurHorizontal);
        gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), blurred);
        while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(blurred, 0, box.width, box.height);
        gs_texrender_end(target);
    }
    gs_blend_state_pop();
    return ok;
}

bool redactTexture(gs_texrender_t *texture, uint32_t width, uint32_t height,
                   const Result &result, Filter &f)
{
    const auto &masks = result.masks;
    if (masks.empty()) return true;
    if (result.mode == qr::MaskMode::Blur) return blurMasks(f, texture, width, height, result);
    // Rebind the existing target without clearing or reallocating it. Only the
    // mask areas are overwritten; no shader samples its own render target.
    gs_texrender_reset(texture);
    if (!gs_texrender_begin_with_color_space(texture, width, height, GS_CS_SRGB)) return false;
    gs_ortho(0, float(width), 0, float(height), -100, 100);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    for (const auto &box : masks) {
        gs_matrix_push();
        gs_matrix_translate3f(float(box.x), float(box.y), 0);
        drawBlack(uint32_t(box.width), uint32_t(box.height));
        if (result.mode == qr::MaskMode::Image && f.overlayTexture && f.overlayWidth && f.overlayHeight) {
            const float scale = std::min(float(box.width) / f.overlayWidth, float(box.height) / f.overlayHeight);
            const uint32_t imageWidth = std::max(1u, uint32_t(f.overlayWidth * scale));
            const uint32_t imageHeight = std::max(1u, uint32_t(f.overlayHeight * scale));
            gs_matrix_translate3f((box.width - imageWidth) / 2.0f, (box.height - imageHeight) / 2.0f, 0);
            auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), f.overlayTexture);
            while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(f.overlayTexture, 0, imageWidth, imageHeight);
        }
        gs_matrix_pop();
    }
    gs_blend_state_pop();
    gs_texrender_end(texture);
    return true;
}

bool captureFrame(Filter &f, obs_source_t *target, obs_source_t *parent, Frame &frame, gs_texrender_t *capture)
{
    if (!f.stage) f.stage = gs_stagesurface_create(frame.detectionWidth, frame.detectionHeight, GS_RGBA);
    if (!capture || !f.stage) return false;
    gs_texrender_reset(capture);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    const bool began = gs_texrender_begin_with_color_space(capture, f.width, f.height, GS_CS_SRGB);
    if (began) {
        struct vec4 clear;
        vec4_set(&clear, 0, 0, 0, 1);
        gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
        gs_ortho(0, float(f.width), 0, float(f.height), -100, 100);
        const uint32_t flags = obs_source_get_output_flags(target);
        if (target == parent && !(flags & OBS_SOURCE_CUSTOM_DRAW) && !(flags & OBS_SOURCE_ASYNC))
            obs_source_default_render(target);
        else
            obs_source_video_render(target);
        gs_texrender_end(capture);
    }
    gs_blend_state_pop();
    if (!began) return false;
    gs_stage_texture(f.stage, gs_texrender_get_texture(capture));
    uint8_t *pixels = nullptr;
    uint32_t stride = 0;
    if (!gs_stagesurface_map(f.stage, &pixels, &stride)) return false;
    for (uint32_t y = 0; y < frame.detectionHeight; ++y)
        std::memcpy(frame.pixels.data() + size_t(y) * frame.detectionWidth * 4,
                    pixels + size_t(y) * stride, size_t(frame.detectionWidth) * 4);
    gs_stagesurface_unmap(f.stage);
    return true;
}

void render(void *data, gs_effect_t *)
{
    auto &f = *static_cast<Filter *>(data);
    obs_source_t *target = obs_filter_get_target(f.context);
    obs_source_t *parent = obs_filter_get_parent(f.context);
    uint32_t width = target ? obs_source_get_base_width(target) : 0;
    uint32_t height = target ? obs_source_get_base_height(target) : 0;
    if (!target || !parent || !width || !height) { drawBlack(width ? width : 1, height ? height : 1); return; }
    // Bound CPU/GPU allocation; unsupported dimensions fail closed.
    if (width > 4096 || height > 4096) { drawBlack(width, height); return; }
    // Refuse HDR sources rather than silently clipping them during RGBA8 readback.
    const enum gs_color_space preferred = GS_CS_SRGB;
    if (obs_source_get_color_space(target, 1, &preferred) != GS_CS_SRGB) {
        drawBlack(width, height); return;
    }
    uint64_t now = os_gettime_ns();
    uint64_t generation;
    int padding, delay, fps;
    qr::MaskMode mode;
    int blurStrength;
    uint64_t overlayRevision;
    std::unique_ptr<qr::Overlay> image;
    {
        std::lock_guard lock(f.mutex);
        if (f.width != width || f.height != height) { ++f.generation; f.completed.clear(); }
        generation = f.generation;
        padding = f.padding; delay = f.delayMs; fps = f.maxFps;
        mode = f.mode; blurStrength = f.blurStrength;
        overlayRevision = f.overlayRevision;
        image = std::move(f.pendingOverlay);
    }
    if (f.graphicsOverlayRevision != overlayRevision) {
        gs_texture_destroy(f.overlayTexture); f.overlayTexture = nullptr;
        f.overlayWidth = f.overlayHeight = 0;
        f.graphicsOverlayRevision = overlayRevision;
    }
    if (image) {
        if (!image->pixels.empty()) {
            const uint8_t *pixels = image->pixels.data();
            f.overlayTexture = gs_texture_create(image->width, image->height, GS_RGBA, 1, &pixels, 0);
            f.overlayWidth = image->width; f.overlayHeight = image->height;
        }
        if (!image->error.empty()) blog(LOG_WARNING, "[obs-qrcode] Overlay unavailable; using black: %s", image->error.c_str());
    }
    if (f.graphicsGeneration != generation) {
        f.clearGraphics();
        f.graphicsGeneration = generation;
        f.width = width; f.height = height;
        f.rendered = false;
    }
    if (!f.rendered) {
        f.rendered = true;
        std::unique_ptr<Result> ready;
        std::vector<uint64_t> discarded;
        bool canCapture;
        std::vector<uint8_t> pixels;
        {
            std::lock_guard lock(f.mutex);
            while (!f.completed.empty() && f.completed.front()->timestamp + uint64_t(delay) * 1000000 <= now) {
                if (ready) discarded.push_back(ready->id);
                ready = std::move(f.completed.front()); f.completed.pop_front();
            }
            // Backpressure bounds retained source textures to approximately 128 MiB.
            const size_t capacity = std::max(size_t(2), size_t(128 * 1024 * 1024) / (size_t(width) * height * 4));
            canCapture = !f.busy && f.completed.size() < capacity &&
                now - f.lastCapture + 1000000ULL >= 1000000000ULL / fps;
            if (canCapture) { f.busy = true; pixels = std::move(f.reusablePixels); }
        }
        for (uint64_t id : discarded) f.discardTexture(id);
        if (ready && ready->generation == generation) {
            if (f.output) f.pool.push_back(f.output);
            f.output = f.takeTexture(ready->id);
            f.outputTimestamp = ready->timestamp;
            if (f.output && (!ready->ok || !redactTexture(f.output, width, height, *ready, f))) {
                f.pool.push_back(f.output); f.output = nullptr;
            }
        }
        if (canCapture) {
            const qr::Size size = {width, height};
            auto frame = std::make_unique<Frame>(Frame{width, height, size.width, size.height,
                now, generation, f.nextId++, padding, mode, blurStrength, std::move(pixels)});
            frame->pixels.resize(size_t(size.width) * size.height * 4);
            auto *texture = f.acquireTexture();
            bool captured = captureFrame(f, target, parent, *frame, texture);
            f.lastCapture = now;
            {
                std::lock_guard lock(f.mutex);
                if (captured && generation == f.generation) {
                    f.retained.emplace(frame->id, texture);
                    f.pending = std::move(frame);
                } else {
                    captured = false;
                    f.reusablePixels = std::move(frame->pixels);
                    f.busy = false;
                }
            }
            if (!captured && texture) f.pool.push_back(texture);
            if (captured) f.wake.notify_one();
        }
    }
    // Never skip this filter: skipping would expose the raw source.
    if (!f.output || now - f.outputTimestamp > 1500000000ULL) { drawBlack(width, height); return; }
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_texture_t *texture = gs_texrender_get_texture(f.output);
    if (!texture) { drawBlack(width, height); return; }
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
    while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(texture, 0, width, height);
}
}

MODULE_EXPORT bool obs_module_load(void)
{
    struct obs_source_info info = {};
    info.id = "macos_qr_shield";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = name;
    info.create = create;
    info.destroy = [](void *data) { delete static_cast<Filter *>(data); };
    info.update = update;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.video_tick = [](void *data, float) { static_cast<Filter *>(data)->rendered = false; };
    info.video_render = render;
    obs_register_source(&info);
    blog(LOG_INFO, "[obs-qrcode] QR Shield loaded (Apple Vision, SDR, GPU masks)");
    return true;
}
