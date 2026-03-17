#!/usr/bin/env bash
set -euo pipefail

RELEASE_REPO="${LOOMLE_RELEASE_REPO:-loomle/loomle}"
VERSION="${LOOMLE_BOOTSTRAP_VERSION:-latest}"

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
    darwin) asset_name="loomle-installer" ;;
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
  target="${tmp_dir}/${asset_name}"

  echo "[loomle-bootstrap] downloading ${download_url}"
  curl -fsSL "${download_url}" -o "${target}"
  chmod +x "${target}"

  if [[ "$#" -eq 0 ]]; then
    cat <<EOF
[loomle-bootstrap] downloaded temporary installer ${target}
[loomle-bootstrap] usage example:
  curl -fsSL https://loomle.ai/install.sh | sh -s -- install --project-root /path/to/MyProject
[loomle-bootstrap] no installer arguments supplied; deleting temporary installer
EOF
    rm -rf "${tmp_dir}"
    exit 2
  fi

  set +e
  "${target}" "$@"
  exit_code=$?
  set -e
  rm -rf "${tmp_dir}"
  exit "$exit_code"
}

main "$@"
