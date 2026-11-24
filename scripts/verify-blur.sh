#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/test.sh
QR_TEST_BUILD_ONLY=1 bash scripts/test-obs.sh --failure
OBS_APP="${OBS_APP:-/Applications/OBS.app}"
for strength in 0 100; do
  QR_TEST_MODE=1 QR_TEST_BLUR_STRENGTH="$strength" build/obs-test \
    "$OBS_APP/Contents/Frameworks" "$PWD/build/obs-qrcode.plugin/Contents/MacOS/obs-qrcode" 1920 1080
  build/blur-test --positive build/blur-positive.png \
    build/blur-output-0.png build/blur-output-1.png build/blur-output-2.png
done
