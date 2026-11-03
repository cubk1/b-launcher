#!/bin/sh
set -eu
binary=$1
mkdir -p "$2"
output=$(cd "$2" && pwd)
version=$3
app="$output/B Launcher.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$binary" "$app/Contents/MacOS/b-launcher"
chmod 755 "$app/Contents/MacOS/b-launcher"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>B Launcher</string>
    <key>CFBundleDisplayName</key>
    <string>B Launcher</string>
    <key>CFBundleIdentifier</key>
    <string>cn.cubk.b-launcher</string>
    <key>CFBundleExecutable</key>
    <string>b-launcher</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST
if command -v codesign > /dev/null; then
    codesign --force --sign - "$app"
elif command -v rcodesign > /dev/null; then
    rcodesign sign "$app"
fi
