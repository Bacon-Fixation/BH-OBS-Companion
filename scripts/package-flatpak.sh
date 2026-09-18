#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

manifest="flatpak/com.obsproject.Studio.Plugin.BaconsHelper.yml"
repo="$root/flatpak-repo"
builddir="$root/flatpak-build"
artifact="$root/dist/bacons-helper-obs-flatpak-x86_64.flatpak"

echo "Bacons Helper OBS Companion root: $root"
flatpak --version
flatpak-builder --version

flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

rm -rf "$builddir" "$repo"
mkdir -p "$root/dist"

# Let flatpak-builder resolve the OBS runtime and matching 25.08 SDK itself.
# This avoids a second, separately-maintained runtime-install step in CI.
flatpak-builder \
  --user \
  --force-clean \
  --install-deps-from=flathub \
  --default-branch=stable \
  --repo="$repo" \
  "$builddir" \
  "$manifest"

echo "Repository refs produced by Flatpak build:"
ostree refs --repo="$repo" || true

# OBS plugin extensions are exported as runtime refs, not app refs.
flatpak build-bundle \
  --runtime \
  --arch=x86_64 \
  --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
  "$repo" \
  "$artifact" \
  com.obsproject.Studio.Plugin.BaconsHelper \
  stable

test -s "$artifact"
printf 'Created %s\n' "$artifact"
