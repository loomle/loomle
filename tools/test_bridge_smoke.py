#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

REQUIRED_TOOLS = {
    "loomle",
    "graph",
    "graph.list",
    "graph.query",
    "graph.actions",
    "graph.mutate",
    "context",
    "execute",
}

EXPECTED_GRAPH_MUTATE_OPS = {
    "addNode.byClass",
    "addNode.byAction",
    "connectPins",
    "disconnectPins",
    "breakPinLinks",
    "setPinDefault",
    "removeNode",
    "moveNode",
    "compile",
    "runScript",
}


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


class McpStdioClient:
    def __init__(self, project_root: Path, manifest_path: Path, timeout_s: float) -> None:
        if not manifest_path.exists():
            fail(f"mcp_server manifest not found: {manifest_path}")

        env = os.environ.copy()
        env["LOOMLE_PROJECT_ROOT"] = str(project_root)

        self.proc = subprocess.Popen(
            ["cargo", "run", "-q", "--manifest-path", str(manifest_path)],
            cwd=str(project_root),
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.timeout_s = timeout_s

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()

    def request(self, req_id: int, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self.proc.stdin is None or self.proc.stdout is None:
            fail("mcp stdio is not available")

        payload = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params,
        }
        self.proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()

        deadline = time.time() + self.timeout_s
        while time.time() < deadline:
            if self.proc.poll() is not None:
                err = ""
                if self.proc.stderr is not None:
                    err = self.proc.stderr.read().strip()
                fail(f"mcp_server exited early: {err}")

            line = self.proc.stdout.readline()
            if not line:
                time.sleep(0.01)
                continue

            line = line.strip()
            if not line:
                continue

            try:
                frame = json.loads(line)
            except json.JSONDecodeError:
                continue

            if frame.get("id") != req_id:
                continue

            if "error" in frame:
                fail(f"JSON-RPC error for {method}: {frame['error']}")
            return frame

        fail(f"timeout waiting for {method} id={req_id}")


def parse_tool_payload(response: dict[str, Any], method: str) -> dict[str, Any]:
    result = response.get("result")
    if not isinstance(result, dict):
        fail(f"Invalid {method} response: missing result object")

    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        return structured

    content = result.get("content")
    if not isinstance(content, list) or not content:
        fail(f"Invalid {method} response: missing content")

    first = content[0]
    if not isinstance(first, dict):
        fail(f"Invalid {method} response: malformed content item")

    text = first.get("text")
    if not isinstance(text, str):
        fail(f"Invalid {method} response: missing text payload")

    try:
        payload = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"Invalid tool payload JSON for {method}: {exc}")

    return payload


def make_temp_asset_path(prefix: str) -> str:
    suffix = time.strftime("%Y%m%d_%H%M%S")
    return f"{prefix}_{suffix}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Loomle bridge through MCP stdio server")
    parser.add_argument("--project-root", required=True, help="UE project root, e.g. /.../UnrealProjects/Loombed")
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeVerify",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--mcp-manifest",
        default=str(Path(__file__).resolve().parents[1] / "mcp_server" / "Cargo.toml"),
        help="Path to mcp_server Cargo.toml",
    )
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    manifest_path = Path(args.mcp_manifest).resolve()

    if not project_root.exists():
        fail(f"project root not found: {project_root}")

    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, manifest_path=manifest_path, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)

    try:
        init_resp = client.request(1, "initialize", {})
        protocol_version = init_resp.get("result", {}).get("protocolVersion")
        if not protocol_version:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol_version}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        tool_names = {
            tool.get("name") for tool in tools if isinstance(tool, dict) and isinstance(tool.get("name"), str)
        }
        missing = sorted(REQUIRED_TOOLS - tool_names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print(f"[PASS] tools/list includes required baseline tools ({len(REQUIRED_TOOLS)})")

        loomle_resp = client.request(3, "tools/call", {"name": "loomle", "arguments": {}})
        loomle_payload = parse_tool_payload(loomle_resp, "tools/call.loomle")
        if loomle_payload.get("isError"):
            fail(f"loomle failed: {loomle_payload.get('message') or loomle_payload}")
        if loomle_payload.get("status") not in {"ok", "degraded"}:
            fail(f"loomle unexpected status: {loomle_payload}")
        print("[PASS] loomle status query succeeded")

        exec_resp = client.request(
            4,
            "tools/call",
            {
                "name": "execute",
                "arguments": {
                    "mode": "exec",
                    "code": "import unreal\nunreal.log('loomle execute verify')",
                },
            },
        )
        exec_payload = parse_tool_payload(exec_resp, "tools/call.execute")
        if exec_payload.get("isError"):
            fail(f"execute failed: {exec_payload.get('message') or exec_payload}")
        print("[PASS] execute channel is available")

        create_resp = client.request(
            5,
            "tools/call",
            {
                "name": "execute",
                "arguments": {
                    "mode": "exec",
                    "code": (
                        "import unreal, json\n"
                        f"asset='{temp_asset}'\n"
                        "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                        "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                        "factory = unreal.BlueprintFactory()\n"
                        "factory.set_editor_property('ParentClass', unreal.Actor)\n"
                        "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                        "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                        "print(json.dumps({'created': bp is not None, 'exists': exists}, ensure_ascii=False))\n"
                    ),
                },
            },
        )
        create_payload = parse_tool_payload(create_resp, "tools/call.execute.create")
        if create_payload.get("isError"):
            fail(f"temporary asset creation failed: {create_payload.get('message') or create_payload}")
        print(f"[PASS] temporary blueprint created: {temp_asset}")

        graph_desc_resp = client.request(
            6,
            "tools/call",
            {
                "name": "graph",
                "arguments": {"graphType": "k2"},
            },
        )
        graph_desc_payload = parse_tool_payload(graph_desc_resp, "tools/call.graph")
        if graph_desc_payload.get("isError"):
            fail(f"graph failed: {graph_desc_payload.get('message') or graph_desc_payload}")
        ops = graph_desc_payload.get("ops")
        if not isinstance(ops, list):
            fail("graph payload missing ops[]")
        ops_set = {op for op in ops if isinstance(op, str)}
        if ops_set != EXPECTED_GRAPH_MUTATE_OPS:
            fail(f"graph ops mismatch. expected={sorted(EXPECTED_GRAPH_MUTATE_OPS)} actual={sorted(ops_set)}")
        print("[PASS] graph reports expected mutate ops")

        run_script_resp = client.request(
            7,
            "tools/call",
            {
                "name": "graph.mutate",
                "arguments": {
                    "graphType": "blueprint",
                    "assetPath": temp_asset,
                    "graphName": "EventGraph",
                    "ops": [
                        {
                            "op": "runScript",
                            "args": {
                                "mode": "inlineCode",
                                "entry": "run",
                                "code": "def run(ctx):\n  return {'ok': True, 'assetPath': ctx.get('assetPath', '')}",
                                "input": {"source": "verify_bridge"},
                            },
                        }
                    ],
                },
            },
        )
        run_script_payload = parse_tool_payload(run_script_resp, "tools/call.graph.mutate")
        if run_script_payload.get("isError"):
            fail(f"graph.mutate runScript failed: {run_script_payload.get('message') or run_script_payload}")
        op_results = run_script_payload.get("opResults", [])
        if not isinstance(op_results, list) or not op_results:
            fail("graph.mutate runScript missing opResults")
        first_op = op_results[0] if isinstance(op_results[0], dict) else {}
        if not first_op.get("ok"):
            fail(f"graph.mutate runScript op failed: {first_op}")
        script_result = first_op.get("scriptResult")
        if not isinstance(script_result, dict) or script_result.get("ok") is not True:
            fail(f"graph.mutate runScript missing/invalid scriptResult: {first_op}")
        print("[PASS] graph.mutate runScript inline execution verified")

        print("[PASS] Bridge verification complete")
        return 0
    finally:
        try:
            _ = client.request(
                99,
                "tools/call",
                {
                    "name": "execute",
                    "arguments": {
                        "mode": "exec",
                        "code": (
                            "import unreal\n"
                            f"asset='{temp_asset}'\n"
                            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                            "  unreal.EditorAssetLibrary.delete_asset(asset)\n"
                        ),
                    },
                },
            )
        except Exception:
            pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
