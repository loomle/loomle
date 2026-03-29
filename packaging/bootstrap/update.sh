#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
REQUESTED_VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"
EDITOR_PERF_SECTION="[/Script/UnrealEd.EditorPerformanceSettings]"
EDITOR_THROTTLE_SETTING="bThrottleCPUWhenNotForeground=False"

fail() {
  echo "[loomle-update][ERROR] $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  update.sh [--project-root <ProjectRoot>] [--version <Version>] [--manifest-url <URL>] [--asset-url <URL>]

Updates the project-local LOOMLE install from the release manifest and platform
bundle.
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

versioned_client_name() {
  local version="$1"
  if [[ "$(detect_platform)" == "windows" ]]; then
    printf 'loomle-%s.exe' "$version"
  else
    printf 'loomle-%s' "$version"
  fi
}

copy_tree_replace() {
  local source="$1"
  local destination="$2"
  [[ -d "$source" ]] || fail "install source not found: $source"
  rm -rf "$destination"
  mkdir -p "$(dirname "$destination")"
  cp -R "$source" "$destination"
}

remove_path_if_exists() {
  local path="$1"
  [[ -e "$path" || -L "$path" ]] || return 0
  rm -rf "$path"
}

copy_entry() {
  local source="$1"
  local destination="$2"
  if [[ -d "$source" ]]; then
    cp -R "$source" "$destination"
  else
    cp "$source" "$destination"
  fi
}

should_skip_name() {
  local name="$1"
  shift
  local skip
  for skip in "$@"; do
    [[ "$name" == "$skip" ]] && return 0
  done
  return 1
}

sync_workspace_entries() {
  local source_root="$1"
  local destination_root="$2"
  shift 2
  local skip_names=("$@")
  mkdir -p "$destination_root"
  while IFS= read -r -d '' source_entry; do
    local name
    name="$(basename "$source_entry")"
    if should_skip_name "$name" "${skip_names[@]}"; then
      continue
    fi
    remove_path_if_exists "$destination_root/$name"
    copy_entry "$source_entry" "$destination_root/$name"
  done < <(find "$source_root" -mindepth 1 -maxdepth 1 -print0 | sort -z)
}

ensure_executable_if_present() {
  local path="$1"
  if [[ -f "$path" ]]; then
    chmod +x "$path"
  fi
}

ensure_ini_setting() {
  local ini_path="$1"
  mkdir -p "$(dirname "$ini_path")"
  if [[ -f "$ini_path" ]] && grep -Fqi "$EDITOR_THROTTLE_SETTING" "$ini_path"; then
    return 0
  fi
  {
    [[ -f "$ini_path" ]] && printf '\n'
    printf '%s\n%s\n' "$EDITOR_PERF_SECTION" "$EDITOR_THROTTLE_SETTING"
  } >> "$ini_path"
}

ensure_workspace_layout() {
  local workspace_root="$1"
  mkdir -p \
    "$workspace_root/install/versions" \
    "$workspace_root/install/manifests" \
    "$workspace_root/install/pending" \
    "$workspace_root/state/diag" \
    "$workspace_root/state/captures"
}

copy_versioned_payload() {
  local workspace_source="$1"
  local workspace_destination="$2"
  local version="$3"
  local source_client_name="$4"
  local target_client_name="$5"
  local version_root="$workspace_destination/install/versions/$version"
  local kit_root="$version_root/kit"

  mkdir -p "$version_root"
  cp "$workspace_source/$source_client_name" "$version_root/$target_client_name"
  chmod +x "$version_root/$target_client_name"

  rm -rf "$kit_root"
  mkdir -p "$kit_root"
  local entry
  for entry in README.md blueprint material pcg workflows examples; do
    if [[ -e "$workspace_source/$entry" ]]; then
      copy_entry "$workspace_source/$entry" "$kit_root/$entry"
    fi
  done
}

copy_manifest_record() {
  local manifest_path="$1"
  local workspace_destination="$2"
  local version="$3"
  cp "$manifest_path" "$workspace_destination/install/manifests/$version.json"
}

write_active_state() {
  local active_state_path="$1"
  local version="$2"
  local platform="$3"
  local project_root="$4"
  local plugin_root="$5"
  local workspace_root="$6"
  local launcher_path="$7"
  local active_client_path="$8"
  local settings_path="$9"
  mkdir -p "$(dirname "$active_state_path")"
  cat > "$active_state_path" <<EOF
{
  "schemaVersion": 1,
  "installedVersion": "$(json_escape "$version")",
  "activeVersion": "$(json_escape "$version")",
  "platform": "$(json_escape "$platform")",
  "projectRoot": "$(json_escape "$project_root")",
  "loomleRoot": "$(json_escape "$workspace_root")",
  "pluginRoot": "$(json_escape "$plugin_root")",
  "launcherPath": "$(json_escape "$launcher_path")",
  "activeClientPath": "$(json_escape "$active_client_path")",
  "manifestsRoot": "$(json_escape "$workspace_root/install/manifests")",
  "versionsRoot": "$(json_escape "$workspace_root/install/versions")",
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
  local launcher_name="loomle"
  local active_client_name
  local launcher_path
  local active_client_path
  local active_state_path
  local settings_path
  local script_dir
  local self_script_name

  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  self_script_name="$(basename "${BASH_SOURCE[0]}")"

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
  if [[ -z "$asset_url" ]]; then
    asset_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/loomle-${platform}.zip"
  fi

  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "${tmp_dir:-}"' EXIT
  manifest_path="$tmp_dir/manifest.json"
  archive_path="$tmp_dir/loomle-${platform}.zip"
  bundle_dir="$tmp_dir/bundle"

  echo "[loomle-update] downloading manifest $manifest_url"
  curl -fsSL "$manifest_url" -o "$manifest_path"
  effective_version="$(resolve_effective_version "$manifest_path")"
  active_client_name="$(versioned_client_name "$effective_version")"

  echo "[loomle-update] downloading bundle $asset_url"
  curl -fsSL "$asset_url" -o "$archive_path"

  mkdir -p "$bundle_dir"
  unzip -q "$archive_path" -d "$bundle_dir"

  plugin_source="$bundle_dir/plugin/LoomleBridge"
  workspace_source="$bundle_dir/Loomle"
  plugin_destination="$project_root/Plugins/LoomleBridge"
  workspace_destination="$project_root/Loomle"
  launcher_path="$workspace_destination/$launcher_name"
  active_client_path="$workspace_destination/install/versions/$effective_version/$active_client_name"
  active_state_path="$workspace_destination/install/active.json"
  settings_path="$project_root/Config/DefaultEditorSettings.ini"

  [[ -d "$plugin_source" ]] || fail "bundle missing plugin/LoomleBridge"
  [[ -d "$workspace_source" ]] || fail "bundle missing Loomle/"

  copy_tree_replace "$plugin_source" "$plugin_destination"
  sync_workspace_entries "$workspace_source" "$workspace_destination" "$self_script_name"
  ensure_workspace_layout "$workspace_destination"
  copy_versioned_payload "$workspace_source" "$workspace_destination" "$effective_version" "$launcher_name" "$active_client_name"
  copy_manifest_record "$manifest_path" "$workspace_destination" "$effective_version"

  [[ -f "$launcher_path" ]] || fail "installed client missing: $launcher_path"
  ensure_executable_if_present "$launcher_path"
  ensure_executable_if_present "$workspace_destination/doctor.sh"

  ensure_ini_setting "$settings_path"
  write_active_state \
    "$active_state_path" \
    "$effective_version" \
    "$platform" \
    "$project_root" \
    "$plugin_destination" \
    "$workspace_destination" \
    "$launcher_path" \
    "$active_client_path" \
    "$settings_path"

  cat <<EOF
{
  "installedVersion": "$(json_escape "$effective_version")",
  "activeVersion": "$(json_escape "$effective_version")",
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
  "install": {
    "activeState": "$(json_escape "$active_state_path")",
    "activeClientPath": "$(json_escape "$active_client_path")"
  }
}
EOF
}

main "$@"
