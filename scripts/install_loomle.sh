#!/usr/bin/env bash
set -euo pipefail

# Install contract (single entrypoint, idempotent):
# 1) Ensure .uproject wiring (AdditionalPluginDirectories + LoomleBridge Editor enablement)
# 2) Resolve plugin binary (compatible local prebuilt first, local build fallback)
# 3) Launch Unreal Editor
# 4) Verify bridge transport endpoint (socket / named pipe equivalent)
# 5) Verify bridge baseline tools (loomle/graph/graph.query/graph.mutate/context/execute) and unreal.LoomleBlueprintAdapter

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOMLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$LOOMLE_DIR/.." && pwd)"

run_windows_installer_if_needed() {
  local uname_s uname_lc win_installer win_installer_path
  uname_s="$(uname -s 2>/dev/null || echo unknown)"
  uname_lc="$(printf '%s' "$uname_s" | tr '[:upper:]' '[:lower:]')"

  case "$uname_lc" in
    mingw*|msys*|cygwin*)
      win_installer="$SCRIPT_DIR/install_loomle_windows.ps1"
      [[ -f "$win_installer" ]] || fail "Windows installer not found: $win_installer"
      command -v powershell.exe >/dev/null 2>&1 || fail "powershell.exe is required on Windows shells"

      win_installer_path="$win_installer"
      if command -v cygpath >/dev/null 2>&1; then
        win_installer_path="$(cygpath -w "$win_installer")"
      fi

      log "Detected Windows shell ($uname_s); forwarding to install_loomle_windows.ps1"
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$win_installer_path" "$@"
      exit $?
      ;;
  esac
}

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.7}"
HOST_OS="$(uname -s 2>/dev/null || echo unknown)"

UE_APP=""
UE_BIN=""
UE_VERSION_FILE=""
GEN_SCRIPT=""
BUILD_SCRIPT=""
BUILD_PLATFORM=""
PLATFORM_BIN_DIR=""
MODULE_BINARY_NAME=""

SKIP_BUILD=0
SKIP_LAUNCH=0
SKIP_VERIFY=0
FORCE_BUILD=0

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

run_windows_installer_if_needed "$@"

for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    --skip-launch) SKIP_LAUNCH=1 ;;
    --skip-verify) SKIP_VERIFY=1 ;;
    --force-build) FORCE_BUILD=1 ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [--skip-build] [--skip-launch] [--skip-verify] [--force-build]

Installs and verifies Loomle for the current UE project.
EOF
      exit 0
      ;;
    *)
      echo "[FAIL] Unknown argument: $arg" >&2
      exit 1
      ;;
  esac
done

command -v python3 >/dev/null 2>&1 || fail "python3 is required"

UPROJECT_PATH="$(find "$PROJECT_ROOT" -maxdepth 1 -type f -name '*.uproject' | head -n 1)"
[[ -n "$UPROJECT_PATH" ]] || fail "No .uproject found in $PROJECT_ROOT"
UPROJECT_NAME="$(basename "$UPROJECT_PATH")"
PROJECT_NAME="${UPROJECT_NAME%.uproject}"
TARGET_NAME="${PROJECT_NAME}Editor"
SOCKET_PATH="$PROJECT_ROOT/Intermediate/loomle.sock"
ROOT_AGENTS_PATH="$PROJECT_ROOT/AGENTS.md"
ROOT_AGENTS_HINT="- Always read ./Loomle/AGENTS.md before starting work in this project."

case "$HOST_OS" in
  Darwin)
    UE_APP="${UE_APP:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app}"
    UE_VERSION_FILE="${UE_VERSION_FILE:-$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.version}"
    GEN_SCRIPT="${GEN_SCRIPT:-$UE_ROOT/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh}"
    BUILD_SCRIPT="${BUILD_SCRIPT:-$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh}"
    BUILD_PLATFORM="Mac"
    PLATFORM_BIN_DIR="Mac"
    MODULE_BINARY_NAME="UnrealEditor-LoomleBridge.dylib"
    ;;
  Linux)
    UE_BIN="${UE_BIN:-$UE_ROOT/Engine/Binaries/Linux/UnrealEditor}"
    UE_VERSION_FILE="${UE_VERSION_FILE:-$UE_ROOT/Engine/Binaries/Linux/UnrealEditor.version}"
    GEN_SCRIPT="${GEN_SCRIPT:-$UE_ROOT/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh}"
    BUILD_SCRIPT="${BUILD_SCRIPT:-$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh}"
    BUILD_PLATFORM="Linux"
    PLATFORM_BIN_DIR="Linux"
    MODULE_BINARY_NAME="UnrealEditor-LoomleBridge.so"
    ;;
  *)
    fail "Unsupported non-Windows host OS: $HOST_OS"
    ;;
esac

ROOT_BIN="$PROJECT_ROOT/Binaries/$PLATFORM_BIN_DIR/$MODULE_BINARY_NAME"
ROOT_MODULES="$PROJECT_ROOT/Binaries/$PLATFORM_BIN_DIR/UnrealEditor.modules"

PLUGIN_DIR="$(
python3 - <<'PY' "$UPROJECT_PATH" "$PROJECT_ROOT"
import json
import pathlib
import sys

uproject_path = pathlib.Path(sys.argv[1])
project_root = pathlib.Path(sys.argv[2]).resolve()

data = json.loads(uproject_path.read_text())

candidates = []
addl = data.get("AdditionalPluginDirectories")
if isinstance(addl, list):
    candidates.extend(addl)

# Keep project-local plugins as a fallback lookup root.
candidates.append("./Plugins")
candidates.append("./Loomle/Plugins")

seen = set()
for entry in candidates:
    if not isinstance(entry, str):
        continue
    entry = entry.strip()
    if not entry:
        continue
    if entry in seen:
        continue
    seen.add(entry)

    base = pathlib.Path(entry)
    if not base.is_absolute():
        base = (project_root / base).resolve()
    plugin_dir = base / "LoomleBridge"
    if (plugin_dir / "LoomleBridge.uplugin").exists():
        print(str(plugin_dir))
        sys.exit(0)

fallback = (project_root / "Loomle" / "Plugins" / "LoomleBridge").resolve()
print(str(fallback))
PY
)"

PLUGIN_BIN="$PLUGIN_DIR/Binaries/$PLATFORM_BIN_DIR/$MODULE_BINARY_NAME"
PLUGIN_MODULES="$PLUGIN_DIR/Binaries/$PLATFORM_BIN_DIR/UnrealEditor.modules"

[[ -d "$PLUGIN_DIR" ]] || fail "Plugin not found from project config/fallback: $PLUGIN_DIR"
pass "Plugin directory resolved: $PLUGIN_DIR"

log "Ensuring .uproject wiring for AdditionalPluginDirectories + LoomleBridge"
python3 - <<'PY' "$UPROJECT_PATH"
import json
import pathlib
import sys

uproject_path = pathlib.Path(sys.argv[1])
data = json.loads(uproject_path.read_text())
changed = False

addl = data.get("AdditionalPluginDirectories")
if not isinstance(addl, list):
    addl = []
    data["AdditionalPluginDirectories"] = addl
if "./Loomle/Plugins" not in addl:
    addl.append("./Loomle/Plugins")
    changed = True

plugins = data.get("Plugins")
if not isinstance(plugins, list):
    plugins = []
    data["Plugins"] = plugins

bridge = None
for p in plugins:
    if isinstance(p, dict) and p.get("Name") == "LoomleBridge":
        bridge = p
        break
if bridge is None:
    bridge = {"Name": "LoomleBridge"}
    plugins.append(bridge)
    changed = True
if bridge.get("Enabled") is not True:
    bridge["Enabled"] = True
    changed = True
if bridge.get("TargetAllowList") != ["Editor"]:
    bridge["TargetAllowList"] = ["Editor"]
    changed = True

if changed:
    uproject_path.write_text(json.dumps(data, indent=2) + "\n")
    print("changed")
else:
    print("unchanged")
PY
pass ".uproject wiring is correct"

log "Ensuring root AGENTS.md includes Loomle guidance"
AGENTS_WRITE_RESULT="$(
python3 - <<'PY' "$PROJECT_ROOT" "$ROOT_AGENTS_PATH" "$ROOT_AGENTS_HINT"
from pathlib import Path
import sys

project_root = Path(sys.argv[1])
default_agents_path = Path(sys.argv[2])
hint = sys.argv[3]

try:
    candidates = []
    if project_root.exists():
        for p in project_root.iterdir():
            if p.is_file() and p.name.lower() == "agents.md":
                candidates.append(p)

    target = default_agents_path
    if candidates:
        exact = [p for p in candidates if p.name == "AGENTS.md"]
        target = exact[0] if exact else sorted(candidates, key=lambda x: x.name.lower())[0]

    existed = target.exists()
    text = target.read_text(encoding="utf-8", errors="replace") if existed else ""
    lines = text.splitlines()

    if hint in lines:
        state = "unchanged"
    else:
        updated = text
        if updated and not updated.endswith("\n"):
            updated += "\n"
        if updated and not updated.endswith("\n\n"):
            updated += "\n"
        updated += hint + "\n"
        target.write_text(updated, encoding="utf-8")
        state = "updated" if existed else "created"

    print(state)
    print(str(target))
except Exception as exc:
    print(f"failed: {exc}", file=sys.stderr)
    raise
PY
)" || fail "Failed to write root AGENTS.md guidance"

AGENTS_WRITE_STATE="$(printf '%s\n' "$AGENTS_WRITE_RESULT" | sed -n '1p')"
AGENTS_TARGET_PATH="$(printf '%s\n' "$AGENTS_WRITE_RESULT" | sed -n '2p')"
[[ -n "$AGENTS_TARGET_PATH" ]] || fail "Root AGENTS.md write result is missing target path"

python3 - <<'PY' "$AGENTS_TARGET_PATH" "$ROOT_AGENTS_HINT"
from pathlib import Path
import sys

path = Path(sys.argv[1])
hint = sys.argv[2]

if not path.exists():
    raise SystemExit(f"target file not found: {path}")
lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
if hint not in lines:
    raise SystemExit(f"guidance line missing in {path}")
PY
[[ "$AGENTS_WRITE_STATE" =~ ^(created|updated|unchanged)$ ]] || fail "Unexpected root AGENTS.md state: $AGENTS_WRITE_STATE"

if [[ "$AGENTS_WRITE_STATE" == "created" ]]; then
  pass "Created root AGENTS guidance at $AGENTS_TARGET_PATH"
elif [[ "$AGENTS_WRITE_STATE" == "updated" ]]; then
  pass "Updated root AGENTS guidance at $AGENTS_TARGET_PATH"
else
  pass "Root AGENTS guidance already up to date at $AGENTS_TARGET_PATH"
fi

check_local_prebuilt_compatibility() {
  [[ -f "$PLUGIN_BIN" ]] || return 1
  [[ -f "$PLUGIN_MODULES" ]] || return 1
  [[ -f "$UE_VERSION_FILE" ]] || return 1

  local ids plugin_build engine_build
  ids="$({
    python3 - <<'PY' "$PLUGIN_MODULES" "$UE_VERSION_FILE"
import json
import pathlib
import sys
mods = json.loads(pathlib.Path(sys.argv[1]).read_text())
ver = json.loads(pathlib.Path(sys.argv[2]).read_text())
print(str(mods.get('BuildId', '')).strip())
print(str(ver.get('CompatibleChangelist', '')).strip())
PY
  } 2>/dev/null || true)"

  plugin_build="$(printf '%s\n' "$ids" | sed -n '1p')"
  engine_build="$(printf '%s\n' "$ids" | sed -n '2p')"
  [[ -n "$plugin_build" && -n "$engine_build" && "$plugin_build" == "$engine_build" ]]
}

sync_built_plugin_artifacts() {
  [[ -f "$ROOT_BIN" ]] || fail "Built plugin binary not found at expected path: $ROOT_BIN"
  mkdir -p "$(dirname "$PLUGIN_BIN")"
  cp -f "$ROOT_BIN" "$PLUGIN_BIN"
  pass "Synchronized built plugin binary to plugin directory"

  if [[ -f "$ROOT_MODULES" ]]; then
    python3 - <<'PY' "$ROOT_MODULES" "$PLUGIN_MODULES" "$MODULE_BINARY_NAME"
import json
import pathlib
import sys

root_modules = pathlib.Path(sys.argv[1])
plugin_modules = pathlib.Path(sys.argv[2])
module_binary_name = sys.argv[3]

root_data = json.loads(root_modules.read_text())
plugin_data = {}
if plugin_modules.exists():
    plugin_data = json.loads(plugin_modules.read_text())

plugin_data["BuildId"] = root_data.get("BuildId", plugin_data.get("BuildId", ""))
mods = plugin_data.get("Modules")
if not isinstance(mods, dict):
    mods = {}
mods["LoomleBridge"] = module_binary_name
plugin_data["Modules"] = mods

plugin_modules.write_text(json.dumps(plugin_data, indent=2) + "\n")
PY
    pass "Synchronized plugin modules BuildId metadata"
  fi
}

should_sync_root_artifacts() {
  [[ -f "$ROOT_BIN" ]] || return 1

  if [[ ! -f "$PLUGIN_BIN" ]]; then
    return 0
  fi

  if [[ "$ROOT_BIN" -nt "$PLUGIN_BIN" ]]; then
    return 0
  fi

  local root_size plugin_size
  root_size="$(wc -c < "$ROOT_BIN" | tr -d '[:space:]')"
  plugin_size="$(wc -c < "$PLUGIN_BIN" | tr -d '[:space:]')"
  if [[ "$root_size" != "$plugin_size" ]]; then
    return 0
  fi

  if [[ -f "$ROOT_MODULES" ]]; then
    if [[ ! -f "$PLUGIN_MODULES" ]]; then
      return 0
    fi

    local ids root_build plugin_build
    ids="$(
      python3 - <<'PY' "$ROOT_MODULES" "$PLUGIN_MODULES"
import json
import pathlib
import sys
root_data = json.loads(pathlib.Path(sys.argv[1]).read_text())
plugin_data = json.loads(pathlib.Path(sys.argv[2]).read_text())
print(str(root_data.get("BuildId", "")).strip())
print(str(plugin_data.get("BuildId", "")).strip())
PY
    )" || return 0

    root_build="$(printf '%s\n' "$ids" | sed -n '1p')"
    plugin_build="$(printf '%s\n' "$ids" | sed -n '2p')"
    if [[ "$root_build" != "$plugin_build" ]]; then
      return 0
    fi
  fi

  return 1
}

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  [[ -x "$GEN_SCRIPT" ]] || fail "GenerateProjectFiles script missing: $GEN_SCRIPT"
  [[ -x "$BUILD_SCRIPT" ]] || fail "Build script missing: $BUILD_SCRIPT"

  BUILD_REQUIRED=1
  if [[ "$FORCE_BUILD" -eq 1 ]]; then
    BUILD_REQUIRED=1
    log "Forced by --force-build"
  elif check_local_prebuilt_compatibility; then
    BUILD_REQUIRED=0
    pass "Using compatible local prebuilt plugin"
  fi

  if [[ "$BUILD_REQUIRED" -eq 1 ]]; then
    log "Generating project files"
    "$GEN_SCRIPT" -project="$UPROJECT_PATH" -game >/dev/null
    pass "Project files generated"

    log "Building target: $TARGET_NAME ($BUILD_PLATFORM Development)"
    "$BUILD_SCRIPT" "$TARGET_NAME" "$BUILD_PLATFORM" Development "$UPROJECT_PATH" -WaitMutex
    pass "Editor target built"
    sync_built_plugin_artifacts
  else
    log "Skipping build; compatible prebuilt plugin is available"
  fi
else
  log "Skipping build (--skip-build)"
fi

if should_sync_root_artifacts; then
  log "Synchronizing root build artifacts to plugin directory"
  sync_built_plugin_artifacts
else
  log "Plugin binary artifacts already synchronized"
fi

if [[ "$SKIP_LAUNCH" -eq 0 ]]; then
  log "Launching Unreal Editor"
  case "$HOST_OS" in
    Darwin)
      [[ -d "$UE_APP" ]] || fail "UnrealEditor.app not found: $UE_APP"
      open -na "$UE_APP" --args "$UPROJECT_PATH"
      ;;
    Linux)
      [[ -x "$UE_BIN" ]] || fail "UnrealEditor binary not found/executable: $UE_BIN"
      "$UE_BIN" "$UPROJECT_PATH" >/dev/null 2>&1 &
      ;;
  esac
  pass "Launch command sent"
else
  log "Skipping launch (--skip-launch)"
fi

if [[ "$SKIP_VERIFY" -eq 0 ]]; then
  log "Waiting for bridge socket: $SOCKET_PATH"
  for _ in $(seq 1 60); do
    if [[ -S "$SOCKET_PATH" ]]; then
      break
    fi
    sleep 1
  done

  [[ -S "$SOCKET_PATH" ]] || fail "bridge socket not ready: $SOCKET_PATH"
  pass "bridge socket is ready"

  log "Running bridge protocol checks"
  python3 "$SCRIPT_DIR/verify_bridge.py" --socket "$SOCKET_PATH"
else
  log "Skipping bridge verification (--skip-verify)"
fi

pass "Loomle install flow completed for $PROJECT_NAME"
