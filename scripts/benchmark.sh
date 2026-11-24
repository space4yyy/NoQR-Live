#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/macos.sh
mkdir -p build
detector="${QR_BENCHMARK_DETECTOR:-src/platform/macos/detector.mm}"
binary="${QR_BENCHMARK_BINARY:-build/detection-benchmark}"
xcrun clang++ "${mac_compile[@]}" -fobjc-arc "${mac_frameworks[@]}" -framework CoreImage \
  tests/detection_benchmark.mm "$detector" -o "$binary"
"$binary"
