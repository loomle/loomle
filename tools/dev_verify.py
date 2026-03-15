#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools"
INSTALL_FROM_CHECKOUT = REPO_ROOT / "packaging" / "install" / "install_from_checkout.py"
SMOKE_SCRIPT = REPO_ROOT / "tests" / "e2e" / "test_bridge_smoke.py"
REGRESSION_SCRIPT = REPO_ROOT / "tests" / "e2e" / "test_bridge_regression.py"
LATENCY_SCRIPT = REPO_ROOT / "tests" / "integration" / "test_loomle_latency.py"


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def step(message: str) -> None:
    print(f"[STEP] {message}", file=sys.stderr)


def detect_platform() -> str:
    if sys.platform == "darwin":
        return "darwin"
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform.startswith("linux"):
        return "linux"
    fail(f"unsupported platform: {sys.platform}")
    raise RuntimeError("unreachable")


def resolve_project_root(project_root_arg: str, dev_config_path_arg: str) -> Path:
    if project_root_arg:
        project_root = Path(project_root_arg).resolve()
    else:
        default_path = TOOLS_DIR / "dev.project-root.local.json"
        config_path = Path(dev_config_path_arg).resolve() if dev_config_path_arg else default_path
        if not config_path.exists():
            fail(
                "missing --project-root and dev config not found. "
                f"expected config at {config_path}. copy tools/dev.project-root.example.json "
                "to tools/dev.project-root.local.json and set project_root."
            )
        try:
            raw = json.loads(config_path.read_text(encoding="utf-8"))
        except Exception as exc:
            fail(f"failed to read dev config {config_path}: {exc}")
        value = raw.get("project_root") if isinstance(raw, dict) else None
        if not isinstance(value, str) or not value.strip():
            fail(f"invalid dev config {config_path}: missing string field 'project_root'")
        project_root = Path(value).resolve()

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(path.is_file() and path.suffix.lower() == ".uproject" for path in project_root.iterdir()):
        fail(f"no .uproject found under: {project_root}")
    return project_root


def run(cmd: list[str], cwd: Path | None = None, allow_failure: bool = False) -> subprocess.CompletedProcess[str]:
    printable = " ".join(str(part) for part in cmd)
    print(f"[RUN]  {printable}", file=sys.stderr)
    result = subprocess.run(
        cmd,
        cwd=str(cwd or REPO_ROOT),
        text=True,
    )
    if result.returncode != 0 and not allow_failure:
        fail(f"command failed ({result.returncode}): {printable}")
    return result


def overlay_tree(source: Path, destination: Path) -> None:
    if not source.exists():
        fail(f"overlay source not found: {source}")
    destination.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        target = destination / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def resolve_ue_root(platform: str, override: str) -> Path:
    if override:
        candidate = Path(override).resolve()
    elif platform == "darwin":
        candidate = Path(os.environ.get("UE_5_7_ROOT_MAC", "/Users/Shared/Epic Games/UE_5.7")).resolve()
    elif platform == "windows":
        candidate = Path(os.environ.get("UE_5_7_ROOT_WINDOWS", r"C:\Program Files\Epic Games\UE_5.7")).resolve()
    else:
        fail("local UE editor automation is only supported on macOS and Windows")
        raise RuntimeError("unreachable")

    if not candidate.exists():
        fail(f"Unreal Engine root not found: {candidate}")
    return candidate


def resolve_run_uat(ue_root: Path, platform: str) -> Path:
    if platform == "darwin":
        run_uat = ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.sh"
    elif platform == "windows":
        run_uat = ue_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
    else:
        fail("local plugin build automation is only supported on macOS and Windows")
        raise RuntimeError("unreachable")

    if not run_uat.exists():
        fail(f"RunUAT not found: {run_uat}")
    return run_uat


def resolve_editor_binary(ue_root: Path, platform: str) -> Path:
    if platform == "darwin":
        editor = ue_root / "Engine" / "Binaries" / "Mac" / "UnrealEditor.app"
    elif platform == "windows":
        editor = ue_root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
    else:
        fail("local UE editor automation is only supported on macOS and Windows")
        raise RuntimeError("unreachable")

    if not editor.exists():
        fail(f"Unreal Editor binary not found: {editor}")
    return editor


def stop_editor(uproject: Path, platform: str) -> None:
    if platform == "darwin":
        run(["pkill", "-f", f"UnrealEditor.*{uproject}"], allow_failure=True)
    elif platform == "windows":
        command = (
            "$uproject = %s; "
            "Get-CimInstance Win32_Process -Filter \"Name = 'UnrealEditor.exe'\" | "
            "Where-Object { $_.CommandLine -like \"*$uproject*\" } | "
            "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
        ) % json.dumps(str(uproject))
        run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command], allow_failure=True)


def start_editor(editor_binary: Path, uproject: Path, platform: str) -> None:
    if platform == "darwin":
        subprocess.Popen(["open", "-na", str(editor_binary), "--args", str(uproject)])
    elif platform == "windows":
        args = [
            str(editor_binary),
            str(uproject),
            "-nullrhi",
            "-unattended",
            "-nosplash",
            "-nop4",
            "-NoSound",
        ]
        subprocess.Popen(args)


def sync_built_plugin_into_project(project_root: Path, ue_root: Path, platform: str, output_dir: Path) -> None:
    step("Build plugin binaries from current checkout and sync into project")
    plugin_descriptor = REPO_ROOT / "engine" / "LoomleBridge" / "LoomleBridge.uplugin"
    if not plugin_descriptor.exists():
        fail(f"plugin descriptor not found: {plugin_descriptor}")

    package_dir = output_dir / "plugin-package"
    if package_dir.exists():
        shutil.rmtree(package_dir)

    run_uat = resolve_run_uat(ue_root, platform)
    target_platform = "Mac" if platform == "darwin" else "Win64"
    run([str(run_uat), "BuildPlugin", f"-Plugin={plugin_descriptor}", f"-Package={package_dir}", f"-TargetPlatforms={target_platform}", "-Rocket"])

    plugin_dst = project_root / "Plugins" / "LoomleBridge"
    if not plugin_dst.exists():
        fail(f"project plugin destination not found after install: {plugin_dst}")

    for generated_dir in ["Binaries", "Intermediate"]:
        stale_dir = plugin_dst / generated_dir
        if stale_dir.exists():
            shutil.rmtree(stale_dir)

    overlay_tree(package_dir, plugin_dst)


def restart_editor(project_root: Path, ue_root: Path, platform: str) -> None:
    uproject = next(path for path in project_root.iterdir() if path.is_file() and path.suffix.lower() == ".uproject")
    editor_binary = resolve_editor_binary(ue_root, platform)
    step("Restart Unreal Editor")
    stop_editor(uproject, platform)
    time.sleep(2.0)
    start_editor(editor_binary, uproject, platform)


def run_smoke_with_retry(project_root: Path, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        result = run(
            [sys.executable, str(SMOKE_SCRIPT), "--project-root", str(project_root)],
            allow_failure=True,
        )
        if result.returncode == 0:
            return
        time.sleep(5.0)
    fail(f"timeout waiting for smoke validation to pass for {project_root}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install the current checkout into a dev UE project, then verify through the project-local Loomle entrypoint."
    )
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root. If omitted, read from tools/dev.project-root.local.json.",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json).",
    )
    parser.add_argument(
        "--ue-root",
        default="",
        help="Optional Unreal Engine root. Defaults to UE_5_7_ROOT_MAC / UE_5_7_ROOT_WINDOWS or the standard UE 5.7 install path.",
    )
    parser.add_argument(
        "--output-dir",
        default=str(REPO_ROOT / "runtime" / "dev-verify-install"),
        help="Temporary build/install output directory.",
    )
    parser.add_argument(
        "--version",
        default="0.0.0-dev",
        help="Version string written into the temporary local install state.",
    )
    parser.add_argument(
        "--wait-timeout",
        type=float,
        default=240.0,
        help="Seconds to wait for smoke validation after restart.",
    )
    parser.add_argument("--skip-regression", action="store_true", help="Skip the regression suite.")
    parser.add_argument("--run-latency", action="store_true", help="Run latency validation after smoke/regression.")
    parser.add_argument("--no-restart", action="store_true", help="Skip Unreal Editor restart after install.")
    parser.add_argument("--install-only", action="store_true", help="Only refresh the project-local install, do not run tests.")
    args = parser.parse_args()

    platform = detect_platform()
    project_root = resolve_project_root(args.project_root, args.dev_config)
    output_dir = Path(args.output_dir).resolve()
    if output_dir == REPO_ROOT or output_dir == project_root:
        fail(f"refusing to use repository root or project root as output dir: {output_dir}")
    ue_root = resolve_ue_root(platform, args.ue_root)

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)

    step("Install current checkout into the dev project")
    run(
        [
            sys.executable,
            str(INSTALL_FROM_CHECKOUT),
            "--repo-root",
            str(REPO_ROOT),
            "--project-root",
            str(project_root),
            "--output-dir",
            str(output_dir),
            "--platform",
            platform,
            "--version",
            args.version,
        ]
    )

    sync_built_plugin_into_project(project_root, ue_root, platform, output_dir)

    if args.install_only:
        print("[PASS] dev install completed", file=sys.stderr)
        return 0

    if not args.no_restart:
        restart_editor(project_root, ue_root, platform)

    step("Run smoke validation")
    run_smoke_with_retry(project_root, args.wait_timeout)

    if not args.skip_regression:
        step("Run regression validation")
        run([sys.executable, str(REGRESSION_SCRIPT), "--project-root", str(project_root)])

    if args.run_latency:
        step("Run latency validation")
        run([sys.executable, str(LATENCY_SCRIPT), "--project-root", str(project_root)])

    print("[PASS] dev verify completed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
