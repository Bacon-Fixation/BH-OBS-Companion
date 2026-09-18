#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "Bacons Helper OBS Companion root: $root"
flatpak --version
flatpak-builder --version

flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user --noninteractive -y flathub com.obsproject.Studio//stable org.freedesktop.Sdk//25.08

rm -rf flatpak-build flatpak-repo
mkdir -p dist
flatpak-builder --user --force-clean --repo=flatpak-repo flatpak-build flatpak/com.obsproject.Studio.Plugin.BaconsHelper.yml
flatpak build-bundle flatpak-repo dist/bacons-helper-obs-flatpak-x86_64.flatpak com.obsproject.Studio.Plugin.BaconsHelper stable

test -s dist/bacons-helper-obs-flatpak-x86_64.flatpak
printf 'Created %s\n' "$root/dist/bacons-helper-obs-flatpak-x86_64.flatpak"
