#!/usr/bin/env bash
set -euo pipefail

# Install LoomleBridge into project-local plugin path from a release manifest.
# Expected target: <ProjectRoot>/Plugins/LoomleBridge

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

MANIFEST_URL="${LOOMLE_MANIFEST_URL:-}"
VERSION="${LOOMLE_VERSION:-latest}"
PROJECT_ROOT="${LOOMLE_PROJECT_ROOT:-$PWD}"
CACHE_DIR="${LOOMLE_CACHE_DIR:-}"
DRY_RUN=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") --manifest-url <url> [--version <x.y.z|latest>] [--project-root <path>] [--cache-dir <path>] [--dry-run]

Installs LoomleBridge release artifact to project-local path.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest-url)
      [[ $# -ge 2 ]] || fail "--manifest-url requires a value"
      MANIFEST_URL="$2"
      shift 2
      ;;
    --version)
      [[ $# -ge 2 ]] || fail "--version requires a value"
      VERSION="$2"
      shift 2
      ;;
    --project-root)
      [[ $# -ge 2 ]] || fail "--project-root requires a value"
      PROJECT_ROOT="$2"
      shift 2
      ;;
    --cache-dir)
      [[ $# -ge 2 ]] || fail "--cache-dir requires a value"
      CACHE_DIR="$2"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
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

[[ -n "$MANIFEST_URL" ]] || fail "Manifest URL is required (--manifest-url or LOOMLE_MANIFEST_URL)"
command -v python3 >/dev/null 2>&1 || fail "python3 is required"
command -v curl >/dev/null 2>&1 || fail "curl is required"

HOST_UNAME="$(uname -s 2>/dev/null || echo unknown)"
HOST_LC="$(printf '%s' "$HOST_UNAME" | tr '[:upper:]' '[:lower:]')"
PLATFORM_KEY=""

case "$HOST_LC" in
  darwin*) PLATFORM_KEY="darwin" ;;
  linux*) PLATFORM_KEY="linux" ;;
  mingw*|msys*|cygwin*) PLATFORM_KEY="windows" ;;
  *) fail "Unsupported platform: $HOST_UNAME" ;;
esac

if [[ -z "$CACHE_DIR" ]]; then
  case "$PLATFORM_KEY" in
    windows)
      if [[ -n "${LOCALAPPDATA:-}" ]]; then
        CACHE_DIR="$LOCALAPPDATA/Loomle/releases"
      else
        CACHE_DIR="$HOME/AppData/Local/Loomle/releases"
      fi
      ;;
    *)
      CACHE_DIR="$HOME/.cache/loomle/releases"
      ;;
  esac
fi

PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"
PLUGIN_ROOT="$PROJECT_ROOT/Plugins"
TARGET_PLUGIN_DIR="$PLUGIN_ROOT/LoomleBridge"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$CACHE_DIR" "$PLUGIN_ROOT"
MANIFEST_PATH="$TMP_DIR/manifest.json"

log "Downloading manifest: $MANIFEST_URL"
curl -fsSL "$MANIFEST_URL" -o "$MANIFEST_PATH"

readarray -t SELECTED < <(python3 - <<'PY' "$MANIFEST_PATH" "$VERSION" "$PLATFORM_KEY"
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
requested = sys.argv[2]
platform = sys.argv[3]

manifest = json.loads(manifest_path.read_text())
versions = manifest.get("versions")
if not isinstance(versions, dict):
    raise SystemExit("manifest.versions must be an object")

version = requested
if version == "latest":
    version = manifest.get("latest")
if not isinstance(version, str) or not version:
    raise SystemExit("cannot resolve target version")

entry = versions.get(version)
if not isinstance(entry, dict):
    raise SystemExit(f"version not found: {version}")

packages = entry.get("packages")
if not isinstance(packages, dict):
    raise SystemExit(f"packages missing for version: {version}")

package = packages.get(platform)
if not isinstance(package, dict):
    raise SystemExit(f"package not found for platform: {platform}")

url = package.get("url")
sha256 = package.get("sha256")
fmt = package.get("format", "")
if not isinstance(url, str) or not url:
    raise SystemExit("package url is required")
if not isinstance(sha256, str) or not sha256:
    raise SystemExit("package sha256 is required")
if not isinstance(fmt, str):
    raise SystemExit("package format must be string")

print(version)
print(url)
print(sha256.lower())
print(fmt.lower())
PY
)

[[ "${#SELECTED[@]}" -eq 4 ]] || fail "Failed to parse manifest selection"
SEL_VERSION="${SELECTED[0]}"
PKG_URL="${SELECTED[1]}"
PKG_SHA256="${SELECTED[2]}"
PKG_FORMAT="${SELECTED[3]}"

FILE_BASENAME="$(basename "$PKG_URL")"
ARCHIVE_PATH="$CACHE_DIR/$FILE_BASENAME"
EXTRACT_DIR="$TMP_DIR/extract"
mkdir -p "$EXTRACT_DIR"

log "Downloading package for $PLATFORM_KEY version $SEL_VERSION"
curl -fsSL "$PKG_URL" -o "$ARCHIVE_PATH"

calc_sha256() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print tolower($1)}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | awk '{print tolower($1)}'
    return
  fi
  if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -Path '$f').Hash.ToLowerInvariant()" | tr -d '\r'
    return
  fi
  fail "No SHA256 tool found"
}

ACTUAL_SHA256="$(calc_sha256 "$ARCHIVE_PATH")"
[[ "$ACTUAL_SHA256" == "$PKG_SHA256" ]] || fail "SHA256 mismatch for $ARCHIVE_PATH"
pass "SHA256 verified"

if [[ -n "$PKG_FORMAT" ]]; then
  case "$PKG_FORMAT" in
    zip)
      command -v unzip >/dev/null 2>&1 || fail "unzip is required for zip packages"
      unzip -q "$ARCHIVE_PATH" -d "$EXTRACT_DIR"
      ;;
    tar.gz|tgz)
      tar -xzf "$ARCHIVE_PATH" -C "$EXTRACT_DIR"
      ;;
    *)
      fail "Unsupported package format in manifest: $PKG_FORMAT"
      ;;
  esac
else
  case "$ARCHIVE_PATH" in
    *.zip)
      command -v unzip >/dev/null 2>&1 || fail "unzip is required for zip packages"
      unzip -q "$ARCHIVE_PATH" -d "$EXTRACT_DIR"
      ;;
    *.tar.gz|*.tgz)
      tar -xzf "$ARCHIVE_PATH" -C "$EXTRACT_DIR"
      ;;
    *)
      fail "Cannot infer archive format from file name: $ARCHIVE_PATH"
      ;;
  esac
fi

SOURCE_PLUGIN_DIR=""
if [[ -d "$EXTRACT_DIR/LoomleBridge" ]]; then
  SOURCE_PLUGIN_DIR="$EXTRACT_DIR/LoomleBridge"
else
  SOURCE_PLUGIN_DIR="$(find "$EXTRACT_DIR" -maxdepth 4 -type d -name LoomleBridge | head -n 1 || true)"
fi
[[ -n "$SOURCE_PLUGIN_DIR" && -d "$SOURCE_PLUGIN_DIR" ]] || fail "LoomleBridge directory not found in package"

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "Dry run enabled; skipping install"
  printf 'version=%s\nplugin=%s\nsource=%s\n' "$SEL_VERSION" "$TARGET_PLUGIN_DIR" "$SOURCE_PLUGIN_DIR"
  exit 0
fi

rm -rf "$TARGET_PLUGIN_DIR"
mkdir -p "$TARGET_PLUGIN_DIR"
cp -R "$SOURCE_PLUGIN_DIR"/. "$TARGET_PLUGIN_DIR"/
pass "Installed LoomleBridge to $TARGET_PLUGIN_DIR"

STATE_DIR="$PROJECT_ROOT/Loomle/install"
STATE_PATH="$STATE_DIR/state.json"
mkdir -p "$STATE_DIR"
python3 - <<'PY' "$STATE_PATH" "$SEL_VERSION" "$PKG_URL" "$TARGET_PLUGIN_DIR"
import json
import pathlib
import sys
from datetime import datetime, timezone

state_path = pathlib.Path(sys.argv[1])
state = {
    "version": sys.argv[2],
    "source": sys.argv[3],
    "plugin_path": sys.argv[4],
    "installed_at_utc": datetime.now(timezone.utc).isoformat(),
}
state_path.write_text(json.dumps(state, indent=2) + "\n")
PY
pass "Wrote install state: $STATE_PATH"

log "Run post-install verify: scripts/project_verify_loomle.sh --project-root $PROJECT_ROOT --fix"
