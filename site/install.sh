#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
REQUESTED_VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"
EDITOR_PERF_SECTION="[/Script/UnrealEd.EditorPerformanceSettings]"
EDITOR_THROTTLE_SETTING="bThrottleCPUWhenNotForeground=False"

fail() {
  echo "[loomle-install][ERROR] $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  install.sh [--project-root <ProjectRoot>] [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>]

Installs LOOMLE into one Unreal project by downloading the release manifest and
platform bundle, extracting it, and materializing Plugins/LoomleBridge and
Loomle/.
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "$1 is required"
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

copy_tree() {
  local source="$1"
  local destination="$2"
  [[ -d "$source" ]] || fail "install source not found: $source"
  rm -rf "$destination"
  mkdir -p "$destination"
  cp -R "$source"/. "$destination"/
}

ensure_ini_setting() {
  local ini_path="$1"
  mkdir -p "$(dirname "$ini_path")"
  if [[ -f "$ini_path" ]]; then
    if grep -Fqi "$EDITOR_THROTTLE_SETTING" "$ini_path"; then
      return 0
    fi
  fi
  {
    [[ -f "$ini_path" ]] && printf '\n'
    printf '%s\n%s\n' "$EDITOR_PERF_SECTION" "$EDITOR_THROTTLE_SETTING"
  } >> "$ini_path"
}

write_install_state() {
  local install_state_path="$1"
  local version="$2"
  local platform="$3"
  local project_root="$4"
  local plugin_root="$5"
  local workspace_root="$6"
  local client_path="$7"
  local settings_path="$8"
  mkdir -p "$(dirname "$install_state_path")"
  cat > "$install_state_path" <<EOF
{
  "schemaVersion": 1,
  "installedVersion": "$(json_escape "$version")",
  "platform": "$(json_escape "$platform")",
  "projectRoot": "$(json_escape "$project_root")",
  "workspaceRoot": "$(json_escape "$workspace_root")",
  "pluginRoot": "$(json_escape "$plugin_root")",
  "clientPath": "$(json_escape "$client_path")",
  "editorPerformance": {
    "settingsFile": "$(json_escape "$settings_path")",
    "throttleWhenNotForeground": false
  }
}
EOF
}

main() {
  require_command curl
  require_command unzip

  local project_root=""
  local manifest_url=""
  local asset_url=""
  local platform
  local release_tag
  local effective_version
  local tmp_dir
  local manifest_path
  local archive_path
  local bundle_dir
  local plugin_source
  local workspace_source
  local plugin_destination
  local workspace_destination
  local client_name="loomle"
  local client_path
  local install_state_path
  local settings_path

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
  if [[ -z "$asset_url" ]]; then
    asset_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/loomle-${platform}.zip"
  fi

  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "${tmp_dir:-}"' EXIT
  manifest_path="$tmp_dir/manifest.json"
  archive_path="$tmp_dir/loomle-${platform}.zip"
  bundle_dir="$tmp_dir/bundle"

  echo "[loomle-install] downloading manifest $manifest_url"
  curl -fsSL "$manifest_url" -o "$manifest_path"
  effective_version="$(resolve_effective_version "$manifest_path")"

  echo "[loomle-install] downloading bundle $asset_url"
  curl -fsSL "$asset_url" -o "$archive_path"

  mkdir -p "$bundle_dir"
  unzip -q "$archive_path" -d "$bundle_dir"

  plugin_source="$bundle_dir/plugin/LoomleBridge"
  workspace_source="$bundle_dir/Loomle"
  plugin_destination="$project_root/Plugins/LoomleBridge"
  workspace_destination="$project_root/Loomle"
  client_path="$workspace_destination/$client_name"
  install_state_path="$workspace_destination/runtime/install.json"
  settings_path="$project_root/Config/DefaultEditorSettings.ini"

  [[ -d "$plugin_source" ]] || fail "bundle missing plugin/LoomleBridge"
  [[ -d "$workspace_source" ]] || fail "bundle missing Loomle/"

  copy_tree "$plugin_source" "$plugin_destination"
  copy_tree "$workspace_source" "$workspace_destination"

  [[ -f "$client_path" ]] || fail "installed client missing: $client_path"
  chmod +x "$client_path"
  [[ -f "$workspace_destination/update.sh" ]] && chmod +x "$workspace_destination/update.sh"
  [[ -f "$workspace_destination/doctor.sh" ]] && chmod +x "$workspace_destination/doctor.sh"

  ensure_ini_setting "$settings_path"
  write_install_state \
    "$install_state_path" \
    "$effective_version" \
    "$platform" \
    "$project_root" \
    "$plugin_destination" \
    "$workspace_destination" \
    "$client_path" \
    "$settings_path"

  cat <<EOF
{
  "installedVersion": "$(json_escape "$effective_version")",
  "platform": "$(json_escape "$platform")",
  "bundleRoot": "$(json_escape "$bundle_dir")",
  "projectRoot": "$(json_escape "$project_root")",
  "plugin": {
    "source": "$(json_escape "$plugin_source")",
    "destination": "$(json_escape "$plugin_destination")"
  },
  "workspace": {
    "source": "$(json_escape "$workspace_source")",
    "destination": "$(json_escape "$workspace_destination")"
  },
  "runtime": {
    "installState": "$(json_escape "$install_state_path")"
  }
}
EOF
}

main "$@"
