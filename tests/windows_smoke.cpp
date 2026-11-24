#include "detector.hpp"
#include "overlay.hpp"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string utf8(const std::filesystem::path &path)
{
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

void verifyQr(const std::filesystem::path &fixtures)
{
    auto frame = qr::loadOverlay(utf8(fixtures / "two-qr.png"));
    require(frame.error.empty() && frame.width == 640 && frame.height == 480, "Cannot load QR fixture");
    require(!qr::detect(nullptr, 0, 0).ok, "Invalid QR input accepted");
    auto result = qr::detect(frame.pixels.data(), frame.width, frame.height);
    require(result.ok, result.error.c_str());
    require(result.boxes.size() == 2, "Expected two QR codes");
    auto contains = [&](int x, int y) {
        return std::any_of(result.boxes.begin(), result.boxes.end(), [&](const qr::Rect &box) {
            return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
        });
    };
    require(contains(98, 102) && contains(480, 320), "Incorrect top-left QR coordinates");
    qr::redact(frame.pixels.data(), frame.width, frame.height, result.boxes, 16);
    result = qr::detect(frame.pixels.data(), frame.width, frame.height);
    require(result.ok && result.boxes.empty(), "QR codes still detected after redaction");
}

void verifyOverlay(const std::filesystem::path &fixtures)
{
    const auto image = qr::loadOverlay(utf8(fixtures / L"遮挡.png"));
    require(image.error.empty() && image.width == 1024 && image.height == 512, "Overlay size or Unicode path failed");
    require(image.pixels[0] == 220 && image.pixels[1] == 40 && image.pixels[2] == 20 && image.pixels[3] == 255,
            "Incorrect RGBA channel order");
    const auto *transparent = image.pixels.data() + (image.width - 1) * 4;
    require(transparent[0] == 0 && transparent[1] == 0 && transparent[2] == 0 && transparent[3] == 255,
            "Transparent overlay pixels must become opaque black");
    require(!qr::loadOverlay(utf8(fixtures / "missing.png")).error.empty(), "Missing overlay was accepted");
}

void verifyModule(std::filesystem::path pluginPath, std::filesystem::path obsPath)
{
    // CTest passes CMake paths with forward slashes. The Windows DLL loader needs native paths.
    obsPath.make_preferred();
    pluginPath.make_preferred();
    require(std::filesystem::is_regular_file(obsPath), "Official obs.dll is missing");
    require(std::filesystem::is_regular_file(pluginPath), "Plugin DLL is missing");
    constexpr DWORD search = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    const HMODULE obs = LoadLibraryExW(obsPath.c_str(), nullptr, search);
    if (!obs) throw std::runtime_error("Cannot load " + utf8(obsPath) + ": " + std::to_string(GetLastError()));
    const HMODULE module = LoadLibraryExW(pluginPath.c_str(), nullptr, search);
    const DWORD error = GetLastError();
    const bool exports = module && GetProcAddress(module, "obs_module_set_pointer") &&
        GetProcAddress(module, "obs_module_ver") && GetProcAddress(module, "obs_module_load");
    if (module) FreeLibrary(module);
    FreeLibrary(obs);
    if (!module) throw std::runtime_error("Cannot load obs-qrcode.dll: " + std::to_string(error));
    require(exports, "OBS module exports are missing");
}
}

int wmain(int argc, wchar_t **argv)
{
    try {
        require(argc == 4, "Expected plugin DLL, OBS DLL and fixture directory");
        verifyModule(argv[1], argv[2]);
        verifyQr(argv[3]);
        verifyOverlay(argv[3]);
        std::cout << "PASS: OBS module load, QR redaction and Windows overlay decoding\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
