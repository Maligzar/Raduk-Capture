#!/usr/bin/env bash
# Publish wiki/*.md to the GitHub wiki for Maligzar/Raduk-Capture.
#
# The GitHub wiki is a separate git repository (<repo>.wiki.git) with no
# REST API, so publishing is a clone, copy, commit, push.
#
# Prerequisites:
#   - The wiki must exist. If it does not, create one page once via the
#     web UI: https://github.com/Maligzar/Raduk-Capture/wiki
#   - Push access to the wiki repo (git credentials or gh auth).
#
# Usage:
#   ./wiki/publish-wiki.sh            # publish
#   ./wiki/publish-wiki.sh --dry-run  # show what would change

set -euo pipefail

REPO_SLUG="${WIKI_REPO:-Maligzar/Raduk-Capture}"
WIKI_URL="${WIKI_URL:-https://github.com/${REPO_SLUG}.wiki.git}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRY_RUN=0
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=1

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Cloning $WIKI_URL"
if ! git clone --quiet "$WIKI_URL" "$WORK/wiki" 2>/dev/null; then
  echo "ERROR: could not clone the wiki repository." >&2
  echo "       If the wiki has never been initialised, create one page here first:" >&2
  echo "       https://github.com/${REPO_SLUG}/wiki" >&2
  exit 1
fi

echo "==> Copying pages"
# Remove pages the wiki has that the source no longer defines, so a rename
# in wiki/ does not leave an orphan page behind.
for existing in "$WORK/wiki"/*.md; do
  [[ -e "$existing" ]] || continue
  base="$(basename "$existing")"
  if [[ ! -f "$SRC_DIR/$base" ]]; then
    echo "    - removing stale page: $base"
    git -C "$WORK/wiki" rm --quiet -- "$base"
  fi
done

for page in "$SRC_DIR"/*.md; do
  base="$(basename "$page")"
  [[ "$base" == "README.md" ]] && continue
  cp "$page" "$WORK/wiki/$base"
done

git -C "$WORK/wiki" add -A

if git -C "$WORK/wiki" diff --cached --quiet; then
  echo "==> No changes; wiki is already up to date."
  exit 0
fi

echo "==> Changes to publish:"
git -C "$WORK/wiki" diff --cached --stat

if (( DRY_RUN )); then
  echo "==> Dry run; nothing pushed."
  exit 0
fi

git -C "$WORK/wiki" -c user.name="${GIT_AUTHOR_NAME:-wiki-publish}" \
    -c user.email="${GIT_AUTHOR_EMAIL:-wiki-publish@localhost}" \
    commit --quiet -m "Publish wiki from wiki/ ($(date -u +%Y-%m-%dT%H:%M:%SZ))"

echo "==> Pushing"
git -C "$WORK/wiki" push --quiet origin HEAD
echo "==> Done: https://github.com/${REPO_SLUG}/wiki"
