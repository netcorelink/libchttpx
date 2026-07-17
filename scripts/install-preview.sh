#!/bin/bash
# Install libchttpx from a PR preview build (GitHub Actions artifact).
#
# Usage:
#   curl -sL https://raw.githubusercontent.com/OWNER/REPO/BRANCH/scripts/install-preview.sh | sudo bash -s -- RUN_ID PR_NUMBER [REPO]
#
# Example:
#   curl -sL https://raw.githubusercontent.com/netcorelink/libchttpx/feat/ws/scripts/install-preview.sh | sudo bash -s -- 1234567890 42
#
# Requires: gh (GitHub CLI) authenticated — run `gh auth login` once.

set -e

RUN_ID="${1:?Usage: install-preview.sh RUN_ID PR_NUMBER [REPO]}"
PR_NUMBER="${2:?Usage: install-preview.sh RUN_ID PR_NUMBER [REPO]}"
REPO="${3:-netcorelink/libchttpx}"

PREFIX="${PREFIX:-/usr/local}"
ARTIFACT_NAME="libchttpx-dev-pr-${PR_NUMBER}"

if [ "$(id -u)" -ne 0 ]; then
    echo "Warning: it's recommended to run with sudo to install into $PREFIX"
fi

if ! command -v gh >/dev/null 2>&1; then
    echo "Error: GitHub CLI (gh) is required."
    echo "Install: https://cli.github.com/"
    echo "Then authenticate: gh auth login"
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "Error: gh is not authenticated. Run: gh auth login"
    exit 1
fi

if pkg-config --exists cjson; then
    echo "cjson already installed."
else
    echo "cjson not found. Installing..."

    if command -v apt >/dev/null 2>&1; then
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

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "Downloading preview build (run $RUN_ID, PR #$PR_NUMBER)..."
cd "$TMPDIR"
gh run download "$RUN_ID" -R "$REPO" -n "$ARTIFACT_NAME"

echo "Extracting..."
tar -xzf libchttpx-dev.tar.gz
cd libchttpx-dev

echo "Installing headers..."
mkdir -p "$PREFIX/include/libchttpx"
cp -r include/* "$PREFIX/include/libchttpx"

echo "Installing shared library..."
mkdir -p "$PREFIX/lib"
cp libchttpx.so "$PREFIX/lib/"

echo "Installing pkg-config file..."
mkdir -p "$PREFIX/lib/pkgconfig"
cp libchttpx.pc "$PREFIX/lib/pkgconfig/"

if command -v ldconfig >/dev/null 2>&1; then
    echo "Updating library cache..."
    ldconfig
fi

echo "Preview libchttpx from PR #$PR_NUMBER installed successfully!"
echo "Use it via:"
echo "  gcc main.c \$(pkg-config --cflags --libs libchttpx) -lcjson"
