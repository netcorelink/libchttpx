#!/bin/bash
# Usage: curl -s https://github.com/netcorelink/libchttpx/scripts/install.sh | sudo sh

set -e

PREFIX="/usr/local"
REPO="https://github.com/netcorelink/libchttpx.git"

# root
if [ "$(id -u)" -ne 0 ]; then
    echo "Warning: it's recommended to run with sudo to install info $PREFIX"
fi

# Check for required commands
for cmd in git gcc make; do
    if ! command type $cmd >/dev/null 2>$1; then
        echo "$cmd is not installed. Attempting to install..."
        # Debian/Ubuntu
        if command -v apt-get  >/dev/null 2>&1; then
            apt-get update
            apt-get install -y $cmd
        elif command -v dnf >/dev/null 2>&1; then
            dnf install -y $cmd
        elif command -v yum >/dev/null 2>&1; then
            yum install -y $cmd
        else
            echo "Cannot install $cmd automatically. Please install it manually."
            exit 1
        fi
    fi
done

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