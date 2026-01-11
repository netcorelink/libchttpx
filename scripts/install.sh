#!/bin/bash
# Usage: curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh

set -e

PREFIX="/usr/local"
RELEASE_URL="https://github.com/netcorelink/libchttpx/releases/latest/download/libchttpx-dev.tar.gz"

# root
if [ "$(id -u)" -ne 0 ]; then
    echo "Warning: it's recommended to run with sudo to install info $PREFIX"
fi

# cjson
if pkg-config --exists cjson; then
    echo "cjson already installed."
else
    echo "cjson not found. Installing...."

    if command -v apt >/dev/null 2>&1; then
        sudo apt update
        sudo apt install -y libcjson-dev
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -Sy --noconfirm cjson
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y cjson-devel
    elif command -v zypper >/dev/null 2>&1; then
        sudo zypper install -y cjson-devel
    else
        echo "Unsupported package manager. Install cjson manually."
        exit 1
    fi
fi

echo "cjson installed successfully!"

TMPDIR=$(mktemp -d)

echo "Bulding in $TMPDIR"
cd "$TMPDIR"

echo "Downloading libchttpx release..."
curl -L "$RELEASE_URL" -o libchttpx.tar.gz

echo "Extracting..."
tar -xzf libchttpx.tar.gz
cd libchttpx-*

mkdir -p .build .out

# Install headers
echo "Installing headers...."
mkdir -p "$PREFIX/include/libchttpx"
cp -r include/* "$PREFIX/include/libchttpx"

# Install library
echo "Installing shared library...."
mkdir -p "$PREFIX/lib"
cp libchttpx.so "$PREFIX/lib/"

# Install pkg-config file
echo "Installing pkg-config file..."
mkdir -p "$PREFIX/lib/pkgconfig"
cp libchttpx.pc "$PREFIX/lib/pkgconfig/"

if command -v ldconfig >/dev/null 2>&1; then
    echo "Updating library cache..."
    ldconfig
fi

echo "libchttpx installed successfully!"
echo "Use it via:"
echo "  gcc main.c \$(pkg-config --cflags --libs libchttpx)"

cd /
rm -rf "$TMPDIR"