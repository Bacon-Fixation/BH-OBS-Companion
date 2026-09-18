#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "Bacons Helper OBS Companion root: $root"
cmake --version

cmake --preset ubuntu-x86_64 --fresh
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

tar -C "$root/dist/linux-x86_64" -czf "$root/dist/bacons-helper-linux-x86_64.tar.gz" .
printf 'Created %s\n' "$root/dist/bacons-helper-linux-x86_64.tar.gz"
