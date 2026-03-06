#!/usr/bin/env bash
set -euo pipefail

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOMLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SKILL_NAME="loomle-skill"
SKILL_SOURCE=""
OUTPUT_ZIP=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--name <skill-name>] [--source <skill-path>] [--output <zip-path>]

Create a release zip for a skill folder.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)
      [[ $# -ge 2 ]] || fail "--name requires a value"
      SKILL_NAME="$2"
      shift 2
      ;;
    --source)
      [[ $# -ge 2 ]] || fail "--source requires a value"
      SKILL_SOURCE="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || fail "--output requires a value"
      OUTPUT_ZIP="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "Unknown argument: $1"
      ;;
  esac
done

if [[ -z "$SKILL_SOURCE" ]]; then
  SKILL_SOURCE="$LOOMLE_DIR/skills/$SKILL_NAME"
fi
if [[ -z "$OUTPUT_ZIP" ]]; then
  OUTPUT_ZIP="$PWD/${SKILL_NAME}.zip"
fi

SKILL_SOURCE="$(cd "$SKILL_SOURCE" && pwd)"
OUTPUT_ZIP="$(python3 - <<'PY' "$OUTPUT_ZIP"
import pathlib
import sys
print(str(pathlib.Path(sys.argv[1]).expanduser().resolve()))
PY
)"

[[ -d "$SKILL_SOURCE" ]] || fail "Skill source does not exist: $SKILL_SOURCE"
[[ -f "$SKILL_SOURCE/SKILL.md" ]] || fail "SKILL.md not found in source: $SKILL_SOURCE"
command -v zip >/dev/null 2>&1 || fail "zip is required"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

stage_dir="$tmp_dir/stage"
mkdir -p "$stage_dir/$SKILL_NAME"
cp -R "$SKILL_SOURCE"/. "$stage_dir/$SKILL_NAME"/

mkdir -p "$(dirname "$OUTPUT_ZIP")"
rm -f "$OUTPUT_ZIP"
(
  cd "$stage_dir"
  zip -r "$OUTPUT_ZIP" "$SKILL_NAME" >/dev/null
)

sha256="$(shasum -a 256 "$OUTPUT_ZIP" | awk '{print tolower($1)}')"
pass "Created skill zip: $OUTPUT_ZIP"
log "SHA256: $sha256"
