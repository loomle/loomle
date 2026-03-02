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

UE_APP="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app"
UE_VERSION_FILE="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.version"
GEN_SCRIPT="/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh"
BUILD_SCRIPT="/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh"

SKIP_BUILD=0
SKIP_LAUNCH=0
SKIP_VERIFY=0
FORCE_BUILD=0

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

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

command -v python3 >/dev/null 2>&1 || fail "python3 is required"

UPROJECT_PATH="$(find "$PROJECT_ROOT" -maxdepth 1 -type f -name '*.uproject' | head -n 1)"
[[ -n "$UPROJECT_PATH" ]] || fail "No .uproject found in $PROJECT_ROOT"
UPROJECT_NAME="$(basename "$UPROJECT_PATH")"
PROJECT_NAME="${UPROJECT_NAME%.uproject}"
TARGET_NAME="${PROJECT_NAME}Editor"
PLUGIN_DIR="$PROJECT_ROOT/Loomle/Plugins/LoomleMcpBridge"
SOCKET_PATH="$PROJECT_ROOT/Intermediate/loomle-mcp.sock"
ROOT_AGENTS_PATH="$PROJECT_ROOT/AGENTS.md"
ROOT_AGENTS_HINT="- Always read ./Loomle/AGENTS.md before starting work in this project."
PLUGIN_BIN="$PLUGIN_DIR/Binaries/Mac/UnrealEditor-LoomleMcpBridge.dylib"
PLUGIN_MODULES="$PLUGIN_DIR/Binaries/Mac/UnrealEditor.modules"

[[ -d "$PLUGIN_DIR" ]] || fail "Plugin not found: $PLUGIN_DIR"
pass "Plugin directory exists"

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
if [[ -f "$ROOT_AGENTS_PATH" ]]; then
  if grep -Fqx -- "$ROOT_AGENTS_HINT" "$ROOT_AGENTS_PATH"; then
    pass "Root AGENTS.md already contains Loomle guidance"
  else
    printf '\n%s\n' "$ROOT_AGENTS_HINT" >> "$ROOT_AGENTS_PATH"
    pass "Appended Loomle guidance to root AGENTS.md"
  fi
else
  printf '%s\n' "$ROOT_AGENTS_HINT" > "$ROOT_AGENTS_PATH"
  pass "Created root AGENTS.md with Loomle guidance"
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
