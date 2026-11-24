#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/macos.sh
mkdir -p build
xcrun clang++ "${mac_compile[@]}" -fobjc-arc "${mac_frameworks[@]}" -framework CoreImage \
  tests/detector_test.mm src/platform/macos/detector.mm src/core/mask.cpp -o build/detector-test
build/detector-test
xcrun clang++ "${mac_compile[@]}" -fobjc-arc "${mac_frameworks[@]}" \
  tests/overlay_test.mm src/platform/macos/overlay.mm -o build/overlay-test
build/overlay-test
xcrun clang++ "${mac_compile[@]}" -fobjc-arc "${mac_frameworks[@]}" \
  tests/blur_test.mm src/platform/macos/detector.mm -o build/blur-test
build/blur-test
