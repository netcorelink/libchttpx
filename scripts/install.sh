#!/bin/bash
# Install libchttpx from GitHub Releases.
#
# Latest:
#   curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
#
# Specific version:
#   curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh -s -- --version=1.5.5
#   curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh -s -- -v=1.5.5
#   curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh -s -- --version v1.5.5

set -e

PREFIX="/usr/local"
VERSION=""
RELEASE_URL="https://github.com/netcorelink/libchttpx/releases/latest/download/libchttpx-dev.tar.gz"

usage() {
    cat <<'EOF'
Usage: install.sh [options]

Options:
  --version=VER, -v=VER   Release tag or version (e.g. 1.5.5 or v1.5.5)
  --version VER, -v VER   Same as above
  --prefix=PATH           Install prefix (default: /usr/local)
  -h, --help              Show this help

Examples:
  curl -s .../install.sh | sudo sh
  curl -s .../install.sh | sudo sh -s -- --version=1.5.5
  curl -s .../install.sh | sudo sh -s -- -v=v1.5.5
EOF
}

normalize_version_tag() {
    case "$1" in
        v*) echo "$1" ;;
        *) echo "v$1" ;;
    esac
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version=*)
            VERSION="${1#*=}"
            ;;
        -v=*)
            VERSION="${1#*=}"
            ;;
        --version|-v)
            if [ -z "${2:-}" ]; then
                echo "Error: $1 requires a value" >&2
                exit 1
            fi
            VERSION="$2"
            shift
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [ -n "$VERSION" ]; then
    TAG="$(normalize_version_tag "$VERSION")"
    RELEASE_URL="https://github.com/netcorelink/libchttpx/releases/download/${TAG}/libchttpx-dev.tar.gz"
    echo "Installing libchttpx ${TAG}..."
else
    echo "Installing libchttpx (latest release)..."
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "Warning: it's recommended to run with sudo to install into $PREFIX"
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

echo "cjson installed successfully!"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "Building in $TMPDIR"
cd "$TMPDIR"

echo "Downloading libchttpx release..."
curl -fsSL "$RELEASE_URL" -o libchttpx.tar.gz

echo "Extracting..."
tar -xzf libchttpx.tar.gz
cd libchttpx-*

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

echo "libchttpx installed successfully!"
echo "Use it via:"
echo "  gcc main.c \$(pkg-config --cflags --libs libchttpx) -lcjson"
