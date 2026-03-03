#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOOMLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

REMOTE="origin"
TARGET_BRANCH=""
ALLOW_DIRTY=0
INSTALL_ARGS=()

log() { printf '[INFO] %s\n' "$1"; }
pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

usage() {
  cat <<EOF
Usage: $(basename "$0") [--remote <name>] [--branch <name>] [--allow-dirty] [-- <install_args...>]

Upgrades Loomle from source (git pull --ff-only) and then runs platform install flow.

Examples:
  $(basename "$0")
  $(basename "$0") -- --skip-launch
  $(basename "$0") --branch main -- --skip-build --skip-verify
EOF
}

run_install_flow() {
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

      log "Detected Windows shell ($uname_s); running install_loomle_windows.ps1"
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$win_installer_path" "${INSTALL_ARGS[@]}"
      ;;
    *)
      "$SCRIPT_DIR/install_loomle.sh" "${INSTALL_ARGS[@]}"
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --remote)
      [[ $# -ge 2 ]] || fail "--remote requires a value"
      REMOTE="$2"
      shift 2
      ;;
    --branch)
      [[ $# -ge 2 ]] || fail "--branch requires a value"
      TARGET_BRANCH="$2"
      shift 2
      ;;
    --allow-dirty)
      ALLOW_DIRTY=1
      shift
      ;;
    --)
      shift
      INSTALL_ARGS=("$@")
      break
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

command -v git >/dev/null 2>&1 || fail "git is required"

git -C "$LOOMLE_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "$LOOMLE_DIR is not a git repository"

if [[ "$ALLOW_DIRTY" -eq 0 ]]; then
  if [[ -n "$(git -C "$LOOMLE_DIR" status --porcelain)" ]]; then
    fail "Loomle repo has local changes. Commit/stash them first, or use --allow-dirty."
  fi
fi

CURRENT_BRANCH="$(git -C "$LOOMLE_DIR" rev-parse --abbrev-ref HEAD)"
if [[ "$CURRENT_BRANCH" == "HEAD" && -z "$TARGET_BRANCH" ]]; then
  fail "Detached HEAD detected. Use --branch <name>."
fi
if [[ -z "$TARGET_BRANCH" ]]; then
  TARGET_BRANCH="$CURRENT_BRANCH"
fi

if ! git -C "$LOOMLE_DIR" remote get-url "$REMOTE" >/dev/null 2>&1; then
  fail "Remote '$REMOTE' not found in $LOOMLE_DIR"
fi

log "Fetching $REMOTE/$TARGET_BRANCH"
git -C "$LOOMLE_DIR" fetch "$REMOTE" "$TARGET_BRANCH" --prune
pass "Fetch complete"

if [[ "$CURRENT_BRANCH" != "$TARGET_BRANCH" ]]; then
  if git -C "$LOOMLE_DIR" show-ref --verify --quiet "refs/heads/$TARGET_BRANCH"; then
    log "Checking out branch $TARGET_BRANCH"
    git -C "$LOOMLE_DIR" checkout "$TARGET_BRANCH"
  else
    log "Creating local branch $TARGET_BRANCH tracking $REMOTE/$TARGET_BRANCH"
    git -C "$LOOMLE_DIR" checkout -b "$TARGET_BRANCH" --track "$REMOTE/$TARGET_BRANCH"
  fi
fi

LOCAL_SHA="$(git -C "$LOOMLE_DIR" rev-parse HEAD)"
REMOTE_SHA="$(git -C "$LOOMLE_DIR" rev-parse "$REMOTE/$TARGET_BRANCH")"

if [[ "$LOCAL_SHA" == "$REMOTE_SHA" ]]; then
  pass "Already up to date at $(git -C "$LOOMLE_DIR" rev-parse --short HEAD)"
else
  if ! git -C "$LOOMLE_DIR" merge-base --is-ancestor "$LOCAL_SHA" "$REMOTE_SHA"; then
    fail "Cannot fast-forward to $REMOTE/$TARGET_BRANCH. Rebase/merge manually, then rerun."
  fi
  log "Fast-forwarding to $REMOTE/$TARGET_BRANCH"
  git -C "$LOOMLE_DIR" pull --ff-only "$REMOTE" "$TARGET_BRANCH"
  pass "Updated to $(git -C "$LOOMLE_DIR" rev-parse --short HEAD)"
fi

log "Running install flow"
run_install_flow
pass "Loomle source upgrade completed"
