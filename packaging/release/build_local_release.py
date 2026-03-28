#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def run(cmd: list[str], cwd: Path) -> None:
    result = subprocess.run(cmd, cwd=str(cwd), stdout=sys.stderr, stderr=sys.stderr)
    if result.returncode != 0:
        fail(f"command failed ({result.returncode}): {' '.join(cmd)}")


def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def client_binary_name(platform: str) -> str:
    if platform == "windows":
        return "loomle.exe"
    if platform in {"darwin", "linux"}:
        return "loomle"
    fail(f"unsupported platform: {platform}")


def detect_platform() -> str:
    if sys.platform == "darwin":
        return "darwin"
    if sys.platform.startswith("win"):
        return "windows"
    return "linux"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a local LOOMLE release bundle from a source checkout.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--version", default="0.0.0-dev")
    parser.add_argument("--platform", default=detect_platform())
    parser.add_argument("--asset-url", default="")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    bundle_dir = output_dir / "bundle"
    bootstrap_dir = output_dir / "bootstrap"
    manifest_path = output_dir / "manifest.json"

    client_dir = repo_root / "client"
    assemble_script = repo_root / "packaging" / "bundle" / "assemble_release_bundle.py"
    manifest_script = repo_root / "packaging" / "bundle" / "build_release_manifest.py"
    zip_script = repo_root / "packaging" / "release" / "write_bundle_zip.py"

    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    bootstrap_dir.mkdir(parents=True, exist_ok=True)

    run(["cargo", "build", "--release"], cwd=client_dir)

    client_name = client_binary_name(args.platform)
    client_binary = client_dir / "target" / "release" / client_name
    if not client_binary.is_file():
        fail(f"client binary not found: {client_binary}")

    assemble_cmd = [
        sys.executable,
        str(assemble_script),
        "--repo-root",
        str(repo_root),
        "--output-dir",
        str(bundle_dir),
        "--platform",
        args.platform,
        "--client-binary",
        str(client_binary),
    ]
    run(assemble_cmd, cwd=repo_root)

    for script_name in ("install.sh", "install.ps1", "update.sh", "update.ps1"):
        shutil.copy2(client_dir / script_name, bootstrap_dir / script_name)

    archive_stem = output_dir / f"loomle-{args.version}-{args.platform}"
    archive_path = output_dir / f"{archive_stem.name}.zip"
    run(
        [
            sys.executable,
            str(zip_script),
            "--bundle-dir",
            str(bundle_dir),
            "--archive-path",
            str(archive_path),
        ],
        cwd=repo_root,
    )
    package_sha = sha256_file(archive_path)
    client_sha = sha256_file(client_binary)
    asset_url = args.asset_url or archive_path.as_uri()
    manifest_cmd = [
        sys.executable,
        str(manifest_script),
        "--manifest-path",
        str(manifest_path),
        "--version",
        args.version,
        "--platform",
        args.platform,
        "--asset-url",
        asset_url,
        "--sha256",
        package_sha,
        "--client-binary-relpath",
        f"workspace/Loomle/{client_name}",
        "--client-sha256",
        client_sha,
    ]
    run(manifest_cmd, cwd=repo_root)

    result = {
        "repoRoot": str(repo_root),
        "outputDir": str(output_dir),
        "bundleDir": str(bundle_dir),
        "bootstrapDir": str(bootstrap_dir),
        "archive": str(archive_path),
        "manifest": str(manifest_path),
        "platform": args.platform,
        "version": args.version,
        "clientBinary": str(client_binary),
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
