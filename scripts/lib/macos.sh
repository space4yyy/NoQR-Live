#!/bin/bash
# Source from the repository root.
OBS_APP="${OBS_APP:-/Applications/OBS.app}"
version_value() { plutil -extract "$1" raw -o - config/versions.json; }

fetch_verified() {
  local url="$1" sha="$2" file="$3"
  if [[ ! -f "$file" ]] || ! printf '%s  %s\n' "$sha" "$file" | shasum -a 256 -c - >/dev/null; then
    curl -fL --retry 2 "$url" -o "$file.partial"
    printf '%s  %s\n' "$sha" "$file.partial" | shasum -a 256 -c -
    mv "$file.partial" "$file"
  fi
}

mac_compile=(-std=c++17 -O2 -Wall -Wextra -mmacosx-version-min=13.0 -I src/core)
mac_backend=(src/platform/macos/detector.mm src/platform/macos/overlay.mm src/core/mask.cpp)
mac_frameworks=(-framework Foundation -framework Vision -framework CoreGraphics -framework ImageIO)

obs_sdk_flags() {
  obs_compile=(-I ".deps/obs-studio-$(version_value obs)/libobs" -I ".deps/simde-$(version_value simde)" -I config)
  obs_link=(-F "$OBS_APP/Contents/Frameworks" -framework libobs -Wl,-rpath,@executable_path/../Frameworks)
}
