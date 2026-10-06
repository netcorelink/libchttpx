#!/bin/bash
# Build a self-contained macOS libchttpx tarball with bundled dependencies.
#
# Usage:
#   scripts/build-macos-package.sh 10.15-x86_64
#   scripts/build-macos-package.sh 11-arm64
#
# Output:
#   dist/macos/libchttpx-macos-<variant>.tar.gz
#
# The package is intended for machines that cannot use modern Homebrew
# (macOS 10.15 Catalina and similar legacy hosts). Dependencies are built
# from source with MACOSX_DEPLOYMENT_TARGET so the resulting dylibs load on
# that OS version without compiling anything on the target Mac.

set -euo pipefail

VARIANT="${1:-}"
if [[ -z "${VARIANT}" ]]; then
  echo "Usage: $0 <10.15-x86_64|11-arm64>" >&2
  exit 1
fi

case "${VARIANT}" in
  10.15-x86_64)
    DEPLOYMENT_TARGET="10.15"
    ARCH="x86_64"
    OPENSSL_PLATFORM="darwin64-x86_64-cc"
    CONFIGURE_HOST="x86_64-apple-darwin19"
    ;;
  11-arm64)
    DEPLOYMENT_TARGET="11.0"
    ARCH="arm64"
    OPENSSL_PLATFORM="darwin64-arm64-cc"
    CONFIGURE_HOST="arm64-apple-darwin20"
    ;;
  *)
    echo "Unsupported variant: ${VARIANT}" >&2
    echo "Expected: 10.15-x86_64 or 11-arm64" >&2
    exit 1
    ;;
esac

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script must run on macOS." >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

VERSION="${LIBCHTTPX_PACKAGE_VERSION:-}"
if [[ -z "${VERSION}" ]]; then
  VERSION="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)"
fi
VERSION="${VERSION:-0.0.0}"

OPENSSL_VERSION="${OPENSSL_VERSION:-3.0.16}"
NGHTTP2_VERSION="${NGHTTP2_VERSION:-1.65.0}"
CJSON_VERSION="${CJSON_VERSION:-1.7.18}"

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
HOST_ARCH="$(uname -m)"
ARCH_FLAGS=(-arch "${ARCH}")
MIN_FLAGS=("-mmacosx-version-min=${DEPLOYMENT_TARGET}")

export MACOSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"
export CFLAGS="${ARCH_FLAGS[*]} ${MIN_FLAGS[*]} -O2"
export CXXFLAGS="${CFLAGS}"
export LDFLAGS="${ARCH_FLAGS[*]} ${MIN_FLAGS[*]}"

if [[ "${HOST_ARCH}" != "${ARCH}" ]]; then
  echo "==> Cross-compiling ${ARCH} on host ${HOST_ARCH}"
fi
WORKDIR="${ROOT}/.macos-build/${VARIANT}"
PREFIX="${WORKDIR}/prefix"
STAGE="${WORKDIR}/stage"
DIST_DIR="${ROOT}/dist/macos"
PACKAGE_NAME="libchttpx-macos-${VARIANT}"
PACKAGE_ROOT="${STAGE}/${PACKAGE_NAME}"

rm -rf "${WORKDIR}"
mkdir -p "${PREFIX}" "${PACKAGE_ROOT}/include/libchttpx" "${PACKAGE_ROOT}/lib/pkgconfig" "${DIST_DIR}" "${WORKDIR}/src"

echo "==> Building macOS package ${PACKAGE_NAME} (version ${VERSION})"
echo "    deployment target: ${DEPLOYMENT_TARGET}"
echo "    architecture:      ${ARCH}"

download() {
  local url="$1"
  local out="$2"
  if [[ -f "${out}" ]]; then
    return 0
  fi
  echo "    downloading $(basename "${out}")"
  curl -fsSL "${url}" -o "${out}"
}

# --- cJSON (compile directly; no CMake required) ---------------------------
build_cjson() {
  local src_dir="${WORKDIR}/src/cJSON-${CJSON_VERSION}"
  download \
    "https://github.com/DaveGamble/cJSON/archive/refs/tags/v${CJSON_VERSION}.tar.gz" \
    "${WORKDIR}/src/cJSON-${CJSON_VERSION}.tar.gz"
  rm -rf "${src_dir}"
  tar -xzf "${WORKDIR}/src/cJSON-${CJSON_VERSION}.tar.gz" -C "${WORKDIR}/src"

  echo "==> Building cJSON ${CJSON_VERSION}"
  "${CC}" "${ARCH_FLAGS[@]}" "${MIN_FLAGS[@]}" -O2 -fPIC \
    -DcJSON_EXPORT_SYMBOLS -shared \
    -o "${PREFIX}/lib/libcjson.1.dylib" \
    "${src_dir}/cJSON.c" \
    -install_name "@rpath/libcjson.1.dylib" \
    -Wl,-compatibility_version,1.0.0 \
    -Wl,-current_version,"${CJSON_VERSION}"

  ln -sf libcjson.1.dylib "${PREFIX}/lib/libcjson.dylib"
  mkdir -p "${PREFIX}/include/cjson"
  cp "${src_dir}/cJSON.h" "${PREFIX}/include/cjson/cJSON.h"
  cat > "${PREFIX}/lib/pkgconfig/libcjson.pc" <<EOF
prefix=${PREFIX}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libcjson
Version: ${CJSON_VERSION}
Description: Ultralightweight JSON parser in ANSI C
Libs: -L\${libdir} -lcjson
Cflags: -I\${includedir}
EOF
}

# --- OpenSSL ---------------------------------------------------------------
build_openssl() {
  local src_dir="${WORKDIR}/src/openssl-${OPENSSL_VERSION}"
  download \
    "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz" \
    "${WORKDIR}/src/openssl-${OPENSSL_VERSION}.tar.gz"
  rm -rf "${src_dir}"
  tar -xzf "${WORKDIR}/src/openssl-${OPENSSL_VERSION}.tar.gz" -C "${WORKDIR}/src"

  echo "==> Building OpenSSL ${OPENSSL_VERSION}"
  (
    cd "${src_dir}"
    ./Configure "${OPENSSL_PLATFORM}" \
      --prefix="${PREFIX}" \
      --libdir=lib \
      shared no-tests \
      "${MIN_FLAGS[@]}"
    make -j"${JOBS}"
    make install_sw
  )

  install_name_tool -id "@rpath/libcrypto.3.dylib" "${PREFIX}/lib/libcrypto.3.dylib"
  install_name_tool -id "@rpath/libssl.3.dylib" "${PREFIX}/lib/libssl.3.dylib"
  install_name_tool -change "${PREFIX}/lib/libcrypto.3.dylib" "@rpath/libcrypto.3.dylib" \
    "${PREFIX}/lib/libssl.3.dylib"
  install_name_tool -add_rpath "@loader_path" "${PREFIX}/lib/libssl.3.dylib" 2>/dev/null || true
}

# --- nghttp2 ---------------------------------------------------------------
build_nghttp2() {
  local src_dir="${WORKDIR}/src/nghttp2-${NGHTTP2_VERSION}"
  download \
    "https://github.com/nghttp2/nghttp2/releases/download/v${NGHTTP2_VERSION}/nghttp2-${NGHTTP2_VERSION}.tar.gz" \
    "${WORKDIR}/src/nghttp2-${NGHTTP2_VERSION}.tar.gz"
  rm -rf "${src_dir}"
  tar -xzf "${WORKDIR}/src/nghttp2-${NGHTTP2_VERSION}.tar.gz" -C "${WORKDIR}/src"

  echo "==> Building nghttp2 ${NGHTTP2_VERSION}"
  (
    cd "${src_dir}"
    configure_args=(
      --prefix="${PREFIX}"
      --libdir="${PREFIX}/lib"
      --enable-lib-only
      --disable-static
      --disable-examples
      --disable-python-bindings
    )
    if [[ "${HOST_ARCH}" != "${ARCH}" ]]; then
      configure_args+=(--host="${CONFIGURE_HOST}" --build="$(clang -dumpmachine)")
    fi
    ./configure "${configure_args[@]}" \
      CC="${CC}" \
      CFLAGS="${CFLAGS}" \
      LDFLAGS="${LDFLAGS}"
    make -j"${JOBS}"
    make install
  )

  local nghttp2_lib
  nghttp2_lib="$(find "${PREFIX}/lib" -name 'libnghttp2.*.dylib' -type f | head -n 1)"
  if [[ -z "${nghttp2_lib}" ]]; then
    echo "libnghttp2 dylib was not produced" >&2
    exit 1
  fi
  install_name_tool -id "@rpath/$(basename "${nghttp2_lib}")" "${nghttp2_lib}"
  ln -sfn "$(basename "${nghttp2_lib}")" "${PREFIX}/lib/libnghttp2.dylib"
}

# --- libchttpx -------------------------------------------------------------
rewrite_to_rpath() {
  local dylib="$1"
  local old
  while IFS= read -r old; do
    [[ -n "${old}" ]] || continue
    case "${old}" in
      "${PREFIX}/lib/"*)
        install_name_tool -change "${old}" "@rpath/$(basename "${old}")" "${dylib}"
        ;;
    esac
  done < <(otool -L "${dylib}" | awk 'NR > 1 {print $1}')
}

build_libchttpx() {
  echo "==> Building libchttpx with TLS (deployment target ${DEPLOYMENT_TARGET})"

  # Do not call the top-level `make clean` here: that target removes
  # .macos-build and dist/macos, including the dependencies and staging
  # directories prepared earlier in this script.
  rm -rf "${ROOT}/.out" "${ROOT}/.build"
  rm -f "${ROOT}/libchttpx.so" "${ROOT}/libchttpx.dylib"

  if [[ ! -f "${PREFIX}/include/nghttp2/nghttp2.h" ]]; then
    echo "nghttp2 headers are missing from ${PREFIX}" >&2
    exit 1
  fi
  if [[ ! -f "${PREFIX}/lib/libssl.3.dylib" || ! -f "${PREFIX}/lib/libcrypto.3.dylib" ]]; then
    echo "OpenSSL libraries are missing from ${PREFIX}" >&2
    exit 1
  fi

  local link_flags="${ARCH_FLAGS[*]} ${MIN_FLAGS[*]} -L${PREFIX}/lib -lcjson -lz -lnghttp2 -lssl -lcrypto -pthread"
  make -C "${ROOT}" \
    TLS=1 \
    CC="${CC}" \
    CFLAGS="-Wall -Wextra -Wpedantic -O2 -Iinclude -Isrc -I${PREFIX}/include ${ARCH_FLAGS[*]} ${MIN_FLAGS[*]} -DCHTTPX_ENABLE_TLS" \
    LIN_LDFLAGS="${link_flags}" \
    libchttpx.dylib

  cp "${ROOT}/libchttpx.dylib" "${PREFIX}/lib/libchttpx.dylib"
  install_name_tool -id "@rpath/libchttpx.dylib" "${PREFIX}/lib/libchttpx.dylib"
  rewrite_to_rpath "${PREFIX}/lib/libchttpx.dylib"
  install_name_tool -add_rpath "@loader_path" "${PREFIX}/lib/libchttpx.dylib" 2>/dev/null || true

  echo "    load commands:"
  otool -L "${PREFIX}/lib/libchttpx.dylib" | sed 's/^/      /'
}

stage_package() {
  echo "==> Staging ${PACKAGE_NAME}"
  local install_lib="/usr/local/lib"

  cp "${ROOT}/include/libchttpx.h" "${PACKAGE_ROOT}/include/libchttpx/"
  cp "${ROOT}/LICENSE" "${PACKAGE_ROOT}/LICENSE"

  # Runtime libraries (absolute install target: /usr/local/lib)
  cp "${PREFIX}/lib/libchttpx.dylib" "${PACKAGE_ROOT}/lib/"
  cp "${PREFIX}/lib/libcjson.1.dylib" "${PACKAGE_ROOT}/lib/"
  ln -sfn libcjson.1.dylib "${PACKAGE_ROOT}/lib/libcjson.dylib"

  local nghttp2_lib nghttp2_base
  nghttp2_lib="$(find "${PREFIX}/lib" -name 'libnghttp2.*.dylib' -type f | head -n 1)"
  nghttp2_base="$(basename "${nghttp2_lib}")"
  cp "${nghttp2_lib}" "${PACKAGE_ROOT}/lib/"
  ln -sfn "${nghttp2_base}" "${PACKAGE_ROOT}/lib/libnghttp2.dylib"

  cp "${PREFIX}/lib/libssl.3.dylib" "${PACKAGE_ROOT}/lib/"
  cp "${PREFIX}/lib/libcrypto.3.dylib" "${PACKAGE_ROOT}/lib/"
  ln -sfn libssl.3.dylib "${PACKAGE_ROOT}/lib/libssl.dylib"
  ln -sfn libcrypto.3.dylib "${PACKAGE_ROOT}/lib/libcrypto.dylib"

  # Final install names for /usr/local so apps can link with -lchttpx and
  # bundled deps resolve next to libchttpx.dylib via @loader_path.
  install_name_tool -id "${install_lib}/libcjson.1.dylib" "${PACKAGE_ROOT}/lib/libcjson.1.dylib"
  install_name_tool -id "${install_lib}/${nghttp2_base}" "${PACKAGE_ROOT}/lib/${nghttp2_base}"
  install_name_tool -id "${install_lib}/libcrypto.3.dylib" "${PACKAGE_ROOT}/lib/libcrypto.3.dylib"
  install_name_tool -id "${install_lib}/libssl.3.dylib" "${PACKAGE_ROOT}/lib/libssl.3.dylib"
  install_name_tool -change "@rpath/libcrypto.3.dylib" "@loader_path/libcrypto.3.dylib" \
    "${PACKAGE_ROOT}/lib/libssl.3.dylib" 2>/dev/null || true
  install_name_tool -change "${PREFIX}/lib/libcrypto.3.dylib" "@loader_path/libcrypto.3.dylib" \
    "${PACKAGE_ROOT}/lib/libssl.3.dylib" 2>/dev/null || true

  install_name_tool -id "${install_lib}/libchttpx.dylib" "${PACKAGE_ROOT}/lib/libchttpx.dylib"
  local old
  while IFS= read -r old; do
    case "${old}" in
      @rpath/*|"${PREFIX}/lib/"*)
        install_name_tool -change "${old}" "@loader_path/$(basename "${old}")" \
          "${PACKAGE_ROOT}/lib/libchttpx.dylib"
        ;;
    esac
  done < <(otool -L "${PACKAGE_ROOT}/lib/libchttpx.dylib" | awk 'NR > 1 {print $1}')

  cat > "${PACKAGE_ROOT}/lib/pkgconfig/libchttpx.pc" <<EOF
prefix=/usr/local
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libchttpx
Description: A compact HTTP/2 server library for C on macOS (bundled runtime deps).
Version: ${VERSION}
Libs: -L\${libdir} -lchttpx
Libs.private: -lz
Cflags: -I\${includedir}/libchttpx
EOF

  cat > "${PACKAGE_ROOT}/manifest.txt" <<EOF
include/libchttpx/libchttpx.h
lib/libchttpx.dylib
lib/libcjson.1.dylib
lib/libcjson.dylib
lib/${nghttp2_base}
lib/libnghttp2.dylib
lib/libssl.3.dylib
lib/libssl.dylib
lib/libcrypto.3.dylib
lib/libcrypto.dylib
lib/pkgconfig/libchttpx.pc
share/libchttpx/manifest.txt
EOF

  mkdir -p "${PACKAGE_ROOT}/share/libchttpx"
  cp "${PACKAGE_ROOT}/manifest.txt" "${PACKAGE_ROOT}/share/libchttpx/manifest.txt"

  cat > "${PACKAGE_ROOT}/README.txt" <<EOF
libchttpx ${VERSION} — macOS ${VARIANT}

Minimum OS: macOS ${DEPLOYMENT_TARGET}
Architecture: ${ARCH}
TLS: enabled (bundled OpenSSL ${OPENSSL_VERSION})

Bundled libraries:
  - cJSON ${CJSON_VERSION}
  - nghttp2 ${NGHTTP2_VERSION}
  - OpenSSL ${OPENSSL_VERSION}
  - system zlib

Install with:
  curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh

Or manually:
  sudo cp -R include/libchttpx /usr/local/include/
  sudo cp -R lib/* /usr/local/lib/
  sudo mkdir -p /usr/local/share/libchttpx
  sudo cp share/libchttpx/manifest.txt /usr/local/share/libchttpx/
EOF
}

pack_tarball() {
  local out="${DIST_DIR}/${PACKAGE_NAME}.tar.gz"
  echo "==> Creating ${out}"
  tar -czf "${out}" -C "${STAGE}" "${PACKAGE_NAME}"
  ls -lh "${out}"
  echo "Package ready: ${out}"
}

mkdir -p "${PREFIX}/lib" "${PREFIX}/include" "${PREFIX}/lib/pkgconfig"

build_cjson
build_openssl
build_nghttp2
build_libchttpx
stage_package
pack_tarball
