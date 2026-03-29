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

Checks install completeness and runtime endpoint readiness for one project-local
LOOMLE install.
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
  local runtime_ready="false"

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
    if python3 - "$endpoint" <<'PY'
import socket, sys
sock_path = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(0.5)
try:
    s.connect(sock_path)
finally:
    s.close()
PY
    then
      runtime_ready="true"
    fi
  fi

  python3 - "$project_root" "$plugin_root" "$loomle_bin" "$install_state" "$endpoint" "$install_ok" "$runtime_ready" <<'PY'
import json, sys
print(json.dumps({
    "projectRoot": sys.argv[1],
    "pluginRoot": sys.argv[2],
    "clientPath": sys.argv[3],
    "installState": sys.argv[4],
    "runtimeEndpoint": sys.argv[5],
    "installOk": sys.argv[6] == "true",
    "runtimeReady": sys.argv[7] == "true",
}, indent=2))
PY
}

main "$@"
