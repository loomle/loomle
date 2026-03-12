#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from test_bridge_smoke import (
    McpStdioClient,
    call_execute_exec_with_retry,
    fail,
    resolve_default_server_binary,
    resolve_project_root,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="Cleanup Loomle bridge test assets in Unreal project content.")
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root. If omitted, read from tools/dev.project-root.local.json",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json)",
    )
    parser.add_argument("--timeout", type=float, default=20.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--mcp-server-bin",
        default="",
        help="Override path to MCP server binary. Defaults to <project>/Plugins/LoomleBridge/Tools/mcp/<platform>/...",
    )
    parser.add_argument(
        "--scan-root",
        default="/Game/Codex",
        help="Unreal content root to scan recursively (default: /Game/Codex)",
    )
    parser.add_argument(
        "--prefix",
        action="append",
        default=[],
        help="Asset name prefix to delete. Can be repeated. Defaults: BP_BridgeVerify_, BP_BridgeRegression_",
    )
    parser.add_argument("--dry-run", action="store_true", help="Only list matched assets, do not delete")
    args = parser.parse_args()

    prefixes = args.prefix or ["BP_BridgeVerify_", "BP_BridgeRegression_"]
    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.mcp_server_bin).resolve() if args.mcp_server_bin else resolve_default_server_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    try:
        code = (
            "import json\n"
            "import unreal\n"
            f"scan_root = {json.dumps(args.scan_root, ensure_ascii=False)}\n"
            f"prefixes = {json.dumps(prefixes, ensure_ascii=False)}\n"
            f"dry_run = {repr(bool(args.dry_run))}\n"
            "registry = unreal.AssetRegistryHelpers.get_asset_registry()\n"
            "assets = registry.get_assets_by_path(unreal.Name(scan_root), recursive=True)\n"
            "matched = []\n"
            "for data in assets:\n"
            "    package_name = str(data.package_name)\n"
            "    asset_name = str(data.asset_name)\n"
            "    if any(asset_name.startswith(prefix) for prefix in prefixes):\n"
            "        matched.append('/' + package_name)\n"
            "deleted = []\n"
            "failed = []\n"
            "if not dry_run:\n"
            "    for asset_path in matched:\n"
            "        ok = unreal.EditorAssetLibrary.delete_asset(asset_path)\n"
            "        if ok:\n"
            "            deleted.append(asset_path)\n"
            "        else:\n"
            "            failed.append(asset_path)\n"
            "result = {\n"
            "    'scanRoot': scan_root,\n"
            "    'prefixes': prefixes,\n"
            "    'dryRun': dry_run,\n"
            "    'matchedCount': len(matched),\n"
            "    'deletedCount': len(deleted),\n"
            "    'failedCount': len(failed),\n"
            "    'matched': matched,\n"
            "    'deleted': deleted,\n"
            "    'failed': failed,\n"
            "}\n"
            "print(json.dumps(result, ensure_ascii=False))\n"
        )
        payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=7000,
            code=code,
            max_attempts=30,
            retry_delay_s=1.0,
        )
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
