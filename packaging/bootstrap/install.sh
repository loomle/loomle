#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"
INSTALL_DIR="${LOOMLE_INSTALL_DIR:-$HOME/.local/bin}"

resolve_release_tag() {
  if [[ "$VERSION" == "latest" ]]; then
    echo "loomle-latest"
  elif [[ "$VERSION" == v* ]]; then
    echo "$VERSION"
  else
    echo "v$VERSION"
  fi
}

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
  local release_tag
  local asset_name
  local download_url
  local tmp_dir
  local target

  platform="$(detect_platform)"
  release_tag="$(resolve_release_tag)"
  case "$platform" in
    darwin) asset_name="loomle-darwin" ;;
    linux)
      echo "[loomle-bootstrap] Linux bootstrap artifacts are not published yet." >&2
      echo "[loomle-bootstrap] Build from source or install from a local release bundle for now." >&2
      exit 1
      ;;
    *)
      echo "Unsupported bootstrap asset platform: $platform" >&2
      exit 1
      ;;
  esac
  download_url="https://github.com/${RELEASE_REPO}/releases/download/${release_tag}/${asset_name}"
  tmp_dir="$(mktemp -d)"
  target="${INSTALL_DIR}/loomle"

  mkdir -p "${INSTALL_DIR}"

  echo "[loomle-bootstrap] downloading ${download_url}"
  curl -fsSL "${download_url}" -o "${tmp_dir}/loomle"
  chmod +x "${tmp_dir}/loomle"
  mv "${tmp_dir}/loomle" "${target}"
  rm -rf "${tmp_dir}"

  cat <<EOF
[loomle-bootstrap] installed ${target}
[loomle-bootstrap] next step:
  loomle install --project-root /path/to/MyProject
[loomle-bootstrap] LOOMLE installs both plugin binaries and plugin source by default.
[loomle-bootstrap] to check for future updates inside an installed project, run:
  loomle update
[loomle-bootstrap] to upgrade an installed project in place, run:
  loomle update --apply
EOF
}

main "$@"
