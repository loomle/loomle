#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def copy_tree(source: Path, destination: Path) -> None:
    if not source.exists():
        fail(f"source not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


def copy_file(source: Path, destination: Path) -> None:
    if not source.exists():
        fail(f"file not found: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def ensure_executable(path: Path) -> None:
    try:
        mode = path.stat().st_mode
        path.chmod(mode | 0o755)
    except Exception as exc:
        fail(f"failed to mark executable {path}: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Assemble a LOOMLE release bundle from the source repository.")
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--client-binary", required=True)
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir).resolve()

    engine_plugin = repo_root / "engine" / "LoomleBridge"
    workspace_root = repo_root / "workspace" / "Loomle"
    client_root = repo_root / "client"
    install_helper = repo_root / "packaging" / "install" / "install_release.py"
    release_plugin = output_dir / "plugin" / "LoomleBridge"
    release_workspace = output_dir / "Loomle"

    reset_dir(output_dir)
    copy_tree(engine_plugin, release_plugin)
    copy_tree(workspace_root, release_workspace)
    copy_file(client_root / "update.sh", release_workspace / "update.sh")
    copy_file(client_root / "update.ps1", release_workspace / "update.ps1")
    copy_file(client_root / "doctor.sh", release_workspace / "doctor.sh")
    copy_file(client_root / "doctor.ps1", release_workspace / "doctor.ps1")
    copy_file(install_helper, release_workspace / "runtime" / "install_release.py")
    ensure_executable(release_workspace / "update.sh")
    ensure_executable(release_workspace / "doctor.sh")

    client_binary = Path(args.client_binary).resolve()
    copy_file(
        client_binary,
        release_workspace / client_binary.name,
    )

    manifest = {
        "bundleRoot": str(output_dir),
        "plugin": str(release_plugin),
        "loomle": str(release_workspace),
        "clientBinaryIncluded": True,
    }
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
