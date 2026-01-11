#!/bin/bash
# Usage: curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh

set -e

PREFIX="/usr/local"
REPO="https://github.com/netcorelink/libchttpx.git"

# root
if [ "$(id -u)" -ne 0 ]; then
    echo "Warning: it's recommended to run with sudo to install info $PREFIX"
fi

TMPDIR=$(mktemp -d)

echo "Bulding in $TMPDIR"
cd "$TMPDIR"

echo "Cloning libchttpx repository...."
git clone --depth=1 "$REPO"
cd libchttpx

mkdir -p .build
mkdir -p .out

echo "Building libchttpx...."
make libchttpx.so

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
echo "  gcc main.c \`pkg-config --cflags --libs libchttpx\`"

cd /
rm -rf "$TMPDIR"