cmake_minimum_required(VERSION 3.28)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/versions.json" versions)
string(JSON plugin_version GET "${versions}" plugin)
project(NoQRLive VERSION ${plugin_version} LANGUAGES CXX)

if(NOT MINGW OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(FATAL_ERROR "Use scripts/package-windows.ps1 for Windows x64 or scripts/build.sh for macOS.")
endif()

set(OBS_DLL "" CACHE FILEPATH "Path to the official Windows x64 obs.dll")
if(NOT EXISTS "${OBS_DLL}")
    message(FATAL_ERROR "Set OBS_DLL to bin/64bit/obs.dll from the pinned OBS release.")
endif()

include(FetchContent)
string(JSON obs_version GET "${versions}" obs)
string(JSON obs_hash GET "${versions}" obsSourceSha256)
string(JSON zxing_version GET "${versions}" zxing)
string(JSON zxing_hash GET "${versions}" zxingSha256)
FetchContent_Declare(obs
    URL "https://github.com/obsproject/obs-studio/archive/refs/tags/${obs_version}.tar.gz"
    URL_HASH "SHA256=${obs_hash}"
    SOURCE_SUBDIR libobs-headers-only)
FetchContent_Declare(zxing
    URL "https://codeload.github.com/zxing-cpp/zxing-cpp/tar.gz/refs/tags/v${zxing_version}"
    URL_HASH "SHA256=${zxing_hash}"
    EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS OFF)
set(ZXING_WRITERS OFF CACHE STRING "Build ZXing without barcode encoders")
set(ZXING_EXAMPLES OFF)
FetchContent_MakeAvailable(obs zxing)

add_library(qr-platform STATIC
    src/core/mask.cpp
    src/platform/windows/detector.cpp
    src/platform/windows/overlay.cpp)
target_compile_features(qr-platform PUBLIC cxx_std_17)
target_compile_definitions(qr-platform PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(qr-platform PUBLIC src/core)
target_link_libraries(qr-platform PRIVATE ZXing::ZXing ole32 windowscodecs)

add_library(obs-qrcode MODULE src/obs/plugin.cpp packaging/windows-module.def)
set_target_properties(obs-qrcode PROPERTIES PREFIX "")
target_include_directories(obs-qrcode SYSTEM PRIVATE "${obs_SOURCE_DIR}/libobs" config)
target_link_libraries(obs-qrcode PRIVATE qr-platform "${OBS_DLL}")
target_link_options(obs-qrcode PRIVATE -static)

include(CTest)
if(BUILD_TESTING)
    add_executable(windows-smoke tests/windows_smoke.cpp)
    target_link_libraries(windows-smoke PRIVATE qr-platform)
    target_link_options(windows-smoke PRIVATE -static -municode)
    add_test(NAME windows-smoke COMMAND windows-smoke "$<TARGET_FILE:obs-qrcode>"
        "${OBS_DLL}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()

install(TARGETS obs-qrcode LIBRARY DESTINATION obs-qrcode/bin/64bit)
install(FILES packaging/GPL-3.0.txt DESTINATION . RENAME LICENSE-GPL-3.0.txt)
install(FILES "${zxing_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE-ZXing.txt)
get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
install(FILES "${compiler_bin}/../LICENSE.TXT" DESTINATION . RENAME LICENSE-LLVM-MinGW.txt)
install(FILES README.md LICENSE .gitignore CMakeLists.txt 安装插件.command DESTINATION source/NoQR-Live)
install(DIRECTORY src scripts tests config cmake packaging .github DESTINATION source/NoQR-Live)
install(DIRECTORY "${zxing_SOURCE_DIR}/" DESTINATION source/NoQR-Live/third_party/zxing-cpp)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "OBS-QR-Shield-${PROJECT_VERSION}-Windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
