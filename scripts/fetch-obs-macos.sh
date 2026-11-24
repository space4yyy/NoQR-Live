#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/macos.sh
version=$(version_value obs)
asset="OBS-Studio-$version-macOS-Apple.dmg"
mkdir -p .deps/obs-assets
fetch_verified "https://github.com/obsproject/obs-studio/releases/download/$version/$asset" \
  "$(version_value obsMacArm64Sha256)" ".deps/obs-assets/$asset"
mountpoint=$(mktemp -d)
cleanup() {
  if [[ -d "$mountpoint/OBS.app" ]]; then hdiutil detach "$mountpoint" -quiet; fi
  rmdir "$mountpoint"
}
trap cleanup EXIT
hdiutil attach ".deps/obs-assets/$asset" -readonly -nobrowse -mountpoint "$mountpoint" -quiet
if [[ -e "$OBS_APP" ]]; then
  printf 'Destination already exists: %s\n' "$OBS_APP" >&2
  exit 1
fi
mkdir -p "$(dirname "$OBS_APP")"
ditto "$mountpoint/OBS.app" "$OBS_APP"
