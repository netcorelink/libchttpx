#!/bin/bash
# Usage: curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/uninstall.sh | sudo sh

set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"

echo "Uninstalling libchttpx from ${PREFIX}..."

if [ -d "${PREFIX}/include/libchttpx" ]; then
  rm -rf "${PREFIX}/include/libchttpx"
  echo "Removed headers"
else
  echo "No headers found"
fi

remove_file() {
  local path="$1"
  if [ -e "${path}" ] || [ -L "${path}" ]; then
    rm -f "${path}"
    echo "Removed ${path}"
  fi
}

# Linux shared library
remove_file "${PREFIX}/lib/libchttpx.so"

# macOS shared library + bundled legacy-package runtime deps.
# Prefer the package manifest when present so we only delete what we installed.
MANIFEST="${PREFIX}/share/libchttpx/manifest.txt"
if [ -f "${MANIFEST}" ]; then
  while IFS= read -r rel || [ -n "${rel}" ]; do
    [ -n "${rel}" ] || continue
    case "${rel}" in
      include/*|share/*) continue ;;
    esac
    remove_file "${PREFIX}/${rel}"
  done < "${MANIFEST}"
  rm -f "${MANIFEST}"
  rmdir "${PREFIX}/share/libchttpx" 2>/dev/null || true
else
  remove_file "${PREFIX}/lib/libchttpx.dylib"
fi

remove_file "${PREFIX}/lib/pkgconfig/libchttpx.pc"

if command -v ldconfig >/dev/null 2>&1; then
  echo "Updating library cache..."
  ldconfig
fi

echo "libchttpx successfully uninstalled!"
