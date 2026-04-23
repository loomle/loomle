#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
REQUESTED_VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"

fail() {
  echo "[loomle-install][ERROR] $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  install.sh [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>] [--install-root <Path>]

Installs LOOMLE globally for the current user. Unreal projects are prepared
later through the MCP tool project.install.
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "$1 is required"
}

download_file() {
  local url="$1"
  local output="$2"
  curl -fsSL \
    --retry 5 \
    --retry-delay 2 \
    --retry-all-errors \
    --connect-timeout 15 \
    --max-time 180 \
    "$url" \
    -o "$output"
}

detect_platform() {
  case "$(uname -s)" in
    Darwin) echo "darwin" ;;
    Linux) echo "linux" ;;
    *) fail "unsupported platform: $(uname -s)" ;;
  esac
}

resolve_release_tag() {
  if [[ "$REQUESTED_VERSION" == v* ]]; then
    echo "$REQUESTED_VERSION"
  else
    echo "v$REQUESTED_VERSION"
  fi
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

resolve_effective_version() {
  local manifest_path="$1"
  if [[ "$REQUESTED_VERSION" == "latest" ]]; then
    local latest
    latest="$(tr -d '\r\n' < "$manifest_path" | sed -n 's/.*"latest"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
    [[ -n "$latest" ]] || fail "failed to resolve latest version from manifest"
    printf '%s' "$latest"
  else
    printf '%s' "${REQUESTED_VERSION#v}"
  fi
}

client_binary_name() {
  if [[ "$(detect_platform)" == "windows" ]]; then
    printf 'loomle.exe'
  else
    printf 'loomle'
  fi
}

remove_path_if_exists() {
  local path="$1"
  [[ -e "$path" || -L "$path" ]] || return 0
  rm -rf "$path"
}

copy_tree_replace() {
  local source="$1"
  local destination="$2"
  [[ -d "$source" ]] || fail "install source not found: $source"
  remove_path_if_exists "$destination"
  mkdir -p "$(dirname "$destination")"
  cp -R "$source" "$destination"
}

copy_file_replace() {
  local source="$1"
  local destination="$2"
  [[ -f "$source" ]] || fail "install file not found: $source"
  remove_path_if_exists "$destination"
  mkdir -p "$(dirname "$destination")"
  cp "$source" "$destination"
}

ensure_executable_if_present() {
  local path="$1"
  if [[ -f "$path" ]]; then
    chmod +x "$path"
  fi
}

write_active_state() {
  local active_state_path="$1"
  local version="$2"
  local platform="$3"
  local install_root="$4"
  local launcher_path="$5"
  local active_client_path="$6"
  mkdir -p "$(dirname "$active_state_path")"
  cat > "$active_state_path" <<EOF
{
  "schemaVersion": 2,
  "installedVersion": "$(json_escape "$version")",
  "activeVersion": "$(json_escape "$version")",
  "platform": "$(json_escape "$platform")",
  "installRoot": "$(json_escape "$install_root")",
  "launcherPath": "$(json_escape "$launcher_path")",
  "activeClientPath": "$(json_escape "$active_client_path")",
  "versionsRoot": "$(json_escape "$install_root/versions")",
  "pluginCacheRoot": "$(json_escape "$install_root/versions/$version/plugin-cache")"
}
EOF
}

main() {
  require_command curl
  require_command unzip

  local manifest_url=""
  local asset_url=""
  local install_root="${LOOMLE_INSTALL_ROOT:-$HOME/.loomle}"
  local platform
  local release_tag
  local effective_version
  local tmp_dir
  local manifest_path
  local archive_path
  local bundle_dir
  local client_name
  local version_root
  local launcher_path
  local active_client_path
  local active_state_path

  while [[ $# -gt 0 ]]; do
    case "$1" in
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
      --install-root)
        [[ $# -ge 2 ]] || fail "missing value for --install-root"
        install_root="$2"
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

  install_root="$(mkdir -p "$install_root" && cd "$install_root" && pwd)"
  platform="$(detect_platform)"
  if [[ -z "$manifest_url" ]]; then
    if [[ "$REQUESTED_VERSION" == "latest" ]]; then
      manifest_url="https://github.com/${RELEASE_REPO}/releases/latest/download/loomle-manifest-${platform}.json"
    else
      release_tag="$(resolve_release_tag)"
      manifest_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/loomle-manifest-${platform}.json"
    fi
  fi
  if [[ -z "$asset_url" ]]; then
    if [[ "$REQUESTED_VERSION" == "latest" ]]; then
      asset_url="https://github.com/${RELEASE_REPO}/releases/latest/download/loomle-${platform}.zip"
    else
      release_tag="${release_tag:-$(resolve_release_tag)}"
      asset_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/loomle-${platform}.zip"
    fi
  fi

  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "${tmp_dir:-}"' EXIT
  manifest_path="$tmp_dir/manifest.json"
  archive_path="$tmp_dir/loomle-${platform}.zip"
  bundle_dir="$tmp_dir/bundle"

  echo "[loomle-install] downloading manifest $manifest_url"
  download_file "$manifest_url" "$manifest_path"
  effective_version="$(resolve_effective_version "$manifest_path")"
  client_name="$(client_binary_name)"

  echo "[loomle-install] downloading bundle $asset_url"
  download_file "$asset_url" "$archive_path"

  mkdir -p "$bundle_dir"
  unzip -q "$archive_path" -d "$bundle_dir"

  version_root="$install_root/versions/$effective_version"
  launcher_path="$install_root/bin/$client_name"
  active_client_path="$version_root/$client_name"
  active_state_path="$install_root/install/active.json"

  [[ -f "$bundle_dir/$client_name" ]] || fail "bundle missing $client_name"
  [[ -d "$bundle_dir/plugin-cache/LoomleBridge" ]] || fail "bundle missing plugin-cache/LoomleBridge"

  mkdir -p "$install_root/bin" "$install_root/install" "$install_root/state/runtimes" "$install_root/locks" "$install_root/logs"
  copy_file_replace "$bundle_dir/$client_name" "$active_client_path"
  copy_file_replace "$bundle_dir/$client_name" "$launcher_path"
  copy_tree_replace "$bundle_dir/plugin-cache/LoomleBridge" "$version_root/plugin-cache/LoomleBridge"
  cp "$manifest_path" "$version_root/manifest.json"
  ensure_executable_if_present "$active_client_path"
  ensure_executable_if_present "$launcher_path"
  write_active_state "$active_state_path" "$effective_version" "$platform" "$install_root" "$launcher_path" "$active_client_path"

  cat <<EOF
{
  "installedVersion": "$(json_escape "$effective_version")",
  "activeVersion": "$(json_escape "$effective_version")",
  "platform": "$(json_escape "$platform")",
  "installRoot": "$(json_escape "$install_root")",
  "launcherPath": "$(json_escape "$launcher_path")",
  "activeClientPath": "$(json_escape "$active_client_path")",
  "pluginCache": "$(json_escape "$version_root/plugin-cache/LoomleBridge")"
}
EOF

  cat <<EOF

Configure MCP hosts:
  Codex:  codex mcp add loomle -- $launcher_path mcp
  Claude: claude mcp add loomle --scope user $launcher_path mcp
EOF
}

main "$@"
