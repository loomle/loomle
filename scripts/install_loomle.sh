#!/usr/bin/env bash
set -euo pipefail

# Install contract (single entrypoint, idempotent):
# 1) Ensure .uproject wiring (AdditionalPluginDirectories + LoomleMcpBridge Editor enablement)
# 2) Resolve plugin binary (compatible local prebuilt first, local build fallback)
# 3) Launch Unreal Editor
# 4) Verify bridge transport endpoint (socket / named pipe equivalent)
# 5) Verify MCP baseline tools (loomle/context/selection/live/execute) and unreal.BlueprintGraphBridge

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOMLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$LOOMLE_DIR/.." && pwd)"

run_windows_installer_if_needed() {
  local uname_s uname_lc win_installer win_installer_path
  uname_s="$(uname -s 2>/dev/null || echo unknown)"
  uname_lc="${uname_s,,}"

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

UE_APP="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app"
UE_VERSION_FILE="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.version"
GEN_SCRIPT="/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh"
BUILD_SCRIPT="/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh"

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
SOCKET_PATH="$PROJECT_ROOT/Intermediate/loomle-mcp.sock"
ROOT_AGENTS_PATH="$PROJECT_ROOT/AGENTS.md"
ROOT_AGENTS_HINT="- Always read ./Loomle/AGENTS.md before starting work in this project."
ROOT_BIN="$PROJECT_ROOT/Binaries/Mac/UnrealEditor-LoomleMcpBridge.dylib"
ROOT_MODULES="$PROJECT_ROOT/Binaries/Mac/UnrealEditor.modules"

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
    plugin_dir = base / "LoomleMcpBridge"
    if (plugin_dir / "LoomleMcpBridge.uplugin").exists():
        print(str(plugin_dir))
        sys.exit(0)

fallback = (project_root / "Loomle" / "Plugins" / "LoomleMcpBridge").resolve()
print(str(fallback))
PY
)"

PLUGIN_BIN="$PLUGIN_DIR/Binaries/Mac/UnrealEditor-LoomleMcpBridge.dylib"
PLUGIN_MODULES="$PLUGIN_DIR/Binaries/Mac/UnrealEditor.modules"

[[ -d "$PLUGIN_DIR" ]] || fail "Plugin not found from project config/fallback: $PLUGIN_DIR"
pass "Plugin directory resolved: $PLUGIN_DIR"

log "Ensuring .uproject wiring for AdditionalPluginDirectories + LoomleMcpBridge"
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
    if isinstance(p, dict) and p.get("Name") == "LoomleMcpBridge":
        bridge = p
        break
if bridge is None:
    bridge = {"Name": "LoomleMcpBridge"}
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
  cp -f "$ROOT_BIN" "$PLUGIN_BIN"
  pass "Synchronized built plugin binary to plugin directory"

  if [[ -f "$ROOT_MODULES" ]]; then
    python3 - <<'PY' "$ROOT_MODULES" "$PLUGIN_MODULES"
import json
import pathlib
import sys

root_modules = pathlib.Path(sys.argv[1])
plugin_modules = pathlib.Path(sys.argv[2])

root_data = json.loads(root_modules.read_text())
plugin_data = {}
if plugin_modules.exists():
    plugin_data = json.loads(plugin_modules.read_text())

plugin_data["BuildId"] = root_data.get("BuildId", plugin_data.get("BuildId", ""))
mods = plugin_data.get("Modules")
if not isinstance(mods, dict):
    mods = {}
mods["LoomleMcpBridge"] = "UnrealEditor-LoomleMcpBridge.dylib"
plugin_data["Modules"] = mods

plugin_modules.write_text(json.dumps(plugin_data, indent=2) + "\n")
PY
    pass "Synchronized plugin modules BuildId metadata"
  fi
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

    log "Building target: $TARGET_NAME (Mac Development)"
    "$BUILD_SCRIPT" "$TARGET_NAME" Mac Development "$UPROJECT_PATH" -WaitMutex
    pass "Editor target built"
    sync_built_plugin_artifacts
  else
    log "Skipping build; compatible prebuilt plugin is available"
  fi
else
  log "Skipping build (--skip-build)"
fi

if [[ "$SKIP_LAUNCH" -eq 0 ]]; then
  [[ -d "$UE_APP" ]] || fail "UnrealEditor.app not found: $UE_APP"
  log "Launching Unreal Editor"
  open -na "$UE_APP" --args "$UPROJECT_PATH"
  pass "Launch command sent"
else
  log "Skipping launch (--skip-launch)"
fi

if [[ "$SKIP_VERIFY" -eq 0 ]]; then
  log "Waiting for MCP socket: $SOCKET_PATH"
  for _ in $(seq 1 60); do
    if [[ -S "$SOCKET_PATH" ]]; then
      break
    fi
    sleep 1
  done

  [[ -S "$SOCKET_PATH" ]] || fail "MCP socket not ready: $SOCKET_PATH"
  pass "MCP socket is ready"

  log "Running bridge protocol checks"
  python3 "$SCRIPT_DIR/verify_bridge.py" --socket "$SOCKET_PATH"
else
  log "Skipping bridge verification (--skip-verify)"
fi

pass "Loomle install flow completed for $PROJECT_NAME"
