#!/usr/bin/env python3
import argparse
import json
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


def strip_plugin_source_for_precompiled_bundle(plugin_root: Path, platform: str) -> None:
    binary_platform_dir = {
        "darwin": "Mac",
        "linux": "Linux",
        "windows": "Win64",
    }.get(platform)
    if not binary_platform_dir:
        return

    source_dir = plugin_root / "Source"
    if not source_dir.is_dir():
        return
    if not (plugin_root / "Binaries" / binary_platform_dir).is_dir():
        return

    shutil.rmtree(source_dir)


def main() -> int:
    parser = argparse.ArgumentParser(description="Assemble a LOOMLE release bundle from the source repository.")
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--server-binary", required=True)
    parser.add_argument("--client-binary", required=True)
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir).resolve()

    engine_plugin = repo_root / "engine" / "LoomleBridge"
    workspace_root = repo_root / "workspace" / "Loomle"
    release_plugin = output_dir / "plugin" / "LoomleBridge"
    release_workspace = output_dir / "workspace" / "Loomle"

    reset_dir(output_dir)
    copy_tree(engine_plugin, release_plugin)
    copy_tree(workspace_root, release_workspace)

    server_binary = Path(args.server_binary).resolve()
    copy_file(
        server_binary,
        release_plugin / "Tools" / "mcp" / args.platform / server_binary.name,
    )

    client_binary = Path(args.client_binary).resolve()
    copy_file(
        client_binary,
        output_dir / "mcp" / "client" / args.platform / client_binary.name,
    )
    copy_file(
        client_binary,
        release_workspace / "client" / client_binary.name,
    )
    strip_plugin_source_for_precompiled_bundle(release_plugin, args.platform)

    manifest = {
        "bundleRoot": str(output_dir),
        "plugin": str(release_plugin),
        "workspace": str(release_workspace),
        "serverBinaryIncluded": True,
        "clientBinaryIncluded": True,
    }
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
