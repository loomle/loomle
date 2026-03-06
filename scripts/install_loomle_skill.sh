#!/usr/bin/env bash
set -euo pipefail

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOMLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SKILL_NAME="loomle-skill"
SKILL_SOURCE="$LOOMLE_DIR/skills/$SKILL_NAME"
SKILL_ZIP_URL=""
SKILL_SHA256=""
SKILLS_HOME="${SKILLS_HOME:-}"
FORCE=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--name <skill-name>] [--source <skill-path>] [--source-url <zip-url>] [--sha256 <hex>] [--skills-home <skills-root>] [--force]

Install a skill into the current agent client's skills directory.
Use --source for a local folder. Use --source-url for a zip release asset.
Use --sha256 with --source-url to verify downloaded archive integrity.

Resolution order for skills root:
1) --skills-home
2) SKILLS_HOME
3) AGENT_SKILLS_HOME
4) CODEX_HOME/skills
5) ~/.codex/skills (default fallback)
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
    --source-url)
      [[ $# -ge 2 ]] || fail "--source-url requires a value"
      SKILL_ZIP_URL="$2"
      shift 2
      ;;
    --sha256)
      [[ $# -ge 2 ]] || fail "--sha256 requires a value"
      SKILL_SHA256="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"
      shift 2
      ;;
    --skills-home)
      [[ $# -ge 2 ]] || fail "--skills-home requires a value"
      SKILLS_HOME="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
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

if [[ -z "$SKILLS_HOME" && -n "${AGENT_SKILLS_HOME:-}" ]]; then
  SKILLS_HOME="$AGENT_SKILLS_HOME"
fi
if [[ -z "$SKILLS_HOME" && -n "${CODEX_HOME:-}" ]]; then
  SKILLS_HOME="$CODEX_HOME/skills"
fi
if [[ -z "$SKILLS_HOME" ]]; then
  SKILLS_HOME="$HOME/.codex/skills"
fi

TARGET_DIR="$SKILLS_HOME/$SKILL_NAME"

resolve_source_dir() {
  local source_dir="$1"
  if [[ -d "$source_dir/$SKILL_NAME" && -f "$source_dir/$SKILL_NAME/SKILL.md" ]]; then
    printf '%s\n' "$source_dir/$SKILL_NAME"
    return
  fi
  if [[ -f "$source_dir/SKILL.md" ]]; then
    printf '%s\n' "$source_dir"
    return
  fi
  fail "Cannot find $SKILL_NAME/SKILL.md in source: $source_dir"
}

if [[ -n "$SKILL_ZIP_URL" ]]; then
  command -v python3 >/dev/null 2>&1 || fail "python3 is required for --source-url"
  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "$tmp_dir"' EXIT
  archive_path="$tmp_dir/${SKILL_NAME}.zip"
  extract_dir="$tmp_dir/extract"
  mkdir -p "$extract_dir"

  log "Downloading skill zip: $SKILL_ZIP_URL"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$SKILL_ZIP_URL" -o "$archive_path"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$archive_path" "$SKILL_ZIP_URL"
  else
    python3 - <<'PY' "$SKILL_ZIP_URL" "$archive_path"
import pathlib
import sys
import urllib.request

url = sys.argv[1]
dst = pathlib.Path(sys.argv[2])
with urllib.request.urlopen(url) as resp:
    dst.write_bytes(resp.read())
PY
  fi

  if [[ -n "$SKILL_SHA256" ]]; then
    calc_sha256() {
      local file_path="$1"
      if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file_path" | awk '{print tolower($1)}'
        return
      fi
      if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file_path" | awk '{print tolower($1)}'
        return
      fi
      python3 - <<'PY' "$file_path"
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
h = hashlib.sha256()
with path.open("rb") as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b""):
        h.update(chunk)
print(h.hexdigest())
PY
    }
    actual_sha256="$(calc_sha256 "$archive_path")"
    [[ "$actual_sha256" == "$SKILL_SHA256" ]] || fail "SHA256 mismatch for downloaded skill zip"
    pass "Downloaded skill zip SHA256 verified"
  fi

  python3 - <<'PY' "$archive_path" "$extract_dir"
import pathlib
import sys
import zipfile

archive = pathlib.Path(sys.argv[1])
extract_dir = pathlib.Path(sys.argv[2])
with zipfile.ZipFile(archive) as zf:
    zf.extractall(extract_dir)
PY

  SKILL_SOURCE="$(resolve_source_dir "$extract_dir")"
else
  SKILL_SOURCE="$(cd "$SKILL_SOURCE" && pwd)"
  [[ -d "$SKILL_SOURCE" ]] || fail "Skill source does not exist: $SKILL_SOURCE"
  SKILL_SOURCE="$(resolve_source_dir "$SKILL_SOURCE")"
fi

[[ -f "$SKILL_SOURCE/SKILL.md" ]] || fail "SKILL.md not found in source: $SKILL_SOURCE"

mkdir -p "$SKILLS_HOME"

if [[ -e "$TARGET_DIR" ]]; then
  if [[ "$FORCE" -eq 1 ]]; then
    log "Replacing existing skill: $TARGET_DIR"
    rm -rf "$TARGET_DIR"
  else
    fail "Target already exists: $TARGET_DIR (use --force to replace)"
  fi
fi

cp -R "$SKILL_SOURCE" "$TARGET_DIR"
pass "Installed $SKILL_NAME to $TARGET_DIR"
