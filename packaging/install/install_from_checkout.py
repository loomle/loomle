#!/usr/bin/env python3
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def run_capture(cmd: list[str], cwd: Path) -> dict:
    result = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        fail(f"command failed ({result.returncode}): {' '.join(cmd)}")
    try:
        return json.loads(result.stdout)
    except Exception as exc:
        fail(f"failed to parse JSON output from {' '.join(cmd)}: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and install LOOMLE into a UE project from a source checkout.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--version", default="0.0.0-dev")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    project_root = Path(args.project_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    if not project_root.exists():
        fail(f"project root not found: {project_root}")

    build_script = repo_root / "packaging" / "release" / "build_local_release.py"
    install_script = repo_root / "packaging" / "install" / "install_release.py"

    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    build_cmd = [
        "python3",
        str(build_script),
        "--repo-root",
        str(repo_root),
        "--output-dir",
        str(output_dir),
        "--version",
        args.version,
    ]
    if args.platform:
        build_cmd.extend(["--platform", args.platform])
    build_result = run_capture(build_cmd, cwd=repo_root)

    platform = build_result.get("platform")
    bundle_dir = build_result.get("bundleDir")
    manifest = build_result.get("manifest")
    if not all(isinstance(value, str) and value for value in [platform, bundle_dir, manifest]):
        fail("build output missing required fields")

    install_result = run_capture(
        [
            "python3",
            str(install_script),
            "--bundle-root",
            bundle_dir,
            "--project-root",
            str(project_root),
            "--manifest-path",
            manifest,
            "--platform",
            platform,
            "--version",
            args.version,
        ],
        cwd=repo_root,
    )

    result = {
        "build": build_result,
        "install": install_result,
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
