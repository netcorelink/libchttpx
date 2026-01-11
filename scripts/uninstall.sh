#!/bin/bash
# Usage: curl -s https://github.com/netcorelink/libchttpx/scripts/uninstall.sh | sudo sh

set -e

PREFIX="/usr/local"

echo "Uninstalling libchttpx from $PREFIX..."

if [ -d "$PREFIX/include/libchttpx" ]; then
    rm -rf "$PREFIX/include/libchttpx"
    echo "Removed headers"
else
    echo "No headers found"
fi

# Remove library
if [ -f "$PREFIX/lib/libchttpx.so" ]; then
    rm -f "$PREFIX/lib/libchttpx.so"
    echo "Removed shared library"
else
    echo "No library found"
fi

# Remove pkg-config
if [ -f "$PREFIX/lib/pkgconfig/libchttpx.pc" ]; then
    rm -f "$PREFIX/lib/pkgconfig/libchttpx.pc"
    echo "Removed pkg-config file"
else
    echo "No pkg-config file found"
fi

# Update library cache (Linux only)
if command -v ldconfig >/dev/null 2>&1; then
    echo "Updating library cache..."
    ldconfig
fi

echo "libchttpx successfully uninstalled!"
