#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${LOOMLE_BOOTSTRAP_BASE_URL:-https://loomle.ai}"
VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"
INSTALL_DIR="${LOOMLE_INSTALL_DIR:-$HOME/.local/bin}"
CLI_NAME="loomle"

detect_platform() {
  case "$(uname -s)" in
    Darwin) echo "darwin" ;;
    Linux) echo "linux" ;;
    *)
      echo "Unsupported platform: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

main() {
  local platform
  local download_url
  local tmp_dir
  local target

  platform="$(detect_platform)"
  download_url="${BASE_URL}/downloads/bootstrap/${VERSION}/${platform}/${CLI_NAME}"
  tmp_dir="$(mktemp -d)"
  target="${INSTALL_DIR}/${CLI_NAME}"

  mkdir -p "${INSTALL_DIR}"

  echo "[loomle-bootstrap] downloading ${download_url}"
  curl -fsSL "${download_url}" -o "${tmp_dir}/${CLI_NAME}"
  chmod +x "${tmp_dir}/${CLI_NAME}"
  mv "${tmp_dir}/${CLI_NAME}" "${target}"
  rm -rf "${tmp_dir}"

  cat <<EOF
[loomle-bootstrap] installed ${target}
[loomle-bootstrap] next step:
  loomle install --project-root /path/to/MyProject
EOF
}

main "$@"
