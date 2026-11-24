#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/macos.sh
obs=$(version_value obs)
simde=$(version_value simde)
if [[ ! -f "$OBS_APP/Contents/Frameworks/libobs.framework/libobs" ]]; then
  printf 'OBS.app not found: %s (override OBS_APP if needed)\n' "$OBS_APP" >&2
  exit 1
fi
if [[ ! -f ".deps/obs-studio-$obs/libobs/obs-module.h" ]]; then
  mkdir -p .deps
  fetch_verified "https://github.com/obsproject/obs-studio/archive/refs/tags/$obs.tar.gz" \
    "$(version_value obsSourceSha256)" .deps/obs-studio.tar.gz
  tar -xzf .deps/obs-studio.tar.gz -C .deps
fi
if [[ ! -f ".deps/simde-$simde/simde/x86/sse2.h" ]]; then
  fetch_verified "https://github.com/simd-everywhere/simde/archive/refs/tags/v$simde.tar.gz" \
    "$(version_value simdeSha256)" .deps/simde.tar.gz
  tar -xzf .deps/simde.tar.gz -C .deps
fi
obs_sdk_flags
bundle=build/obs-qrcode.plugin
mkdir -p "$bundle/Contents"/{MacOS,Resources}
sed -e "s/@VERSION@/$(version_value plugin)/" -e "s/@BUILD@/$(version_value pluginBuild)/" \
  packaging/macos/Info.plist.in > "$bundle/Contents/Info.plist"
xcrun clang++ "${mac_compile[@]}" -fobjc-arc -bundle "${obs_compile[@]}" \
  "${obs_link[@]}" "${mac_frameworks[@]}" src/obs/plugin.cpp "${mac_backend[@]}" \
  -o "$bundle/Contents/MacOS/obs-qrcode"
codesign --force --sign - "$bundle"
printf 'Built: %s/%s\n' "$PWD" "$bundle"
