#!/usr/bin/env python3
import argparse
import json
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


def read_cargo_lock_package_version(path: Path, package_name: str) -> str:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    for package in data.get("package", []):
        if package.get("name") == package_name:
            version = package.get("version")
            if not isinstance(version, str) or not version:
                raise SystemExit(
                    f"invalid {package_name} package version in {path}: {version!r}"
                )
            return version
    raise SystemExit(f"missing {package_name} package in {path}")


def read_uplugin_version_name(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    version_name = data.get("VersionName")
    if not isinstance(version_name, str) or not version_name:
        raise SystemExit(f"invalid VersionName in {path}: {version_name!r}")
    return version_name


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
        "client_lock": read_cargo_lock_package_version(
            repo_root / "client/Cargo.lock", "loomle"
        ),
        "loomle_bridge_uplugin": read_uplugin_version_name(
            repo_root / "engine/LoomleBridge/LoomleBridge.uplugin"
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
