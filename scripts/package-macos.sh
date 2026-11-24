#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/build.sh
bundle="$PWD/build/obs-qrcode.plugin"
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$bundle/Contents/Info.plist")
architecture=$(lipo -archs "$bundle/Contents/MacOS/obs-qrcode")
if [[ "$architecture" != arm64 ]]; then
  echo "This installer is configured for arm64; found: $architecture" >&2
  exit 1
fi
codesign --verify --strict "$bundle"
staging=$(mktemp -d "$PWD/build/package.XXXXXX")
trap 'rm -rf "$staging"' EXIT
name="OBS-QR-Shield-$version-macOS-arm64"
destination="Library/Application Support/obs-studio/plugins"
mkdir -p "$staging/root/$destination" "$staging/resources" "$staging/dmg" "$staging/source/obs-qrcode"
ditto "$bundle" "$staging/root/$destination/obs-qrcode.plugin"
cp packaging/macos/welcome.html packaging/macos/conclusion.html "$staging/resources/"
cp packaging/macos/README.html "$staging/resources/readme.html"
cp LICENSE "$staging/resources/license.txt"
# Some pkgbuild versions do not auto-discover .plugin bundles.
cat > "$staging/components.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><array><dict>
<key>RootRelativeBundlePath</key><string>$destination/obs-qrcode.plugin</string>
<key>BundleIsRelocatable</key><false/>
<key>BundleIsVersionChecked</key><true/>
<key>BundleHasStrictIdentifier</key><true/>
<key>BundleOverwriteAction</key><string>upgrade</string>
</dict></array></plist>
EOF
pkgbuild --root "$staging/root" --component-plist "$staging/components.plist" \
  --identifier local.obs-qrcode.installer.component --version "$version" \
  --install-location / "$staging/component.pkg"
cat > "$staging/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>OBS QR Shield $version</title>
  <welcome file="welcome.html" mime-type="text/html"/>
  <readme file="readme.html" mime-type="text/html"/>
  <license file="license.txt" mime-type="text/plain"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
  <volume-check><allowed-os-versions><os-version min="13.0"/></allowed-os-versions></volume-check>
  <choices-outline><line choice="plugin"/></choices-outline>
  <choice id="plugin" title="二维码自动遮挡 / QR Shield" description="为当前用户安装 OBS 二维码滤镜">
    <pkg-ref id="local.obs-qrcode.installer.component"/>
  </choice>
  <pkg-ref id="local.obs-qrcode.installer.component" version="$version">component.pkg</pkg-ref>
  <pkg-ref id="local.obs-qrcode.installer.component"><must-close><app id="com.obsproject.obs-studio"/></must-close></pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution "$staging/Distribution.xml" --resources "$staging/resources" \
  --package-path "$staging" "$PWD/build/$name.pkg"
for folder in src scripts tests packaging config cmake .github; do
  ditto "$folder" "$staging/source/obs-qrcode/$folder"
done
cp LICENSE README.md .gitignore CMakeLists.txt 安装插件.command "$staging/source/obs-qrcode/"
ditto -c -k --sequesterRsrc --keepParent "$staging/source/obs-qrcode" "$PWD/build/OBS-QR-Shield-$version-source.zip"
cp "$PWD/build/$name.pkg" "$staging/dmg/"
cp "$PWD/build/OBS-QR-Shield-$version-source.zip" "$staging/dmg/"
cp packaging/macos/README.html "$staging/dmg/安装说明.html"
cp LICENSE "$staging/dmg/LICENSE"
hdiutil create -volname "OBS QR Shield $version" -srcfolder "$staging/dmg" -ov -format UDZO "$PWD/build/$name.dmg"
(cd build && shasum -a 256 "$name.pkg" "$name.dmg" "OBS-QR-Shield-$version-source.zip") > "build/$name-SHA256.txt"
echo "Created: $PWD/build/$name.pkg"
echo "Created: $PWD/build/$name.dmg"
