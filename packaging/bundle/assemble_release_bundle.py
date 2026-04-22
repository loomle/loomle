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


def copy_tree(source: Path, destination: Path, ignore_names: set[str] | None = None) -> None:
    if not source.exists():
        fail(f"source not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    ignore = shutil.ignore_patterns(*(ignore_names or set()))
    shutil.copytree(source, destination, ignore=ignore)


def copy_file(source: Path, destination: Path) -> None:
    if not source.exists():
        fail(f"file not found: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


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
    release_plugin_cache = output_dir / "plugin-cache" / "LoomleBridge"

    reset_dir(output_dir)
    copy_tree(engine_plugin, release_plugin_cache, {"Intermediate", "Saved", ".DS_Store"})

    client_binary = Path(args.client_binary).resolve()
    copy_file(
        client_binary,
        output_dir / client_binary.name,
    )

    manifest = {
        "bundleRoot": str(output_dir),
        "pluginCache": str(release_plugin_cache),
        "loomle": str(output_dir / client_binary.name),
        "clientBinaryIncluded": True,
    }
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
