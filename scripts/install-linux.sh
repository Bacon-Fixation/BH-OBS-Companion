#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
data_home="${XDG_DATA_HOME:-${HOME}/.local/share}"
config_home="${XDG_CONFIG_HOME:-${HOME}/.config}"
plugin_root="$data_home/obs-studio/plugins"
package_root="$script_dir"
migrate_legacy=false
if [[ ! -f "$package_root/bacons-helper/bacons-helper.so" ]]; then package_root="$script_dir/../dist/linux-x86_64"; fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --package-root|--plugin-root)
      if [[ $# -lt 2 ]]; then echo "Missing value for $1" >&2; exit 2; fi
      case "$1" in --package-root) package_root="$2";; --plugin-root) plugin_root="$2";; esac
      shift 2;;
    --migrate-legacy) migrate_legacy=true; shift;;
    *) echo "Usage: $0 [--package-root DIR] [--plugin-root DIR] [--migrate-legacy]" >&2; exit 2;;
  esac
done
if command -v pgrep >/dev/null && pgrep -x obs >/dev/null; then echo 'Close OBS Studio before installing.' >&2; exit 1; fi
source_dir="$(cd "$package_root/bacons-helper" && pwd)"
for relative in bacons-helper.so data/manifest.json data/locale/en-US.ini data/language-guesser/model.json; do
  test -s "$source_dir/$relative" || { echo "Incomplete plugin bundle: $relative" >&2; exit 1; }
done
if ! grep -Eq '"id"[[:space:]]*:[[:space:]]*"bacons-helper"' "$source_dir/data/manifest.json"; then echo 'Invalid Bacons Helper plugin manager manifest.' >&2; exit 1; fi
if [[ -L "$package_root/bacons-helper" ]] || [[ -n "$(find "$source_dir" -type l -print -quit)" ]]; then echo 'Plugin bundle must not contain symbolic links.' >&2; exit 1; fi
mkdir -p "$plugin_root"
plugin_root="$(cd "$plugin_root" && pwd)"
target="$plugin_root/bacons-helper"
if [[ "$source_dir" == "$target" ]]; then echo 'Extract the release into a separate directory first.' >&2; exit 1; fi
backup_root="$(dirname "$plugin_root")/plugin-backups/bacons-helper/$(date +%Y%m%d-%H%M%S)-$$"
staging="$(mktemp -d "$plugin_root/.bacons-helper-stage-XXXXXX")"
previous_moved=false
cleanup() {
  local status=$?
  if [[ "$previous_moved" == true && ! -e "$target" ]]; then mv -- "$backup_root/previous-bundle" "$target"; fi
  rm -rf -- "$staging"
  exit "$status"
}
trap cleanup EXIT
cp -a "$source_dir/." "$staging/"
if [[ -e "$target" || -L "$target" ]]; then
  mkdir -p "$backup_root"
  mv -- "$target" "$backup_root/previous-bundle"
  previous_moved=true
fi
mv -- "$staging" "$target"
legacy="$config_home/obs-studio/plugins/bacons-helper"
if [[ "$migrate_legacy" == true && "$legacy" != "$target" && ( -e "$legacy" || -L "$legacy" ) ]]; then
  mkdir -p "$backup_root"
  mv -- "$legacy" "$backup_root/legacy-user-bundle"
fi
printf 'Installed Bacons Helper to %s\n' "$target"
if [[ -d "$backup_root" ]]; then printf 'Previous files backed up to %s\n' "$backup_root"; fi
printf 'Open Tools -> Plugin Manager in OBS Studio 33+. Restart OBS after changing its enabled state.\n'
