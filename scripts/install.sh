#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ ! -f build/obs-qrcode.plugin/Contents/MacOS/obs-qrcode ]]; then
  bash scripts/build.sh
fi
destination="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$destination"
staging=$(mktemp -d "$destination/.obs-qrcode-install.XXXXXX")
cleanup() {
  local status=$?
  if [[ ! -e "$destination/obs-qrcode.plugin" && -d "$staging/previous.plugin" ]]; then
    mv "$staging/previous.plugin" "$destination/obs-qrcode.plugin"
  fi
  rm -rf "$staging"
  return "$status"
}
trap cleanup EXIT
ditto build/obs-qrcode.plugin "$staging/next.plugin"
codesign --verify --strict "$staging/next.plugin"
# Rename complete bundles; never overwrite a binary already mapped by OBS.
if [[ -e "$destination/obs-qrcode.plugin" ]]; then
  mv "$destination/obs-qrcode.plugin" "$staging/previous.plugin"
fi
mv "$staging/next.plugin" "$destination/obs-qrcode.plugin"
echo "Installed: $destination/obs-qrcode.plugin"
echo "Restart OBS, then add 二维码自动遮挡 / QR Shield under the capture source's Effects Filters."
