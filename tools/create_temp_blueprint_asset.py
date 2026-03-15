#!/usr/bin/env python3
import argparse
import json
import sys
import time
import traceback
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_execute_exec_with_retry,
    parse_execute_json,
)


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def resolve_loomle_binary(project_root: Path, override: str) -> Path:
    if override:
        candidate = Path(override).resolve()
    else:
        binary_name = "loomle.exe" if sys.platform.startswith("win") else "loomle"
        project_local = project_root / "Loomle" / binary_name
        if project_local.is_file():
            candidate = project_local
        else:
            candidate = REPO_ROOT / "mcp" / "client" / "target" / "release" / binary_name
    if not candidate.is_file():
        fail(f"loomle binary not found: {candidate}")
    return candidate


def write_github_output(path: str, asset_path: str) -> None:
    if not path:
        return
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"asset_path={asset_path}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a temporary Blueprint asset for LOOMLE benchmarks.")
    parser.add_argument("--project-root", required=True, help="UE project root containing a .uproject")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_WinIssue49Probe",
        help="Content Browser asset path prefix",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=45.0,
        help="Per-request timeout in seconds for the loomle session client",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Optional path to the loomle client binary. Defaults to <ProjectRoot>/Loomle/loomle(.exe) when present.",
    )
    parser.add_argument(
        "--github-output",
        default="",
        help="Optional GitHub Actions output file path. When set, writes asset_path=<value> to this file.",
    )
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    if not project_root.is_dir():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    asset_path = f"{args.asset_prefix}_{time.strftime('%Y%m%d_%H%M%S')}"
    loomle_binary = resolve_loomle_binary(project_root, args.loomle_bin)
    print(f"[loomle-benchmark] creating temporary Blueprint asset {asset_path}", file=sys.stderr)

    client: McpStdioClient | None = None
    try:
        client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=args.timeout)
        _ = client.request(1, "initialize", {})
        payload = parse_execute_json(
            call_execute_exec_with_retry(
                client=client,
                req_id_base=30000,
                code=(
                    "import json, unreal\n"
                    f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
                    "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                    "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                    "factory = unreal.BlueprintFactory()\n"
                    "factory.set_editor_property('ParentClass', unreal.Actor)\n"
                    "bp = unreal.EditorAssetLibrary.load_asset(asset)\n"
                    "if bp is None:\n"
                    "    bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                    "if bp is None:\n"
                    "    raise RuntimeError(f'failed to create Blueprint asset: {asset}')\n"
                    "print(json.dumps({'assetPath': asset, 'created': True}, ensure_ascii=False))\n"
                ),
                max_attempts=10,
                retry_delay_s=1.0,
            )
        )
        created_path = payload.get("assetPath")
        if not isinstance(created_path, str) or not created_path.strip():
            fail(f"execute payload missing assetPath: {payload}")
        write_github_output(args.github_output, created_path)
        print(json.dumps({"assetPath": created_path}, ensure_ascii=False))
        return 0
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 1
        print(f"[loomle-benchmark][ERROR] failed to create temporary Blueprint asset {asset_path}", file=sys.stderr)
        return code
    except Exception:
        print(f"[loomle-benchmark][ERROR] unexpected exception while creating {asset_path}", file=sys.stderr)
        traceback.print_exc()
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main())
