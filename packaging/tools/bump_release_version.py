#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")


def replace_one(pattern: str, replacement: str, text: str, path: Path) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"failed to update expected version field in {path}")
    return updated


def update_cargo_toml(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated = replace_one(
        r'(^version\s*=\s*)"[^"]+"',
        rf'\g<1>"{version}"',
        text,
        path,
    )
    path.write_text(updated, encoding="utf-8")


def update_cargo_lock(path: Path, package_name: str, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    pattern = (
        rf'(\[\[package\]\]\nname = "{re.escape(package_name)}"\nversion = )"[^"]+"'
    )
    updated = replace_one(pattern, rf'\g<1>"{version}"', text, path)
    path.write_text(updated, encoding="utf-8")


def update_uplugin(path: Path, version: str, build_number: int | None) -> int:
    data = json.loads(path.read_text(encoding="utf-8"))
    current_build_number = data.get("Version")
    if not isinstance(current_build_number, int):
        raise SystemExit(f"invalid Version in {path}: {current_build_number!r}")

    next_build_number = (
        build_number if build_number is not None else current_build_number + 1
    )
    if next_build_number < current_build_number:
        raise SystemExit(
            f"new uplugin Version {next_build_number} is lower than current "
            f"{current_build_number}"
        )

    data["Version"] = next_build_number
    data["VersionName"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return next_build_number


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Bump all release-facing LOOMLE version fields."
    )
    parser.add_argument("version", help="New semantic version, for example 0.5.5")
    parser.add_argument(
        "--repo-root",
        default=Path(__file__).resolve().parents[2],
        type=Path,
        help="Repository root",
    )
    parser.add_argument(
        "--uplugin-version",
        type=int,
        default=None,
        help="Explicit integer LoomleBridge.uplugin Version. Defaults to +1.",
    )
    args = parser.parse_args()

    if not SEMVER_RE.match(args.version):
        raise SystemExit(f"invalid release version: {args.version!r}")

    repo_root = args.repo_root.resolve()
    update_cargo_toml(repo_root / "client/Cargo.toml", args.version)
    update_cargo_lock(repo_root / "client/Cargo.lock", "loomle", args.version)
    uplugin_build = update_uplugin(
        repo_root / "engine/LoomleBridge/LoomleBridge.uplugin",
        args.version,
        args.uplugin_version,
    )

    print(
        f"bumped release version to {args.version} "
        f"(LoomleBridge.uplugin Version {uplugin_build})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
