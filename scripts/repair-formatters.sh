#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(git -C "$project_root" rev-parse --show-toplevel)"
project_prefix="$(git -C "$project_root" rev-parse --show-prefix)"
for entry in 'run-clang-format:clang' 'run-gersemi:gersemi' 'run-swift-format:swift'; do
  name="${entry%%:*}"
  formatter="${entry#*:}"
  relative_path="build-aux/$name"
  target_path="$project_root/$relative_path"
  # Remove the entry itself to avoid following an existing symlink.
  rm -f -- "$target_path"
  {
    printf '%s\n' '#!/usr/bin/env zsh' '' '# Regular file: portable across Git checkouts without symlink support.' 'typeset -g SCRIPT_HOME=${0:A:h}'
    printf 'typeset -g FORMATTER_NAME=%s\n' "$formatter"
    printf '%s\n' 'source "${SCRIPT_HOME}/.run-format.zsh" "$@"'
  } > "$target_path"
  chmod +x "$target_path"
  blob="$(git -C "$project_root" hash-object -w -- "$relative_path")"
  git -C "$repo_root" update-index --add --cacheinfo "100755,$blob,$project_prefix$relative_path"
done
printf '%s\n' 'Repaired and staged regular executable launchers (100755). Review, commit and push the patch.'
git -C "$repo_root" ls-files --stage -- "${project_prefix}build-aux/run-clang-format" "${project_prefix}build-aux/run-gersemi" "${project_prefix}build-aux/run-swift-format"
