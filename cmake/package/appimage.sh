#!/bin/sh
set -eu
binary=$(realpath "$1")
output=$(realpath -m "$2")
tool=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
appdir=$(mktemp -d)/b-launcher.AppDir
mkdir -p "$appdir/usr/bin"
cp "$binary" "$appdir/usr/bin/b-launcher"
chmod 755 "$appdir/usr/bin/b-launcher"
cp "$root/assets/images/logo.svg" "$appdir/b-launcher.svg"
cat > "$appdir/b-launcher.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=B Launcher
Exec=b-launcher
Icon=b-launcher
Categories=Game;
Terminal=false
DESKTOP
ln -s usr/bin/b-launcher "$appdir/AppRun"
ARCH=${ARCH:-x86_64} APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream "$appdir" "$output"
rm -rf "$(dirname "$appdir")"
