#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "Bacons Helper OBS Companion root: $root"
cmake --version
ninja --version
pkg-config --version

echo "Installed OBS/Qt/libsecret packages:"
dpkg-query -W -f='${Package} ${Version}\n' libobs-dev libobs0t64 obs-studio qt6-base-dev libsecret-1-dev 2>/dev/null || true

libobs_config="$(dpkg -L libobs-dev 2>/dev/null | grep '/libobsConfig\.cmake$' | head -n1 || true)"
frontend_config="$(dpkg -L libobs-dev 2>/dev/null | grep '/obs-frontend-apiConfig\.cmake$' | head -n1 || true)"
qt6_config="$(dpkg -L qt6-base-dev 2>/dev/null | grep '/Qt6Config\.cmake$' | head -n1 || true)"

if [[ -z "$libobs_config" || ! -f "$libobs_config" ]]; then
  echo "libobsConfig.cmake was not found in libobs-dev." >&2
  dpkg -L libobs-dev 2>/dev/null | grep -E '/cmake/|libobs' || true
  exit 1
fi
if [[ -z "$frontend_config" || ! -f "$frontend_config" ]]; then
  echo "obs-frontend-apiConfig.cmake was not found in libobs-dev." >&2
  dpkg -L libobs-dev 2>/dev/null | grep -E 'frontend|/cmake/' || true
  exit 1
fi
if [[ -z "$qt6_config" || ! -f "$qt6_config" ]]; then
  echo "Qt6Config.cmake was not found in qt6-base-dev." >&2
  dpkg -L qt6-base-dev 2>/dev/null | grep -E 'Qt6Config\.cmake|/cmake/Qt6' || true
  exit 1
fi

libobs_dir="$(dirname "$libobs_config")"
frontend_dir="$(dirname "$frontend_config")"
qt6_dir="$(dirname "$qt6_config")"

echo "Resolved Linux CMake packages:"
echo "  libobs_DIR=$libobs_dir"
echo "  obs-frontend-api_DIR=$frontend_dir"
echo "  Qt6_DIR=$qt6_dir"

pkg-config --modversion libsecret-1

cmake --preset ubuntu-x86_64 --fresh \
  -Dlibobs_DIR="$libobs_dir" \
  -Dobs-frontend-api_DIR="$frontend_dir" \
  -DQt6_DIR="$qt6_dir"

cmake --build --preset ubuntu-x86_64 --parallel
rm -rf dist/linux-x86_64
mkdir -p dist
cmake --install build_x86_64 --prefix "$root/dist/linux-x86_64"

plugin="$(find "$root/dist/linux-x86_64" -type f -name 'bacons-helper.so' -print -quit)"
if [[ -z "$plugin" ]]; then
  find "$root/dist/linux-x86_64" -type f -print
  echo 'Expected bacons-helper.so was not staged.' >&2
  exit 1
fi

locale="$(find "$root/dist/linux-x86_64" -type f -path '*/obs-plugins/bacons-helper/locale/en-US.ini' -print -quit)"
if [[ -z "$locale" ]]; then
  find "$root/dist/linux-x86_64" -type f -print
  echo 'Expected en-US.ini was not staged.' >&2
  exit 1
fi

# Show dynamic dependencies in CI so a bad link is obvious before packaging.
ldd "$plugin" || true

tar -C "$root/dist/linux-x86_64" -czf "$root/dist/bacons-helper-linux-x86_64.tar.gz" .
printf 'Created %s\n' "$root/dist/bacons-helper-linux-x86_64.tar.gz"
