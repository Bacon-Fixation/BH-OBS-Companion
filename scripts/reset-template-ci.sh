#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW_DIR="$ROOT/.github/workflows"

[[ -d "$WORKFLOW_DIR" ]] || { echo "Workflow directory not found: $WORKFLOW_DIR" >&2; exit 1; }

for file in "$WORKFLOW_DIR"/*; do
  [[ -f "$file" ]] || continue
  case "$(basename "$file")" in
    build.yml|push.yaml|pr-pull.yaml) ;;
    *) echo "Removing stale/template workflow: $(basename "$file")"; rm -f "$file" ;;
  esac
done

echo
echo 'Remaining workflows:'
find "$WORKFLOW_DIR" -maxdepth 1 -type f -printf '%f\n' | sort
echo
echo 'Commit the workflow replacements/deletions:'
echo '  git add -A'
echo '  git commit -m "Use Bacons Helper OBS Companion CI"'
echo '  git push'
