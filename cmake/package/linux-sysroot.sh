#!/bin/sh
set -eu
out=$(realpath -m "$1")
work=$(mktemp -d)
mkdir -p "$out"
cd "$work"
apt-get download \
    libx11-dev libx11-6 libxext-dev libxext6 x11proto-dev libxrandr-dev libxrandr2 libxcursor-dev libxcursor1 \
    libxi-dev libxi6 libxfixes-dev libxfixes3 libxss-dev libxss1 libxrender-dev libxrender1 libxtst-dev libxtst6 \
    libxcb1-dev libxcb1 libxau-dev libxau6 libxdmcp-dev libxdmcp6 libbsd0 libmd0 \
    libwayland-dev libwayland-client0 libwayland-cursor0 libwayland-egl1 libwayland-server0 \
    libxkbcommon-dev libxkbcommon0 libdecor-0-dev libdecor-0-0 libffi-dev libffi8 \
    libegl-dev libegl1 libgl-dev libgl1 libglx-dev libglx0 libopengl0 libglvnd0 \
    libfontconfig-dev libfontconfig1 libdbus-1-dev libdbus-1-3 libudev-dev libudev1 \
    libpulse-dev libpulse0 libasound2-dev libasound2t64 libpipewire-0.3-dev libpipewire-0.3-0t64 libspa-0.2-dev \
    libibus-1.0-dev libibus-1.0-5 libglib2.0-dev libglib2.0-0t64 libglib2.0-dev-bin \
    libpcre2-dev zlib1g-dev libmount-dev libblkid-dev libselinux1-dev libsepol-dev
for package in *.deb; do
    dpkg-deb -x "$package" "$out"
done
rm -rf "$work"
