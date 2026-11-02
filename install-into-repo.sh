#!/bin/bash
set -euo pipefail
SOURCE="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-}"
if [[ -z "$DEST" || ! -d "$DEST/.git" ]]; then
  echo "Usage: bash install-into-repo.sh /path/to/NI-KORE-macOS-driver" >&2
  exit 2
fi
for dir in automation .github/workflows; do
  mkdir -p "$DEST/$dir"
  for file in "$SOURCE/$dir"/*; do
    [[ -f "$file" ]] || continue
    if [[ -e "$DEST/$dir/$(basename "$file")" ]]; then
      echo "Refusing to overwrite $DEST/$dir/$(basename "$file")" >&2
      exit 3
    fi
    cp -p "$file" "$DEST/$dir/"
  done
done
cp "$SOURCE/README-SETUP.md" "$DEST/README-AUTOMATION.md"
echo "Installed additive files into $DEST. Review 'git status' before committing."
