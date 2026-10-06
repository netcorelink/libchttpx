#!/usr/bin/env bash
# Usage: curl -fsSL https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | bash
#
# Run as the current user. The installer asks for sudo only for privileged
# operations. This avoids macOS zsh suspending a "curl | sudo sh" pipeline
# while sudo waits for terminal input.

set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
RELEASE_BASE="${RELEASE_BASE:-https://github.com/netcorelink/libchttpx/releases/latest/download}"

run_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
    return
  fi

  if ! command -v sudo >/dev/null 2>&1; then
    echo "Administrator privileges are required to install into ${PREFIX}, but sudo is not available." >&2
    exit 1
  fi

  sudo "$@"
}

if [ "$(id -u)" -ne 0 ]; then
  echo "Installing into ${PREFIX}; sudo may ask for your password when system files are written."
fi

os="$(uname -s)"

install_linux() {
  local release_url="${RELEASE_BASE}/libchttpx-dev.tar.gz"

  if pkg-config --exists libcjson 2>/dev/null || pkg-config --exists cjson 2>/dev/null; then
    echo "cjson already installed."
  else
    echo "cjson not found. Installing...."
    if command -v apt >/dev/null 2>&1; then
      run_root apt install -y libcjson-dev
    elif command -v pacman >/dev/null 2>&1; then
      run_root pacman -Sy --noconfirm cjson
    elif command -v dnf >/dev/null 2>&1; then
      run_root dnf install -y cjson-devel
    elif command -v zypper >/dev/null 2>&1; then
      run_root zypper install -y cjson-devel
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
      run_root apt install -y zlib1g-dev
    elif command -v pacman >/dev/null 2>&1; then
      run_root pacman -Sy --noconfirm zlib
    elif command -v dnf >/dev/null 2>&1; then
      run_root dnf install -y zlib-devel
    elif command -v zypper >/dev/null 2>&1; then
      run_root zypper install -y zlib-devel
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
      run_root apt install -y libnghttp2-dev
    elif command -v pacman >/dev/null 2>&1; then
      run_root pacman -Sy --noconfirm nghttp2
    elif command -v dnf >/dev/null 2>&1; then
      run_root dnf install -y libnghttp2-devel
    elif command -v zypper >/dev/null 2>&1; then
      run_root zypper install -y libnghttp2-devel
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
  run_root mkdir -p "${PREFIX}/include/libchttpx"
  run_root cp -R "${pkgdir}/include/"* "${PREFIX}/include/libchttpx/"

  echo "Installing shared library...."
  run_root mkdir -p "${PREFIX}/lib"
  run_root cp "${pkgdir}/libchttpx.so" "${PREFIX}/lib/"

  echo "Installing pkg-config file..."
  run_root mkdir -p "${PREFIX}/lib/pkgconfig"
  run_root cp "${pkgdir}/libchttpx.pc" "${PREFIX}/lib/pkgconfig/"

  if command -v ldconfig >/dev/null 2>&1; then
    echo "Updating library cache..."
    run_root ldconfig
  fi

  rm -rf "${tmpdir}"
}

# Compare dotted versions: version_ge A B  =>  A >= B
version_ge() {
  local a1 a2 a3 b1 b2 b3

  IFS=. read -r a1 a2 a3 <<< "$1"
  IFS=. read -r b1 b2 b3 <<< "$2"

  a1="${a1:-0}"; a2="${a2:-0}"; a3="${a3:-0}"
  b1="${b1:-0}"; b2="${b2:-0}"; b3="${b3:-0}"

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
  run_root mkdir -p "${PREFIX}/include/libchttpx"
  run_root cp -R "${pkgdir}/include/libchttpx/"* "${PREFIX}/include/libchttpx/"

  echo "Installing libraries to ${PREFIX}/lib ..."
  run_root mkdir -p "${PREFIX}/lib" "${PREFIX}/lib/pkgconfig" "${PREFIX}/share/libchttpx"
  # Bundled runtime deps ship beside libchttpx.dylib (@loader_path / @rpath).
  run_root cp -R "${pkgdir}/lib/"* "${PREFIX}/lib/"

  if [ -f "${pkgdir}/share/libchttpx/manifest.txt" ]; then
    run_root cp "${pkgdir}/share/libchttpx/manifest.txt" "${PREFIX}/share/libchttpx/manifest.txt"
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
