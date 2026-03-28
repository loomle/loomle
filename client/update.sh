#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
REQUESTED_VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"

fail() {
  echo "[loomle-update][ERROR] $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  update.sh [--project-root <ProjectRoot>] [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>]

Updates the project-local LOOMLE install from the release manifest and bundle.
EOF
}

require_python() {
  command -v python3 >/dev/null 2>&1 || fail "python3 is required"
}

detect_platform() {
  case "$(uname -s)" in
    Darwin) echo "darwin" ;;
    Linux) echo "linux" ;;
    *) fail "unsupported platform: $(uname -s)" ;;
  esac
}

resolve_release_tag() {
  if [[ "$REQUESTED_VERSION" == "latest" ]]; then
    echo "loomle-latest"
  elif [[ "$REQUESTED_VERSION" == v* ]]; then
    echo "$REQUESTED_VERSION"
  else
    echo "v$REQUESTED_VERSION"
  fi
}

find_project_root() {
  local start="$1"
  local dir
  dir="$(cd "$start" && pwd)"
  while [[ "$dir" != "/" ]]; do
    if find "$dir" -maxdepth 1 -type f -name '*.uproject' | grep -q .; then
      echo "$dir"
      return 0
    fi
    dir="$(dirname "$dir")"
  done
  return 1
}

main() {
  require_python

  local project_root=""
  local manifest_url=""
  local asset_url=""
  local platform
  local release_tag
  local tmp_dir
  local manifest_path
  local archive_path
  local bundle_dir
  local helper_path
  local script_dir

  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --project-root)
        [[ $# -ge 2 ]] || fail "missing value for --project-root"
        project_root="$2"
        shift 2
        ;;
      --version)
        [[ $# -ge 2 ]] || fail "missing value for --version"
        REQUESTED_VERSION="$2"
        shift 2
        ;;
      --manifest-url)
        [[ $# -ge 2 ]] || fail "missing value for --manifest-url"
        manifest_url="$2"
        shift 2
        ;;
      --asset-url)
        [[ $# -ge 2 ]] || fail "missing value for --asset-url"
        asset_url="$2"
        shift 2
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        fail "unknown argument: $1"
        ;;
    esac
  done

  if [[ -z "$project_root" && -f "$script_dir/../"*.uproject ]]; then
    project_root="$(cd "$script_dir/.." && pwd)"
  fi
  if [[ -z "$project_root" ]]; then
    project_root="$(find_project_root "$PWD" || true)"
  fi
  [[ -n "$project_root" ]] || fail "could not resolve Unreal project root; pass --project-root"
  project_root="$(cd "$project_root" && pwd)"

  platform="$(detect_platform)"
  release_tag="$(resolve_release_tag)"
  if [[ -z "$manifest_url" ]]; then
    manifest_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/loomle-manifest-${platform}.json"
  fi

  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "${tmp_dir:-}"' EXIT
  manifest_path="$tmp_dir/manifest.json"
  archive_path="$tmp_dir/loomle-${platform}.zip"
  bundle_dir="$tmp_dir/bundle"

  echo "[loomle-update] downloading manifest $manifest_url"
  curl -fsSL "$manifest_url" -o "$manifest_path"

  if [[ -z "$asset_url" ]]; then
    asset_url="$(python3 - "$manifest_path" "$REQUESTED_VERSION" "$platform" <<'PY'
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
requested = sys.argv[2]
platform = sys.argv[3]
version = manifest.get("latest") if requested == "latest" else requested.removeprefix("v")
packages = manifest.get("versions", {}).get(version, {}).get("packages", {})
package = packages.get(platform)
if not isinstance(package, dict):
    raise SystemExit(1)
print(package["url"])
PY
)" || fail "failed to resolve asset URL from manifest"
  fi

  echo "[loomle-update] downloading bundle $asset_url"
  curl -fsSL "$asset_url" -o "$archive_path"

  mkdir -p "$bundle_dir"
  python3 - "$archive_path" "$bundle_dir" <<'PY'
import sys, zipfile
archive, target = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive) as zf:
    zf.extractall(target)
PY

  helper_path="$bundle_dir/Loomle/runtime/install_release.py"
  [[ -f "$helper_path" ]] || fail "bundle missing install helper: $helper_path"

  python3 "$helper_path" \
    --bundle-root "$bundle_dir" \
    --project-root "$project_root" \
    --manifest-path "$manifest_path" \
    --platform "$platform" \
    --version "${REQUESTED_VERSION#v}"
}

main "$@"
