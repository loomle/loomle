#!/usr/bin/env bash
set -euo pipefail

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
warn() { printf '[WARN] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

PROJECT_ROOT="$PWD"
FIX=0
RUN_BENCH=0
SOCKET_PATH=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--project-root <path>] [--fix] [--run-bench] [--socket <path>]

Verifies Loomle project-local install and required runtime settings.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --project-root)
      [[ $# -ge 2 ]] || fail "--project-root requires a value"
      PROJECT_ROOT="$2"
      shift 2
      ;;
    --fix)
      FIX=1
      shift
      ;;
    --run-bench)
      RUN_BENCH=1
      shift
      ;;
    --socket)
      [[ $# -ge 2 ]] || fail "--socket requires a value"
      SOCKET_PATH="$2"
      shift 2
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

PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"
[[ -f "$PROJECT_ROOT"/*.uproject ]] || warn "No .uproject found at project root: $PROJECT_ROOT"

PLUGIN_DIR="$PROJECT_ROOT/Loomle/Plugins/LoomleBridge"
[[ -d "$PLUGIN_DIR" ]] || fail "Missing plugin directory: $PLUGIN_DIR"
pass "Plugin exists: $PLUGIN_DIR"

CFG_PATH="$PROJECT_ROOT/Config/DefaultEditorSettings.ini"
mkdir -p "$(dirname "$CFG_PATH")"
[[ -f "$CFG_PATH" ]] || touch "$CFG_PATH"

CHECK_RESULT="$(python3 - <<'PY' "$CFG_PATH" "$FIX"
import pathlib
import sys

cfg_path = pathlib.Path(sys.argv[1])
fix = sys.argv[2] == "1"
section = "[/Script/UnrealEd.EditorPerformanceSettings]"
key = "bThrottleCPUWhenNotForeground"
required = "False"

lines = cfg_path.read_text().splitlines()
found_section = False
section_idx = -1
key_idx = -1

for i, line in enumerate(lines):
    s = line.strip()
    if s.startswith("[") and s.endswith("]"):
        if s == section:
            found_section = True
            section_idx = i
        elif found_section and key_idx == -1:
            break
    if found_section and s.startswith(f"{key}="):
        key_idx = i
        break

changed = False
if key_idx >= 0:
    current = lines[key_idx].split("=", 1)[1].strip()
    if current != required:
        if fix:
            lines[key_idx] = f"{key}={required}"
            changed = True
        else:
            print("mismatch")
            raise SystemExit(0)
else:
    if fix:
        if lines and lines[-1].strip() != "":
            lines.append("")
        if not found_section:
            lines.append(section)
        lines.append(f"{key}={required}")
        changed = True
    else:
        print("missing")
        raise SystemExit(0)

if changed:
    cfg_path.write_text("\n".join(lines) + "\n")
    print("fixed")
else:
    print("ok")
PY
)"

case "$CHECK_RESULT" in
  ok)
    pass "Editor performance setting is correct"
    ;;
  fixed)
    pass "Editor performance setting fixed"
    ;;
  missing|mismatch)
    fail "Editor performance setting invalid ($CHECK_RESULT). Rerun with --fix"
    ;;
  *)
    fail "Unexpected config check result: $CHECK_RESULT"
    ;;
esac

if [[ -z "$SOCKET_PATH" ]]; then
  SOCKET_PATH="$PROJECT_ROOT/Intermediate/loomle.sock"
fi

if [[ -S "$SOCKET_PATH" || -p "$SOCKET_PATH" || -e "$SOCKET_PATH" ]]; then
  pass "Bridge endpoint found: $SOCKET_PATH"
else
  warn "Bridge endpoint not found yet: $SOCKET_PATH"
fi

if [[ "$RUN_BENCH" -eq 1 ]]; then
  BENCH_SCRIPT="$PROJECT_ROOT/Loomle/scripts/benchmark_loomle_bridge.py"
  [[ -f "$BENCH_SCRIPT" ]] || fail "Benchmark script not found: $BENCH_SCRIPT"
  if [[ ! -e "$SOCKET_PATH" ]]; then
    fail "Cannot run benchmark without endpoint: $SOCKET_PATH"
  fi
  log "Running bridge benchmark"
  python3 "$BENCH_SCRIPT" --socket "$SOCKET_PATH" --tool loomle --total 200 --concurrency 1 --warmup 20
  pass "Bridge benchmark completed"
fi
