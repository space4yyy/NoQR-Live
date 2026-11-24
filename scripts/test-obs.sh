#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/macos.sh
bash scripts/build.sh
obs_sdk_flags
common=("${mac_compile[@]}" -fobjc-arc "${obs_compile[@]}" "${obs_link[@]}" "${mac_frameworks[@]}")
xcrun clang++ "${common[@]}" -Wl,-rpath,"$OBS_APP/Contents/Frameworks" \
  -framework AppKit -framework CoreImage tests/obs_test.mm -o build/obs-test
module="$PWD/build/obs-qrcode.plugin/Contents/MacOS/obs-qrcode"
if [[ "${1:-}" == --failure ]]; then
  xcrun clang++ "${common[@]}" -bundle src/obs/plugin.cpp src/core/mask.cpp \
    src/platform/macos/overlay.mm tests/failing_detector.cpp -o build/obs-qrcode-failure.so
  module="$PWD/build/obs-qrcode-failure.so"
  set -- 640 360 --failure
fi
if [[ "${QR_TEST_BUILD_ONLY:-0}" != 1 ]]; then
  build/obs-test "$OBS_APP/Contents/Frameworks" "$module" "$@"
fi
