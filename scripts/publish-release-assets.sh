#!/usr/bin/env bash
set -euo pipefail
: "${RELEASE_TAG:?RELEASE_TAG must name the release tag}"
: "${GH_REPO:?GH_REPO must name the owner/repository}"
asset_directory="${1:-release-assets}"
# Validate all packages before making any release changes.
assets=(
  "$asset_directory/bacons-helper-windows-x64.zip"
  "$asset_directory/bacons-helper-windows-x64-obs31.1-32.zip"
  "$asset_directory/bacons-helper-linux-x86_64.tar.gz"
  "$asset_directory/bacons-helper-obs-flatpak-x86_64.flatpak"
)
for asset in "${assets[@]}"; do
  if [[ ! -s "$asset" ]]; then
    printf 'Missing or empty release package: %s\n' "$asset" >&2
    exit 1
  fi
done
# Existing release text, title, draft/prerelease state and latest designation
# remain unchanged. Reruns replace matching package assets only.
if gh release view "$RELEASE_TAG" --repo "$GH_REPO" --json tagName >/dev/null 2>&1; then
  gh release upload "$RELEASE_TAG" "${assets[@]}" --repo "$GH_REPO" --clobber
else
  # Retain the existing tag-push publishing behavior, but never create a tag
  # implicitly or attach a branch build to a mistyped release tag.
  gh release create "$RELEASE_TAG" "${assets[@]}" --repo "$GH_REPO" --verify-tag --generate-notes
fi
