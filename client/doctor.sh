#!/usr/bin/env bash
set -euo pipefail

fail() {
  echo "[loomle-doctor][ERROR] $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  doctor.sh [--project-root <ProjectRoot>]

Checks install completeness and expected runtime endpoint metadata for one
project-local LOOMLE install.
EOF
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
  local project_root=""
  local script_dir
  local loomle_bin
  local install_state
  local plugin_root
  local endpoint
  local install_ok="false"
  local runtime_status="endpoint_missing"

  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --project-root)
        [[ $# -ge 2 ]] || fail "missing value for --project-root"
        project_root="$2"
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

  loomle_bin="$project_root/Loomle/loomle"
  install_state="$project_root/Loomle/install/active.json"
  plugin_root="$project_root/Plugins/LoomleBridge"
  endpoint="$project_root/Intermediate/loomle.sock"

  if [[ -d "$plugin_root" && -f "$loomle_bin" && -f "$install_state" ]]; then
    install_ok="true"
  fi

  if [[ -S "$endpoint" ]]; then
    runtime_status="endpoint_present"
  fi

  python3 - "$project_root" "$plugin_root" "$loomle_bin" "$install_state" "$endpoint" "$install_ok" "$runtime_status" <<'PY'
import json, sys
print(json.dumps({
    "projectRoot": sys.argv[1],
    "pluginRoot": sys.argv[2],
    "clientPath": sys.argv[3],
    "installState": sys.argv[4],
    "runtimeEndpoint": sys.argv[5],
    "installOk": sys.argv[6] == "true",
    "runtimeStatus": sys.argv[7],
    "runtimeProbePerformed": False,
}, indent=2))
PY
}

main "$@"
