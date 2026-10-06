#!/bin/bash
# Usage: curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh

set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
RELEASE_BASE="${RELEASE_BASE:-https://github.com/netcorelink/libchttpx/releases/latest/download}"

if [ "$(id -u)" -ne 0 ]; then
  echo "Warning: it's recommended to run with sudo to install into ${PREFIX}"
fi

os="$(uname -s)"

install_linux() {
  local release_url="${RELEASE_BASE}/libchttpx-dev.tar.gz"

  if pkg-config --exists libcjson 2>/dev/null || pkg-config --exists cjson 2>/dev/null; then
    echo "cjson already installed."
  else
    echo "cjson not found. Installing...."
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

  if pkg-config --exists zlib 2>/dev/null; then
    echo "zlib already installed."
  else
    echo "zlib not found. Installing...."
    if command -v apt >/dev/null 2>&1; then
      sudo apt install -y zlib1g-dev
    elif command -v pacman >/dev/null 2>&1; then
      sudo pacman -Sy --noconfirm zlib
    elif command -v dnf >/dev/null 2>&1; then
      sudo dnf install -y zlib-devel
    elif command -v zypper >/dev/null 2>&1; then
      sudo zypper install -y zlib-devel
    else
      echo "Unsupported package manager. Install zlib manually."
      exit 1
    fi
  fi

  if pkg-config --exists libnghttp2 2>/dev/null; then
    echo "nghttp2 already installed."
  else
    echo "nghttp2 not found. Installing...."
    if command -v apt >/dev/null 2>&1; then
      sudo apt install -y libnghttp2-dev
    elif command -v pacman >/dev/null 2>&1; then
      sudo pacman -Sy --noconfirm nghttp2
    elif command -v dnf >/dev/null 2>&1; then
      sudo dnf install -y libnghttp2-devel
    elif command -v zypper >/dev/null 2>&1; then
      sudo zypper install -y libnghttp2-devel
    else
      echo "Unsupported package manager. Install nghttp2 manually."
      exit 1
    fi
  fi

  local tmpdir
  tmpdir="$(mktemp -d)"
  echo "Downloading libchttpx release..."
  curl -fsSL "${release_url}" -o "${tmpdir}/libchttpx.tar.gz"

  echo "Extracting..."
  tar -xzf "${tmpdir}/libchttpx.tar.gz" -C "${tmpdir}"
  local pkgdir
  pkgdir="$(find "${tmpdir}" -mindepth 1 -maxdepth 1 -type d -name 'libchttpx-*' | head -n 1)"
  if [ -z "${pkgdir}" ]; then
    echo "Could not find extracted package directory." >&2
    exit 1
  fi

  echo "Installing headers...."
  mkdir -p "${PREFIX}/include/libchttpx"
  cp -R "${pkgdir}/include/"* "${PREFIX}/include/libchttpx/"

  echo "Installing shared library...."
  mkdir -p "${PREFIX}/lib"
  cp "${pkgdir}/libchttpx.so" "${PREFIX}/lib/"

  echo "Installing pkg-config file..."
  mkdir -p "${PREFIX}/lib/pkgconfig"
  cp "${pkgdir}/libchttpx.pc" "${PREFIX}/lib/pkgconfig/"

  if command -v ldconfig >/dev/null 2>&1; then
    echo "Updating library cache..."
    ldconfig
  fi

  rm -rf "${tmpdir}"
}

# Compare dotted versions: version_ge A B  =>  A >= B
version_ge() {
  local IFS=.
  # shellcheck disable=SC2086
  set -- $1
  local a1="${1:-0}" a2="${2:-0}" a3="${3:-0}"
  # shellcheck disable=SC2086
  set -- $2
  local b1="${1:-0}" b2="${2:-0}" b3="${3:-0}"

  if [ "${a1}" -ne "${b1}" ]; then [ "${a1}" -gt "${b1}" ]; return; fi
  if [ "${a2}" -ne "${b2}" ]; then [ "${a2}" -gt "${b2}" ]; return; fi
  [ "${a3}" -ge "${b3}" ]
}

select_macos_package() {
  local product_version arch
  product_version="$(sw_vers -productVersion)"
  arch="$(uname -m)"

  echo "Detected macOS ${product_version} (${arch})"

  case "${arch}" in
    arm64)
      if ! version_ge "${product_version}" "11.0"; then
        echo "Apple Silicon requires macOS 11 Big Sur or newer." >&2
        exit 1
      fi
      MACOS_PACKAGE="libchttpx-macos-11-arm64.tar.gz"
      ;;
    x86_64)
      if ! version_ge "${product_version}" "10.15"; then
        echo "Intel Macs require macOS 10.15 Catalina or newer." >&2
        exit 1
      fi
      # Catalina-compatible Intel build also runs on newer Intel macOS releases.
      MACOS_PACKAGE="libchttpx-macos-10.15-x86_64.tar.gz"
      ;;
    *)
      echo "Unsupported macOS architecture: ${arch}" >&2
      exit 1
      ;;
  esac
}

install_macos() {
  select_macos_package
  local release_url="${RELEASE_BASE}/${MACOS_PACKAGE}"
  local tmpdir
  tmpdir="$(mktemp -d)"

  echo "Downloading ${MACOS_PACKAGE}..."
  echo "(self-contained package; Homebrew is not required)"
  if ! curl -fsSL "${release_url}" -o "${tmpdir}/pkg.tar.gz"; then
    echo "Failed to download ${release_url}" >&2
    echo "Make sure this release publishes macOS legacy packages." >&2
    rm -rf "${tmpdir}"
    exit 1
  fi

  echo "Extracting..."
  tar -xzf "${tmpdir}/pkg.tar.gz" -C "${tmpdir}"
  local pkgdir
  pkgdir="$(find "${tmpdir}" -mindepth 1 -maxdepth 1 -type d -name 'libchttpx-macos-*' | head -n 1)"
  if [ -z "${pkgdir}" ]; then
    echo "Could not find extracted macOS package directory." >&2
    rm -rf "${tmpdir}"
    exit 1
  fi

  echo "Installing headers to ${PREFIX}/include/libchttpx ..."
  mkdir -p "${PREFIX}/include/libchttpx"
  cp -R "${pkgdir}/include/libchttpx/"* "${PREFIX}/include/libchttpx/"

  echo "Installing libraries to ${PREFIX}/lib ..."
  mkdir -p "${PREFIX}/lib" "${PREFIX}/lib/pkgconfig" "${PREFIX}/share/libchttpx"
  # Bundled runtime deps ship beside libchttpx.dylib (@loader_path / @rpath).
  cp -R "${pkgdir}/lib/"* "${PREFIX}/lib/"

  if [ -f "${pkgdir}/share/libchttpx/manifest.txt" ]; then
    cp "${pkgdir}/share/libchttpx/manifest.txt" "${PREFIX}/share/libchttpx/manifest.txt"
  fi

  rm -rf "${tmpdir}"
}

case "${os}" in
  Linux)
    install_linux
    ;;
  Darwin)
    install_macos
    ;;
  *)
    echo "Unsupported OS: ${os}" >&2
    echo "libchttpx install.sh supports Linux and macOS." >&2
    exit 1
    ;;
esac

echo "libchttpx installed successfully!"
echo "Use it via:"
echo "  cc main.c \$(pkg-config --cflags --libs libchttpx)"
