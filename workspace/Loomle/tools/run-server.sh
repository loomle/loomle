#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

"${PROJECT_ROOT}/Loomle/client/loomle" run-server --project-root "${PROJECT_ROOT}" -- "$@"
