#!/usr/bin/env python3
import argparse
import json
import re
import sys
import tomllib
from pathlib import Path


def read_cargo_version(path: Path) -> str:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    try:
        version = data["package"]["version"]
    except KeyError as exc:
        raise SystemExit(f"missing package.version in {path}: {exc}") from exc
    if not isinstance(version, str) or not version:
        raise SystemExit(f"invalid package.version in {path}: {version!r}")
    return version


def read_uplugin_version_name(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    version_name = data.get("VersionName")
    if not isinstance(version_name, str) or not version_name:
        raise SystemExit(f"invalid VersionName in {path}: {version_name!r}")
    return version_name


def read_bridge_plugin_version(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    match = re.search(r'PluginVersion\s*=\s*TEXT\("([^"]+)"\)', source)
    if not match:
        raise SystemExit(f"missing PluginVersion constant in {path}")
    version = match.group(1)
    if not version:
        raise SystemExit(f"invalid PluginVersion in {path}: {version!r}")
    return version


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify LOOMLE release version strings stay in sync."
    )
    parser.add_argument(
        "--repo-root",
        default=Path(__file__).resolve().parents[1],
        type=Path,
        help="Repository root containing Cargo.toml and LoomleBridge.uplugin",
    )
    parser.add_argument(
        "--expected-version",
        default="",
        help="Optional exact version all release-facing version strings must match",
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    versions = {
        "client": read_cargo_version(repo_root / "client/Cargo.toml"),
        "loomle_bridge_uplugin": read_uplugin_version_name(
            repo_root / "engine/LoomleBridge/LoomleBridge.uplugin"
        ),
        "loomle_bridge_runtime": read_bridge_plugin_version(
            repo_root
            / "engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeModule.cpp"
        ),
    }

    distinct_versions = sorted(set(versions.values()))
    if len(distinct_versions) != 1:
        details = ", ".join(f"{name}={version}" for name, version in versions.items())
        raise SystemExit(f"release version mismatch: {details}")

    if args.expected_version and distinct_versions[0] != args.expected_version:
        raise SystemExit(
            "release version mismatch: expected "
            f"{args.expected_version}, found {distinct_versions[0]}"
        )

    print(f"release versions aligned: {distinct_versions[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
