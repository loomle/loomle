#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from test_bridge_smoke import (
    EXPECTED_GRAPH_MUTATE_OPS,
    REQUIRED_TOOLS,
    McpStdioClient,
    call_tool,
    fail,
    make_temp_asset_path,
    resolve_project_root,
    resolve_default_server_binary,
)


def op_ok(payload: dict) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        fail(f"graph.mutate missing opResults: {payload}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    if not first.get("ok"):
        fail(f"graph.mutate op failed: {first}")
    return first


def main() -> int:
    parser = argparse.ArgumentParser(description="Deep regression validation for Loomle bridge through MCP stdio")
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root, e.g. /Users/xartest/dev/LoomleDevHost. If omitted, read from tools/dev.project-root.local.json",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json)",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeRegression",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--mcp-server-bin",
        default="",
        help="Override path to MCP server binary. Defaults to <project>/Plugins/LoomleBridge/Tools/mcp/<platform>/...",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.mcp_server_bin).resolve() if args.mcp_server_bin else resolve_default_server_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)

    try:
        init = client.request(1, "initialize", {})
        protocol = init.get("result", {}).get("protocolVersion")
        if not protocol:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        names = {t.get("name") for t in tools if isinstance(t, dict) and isinstance(t.get("name"), str)}
        missing = sorted(REQUIRED_TOOLS - names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print("[PASS] tools/list baseline tools available")

        graph_desc = call_tool(client, 3, "graph", {"graphType": "k2"})
        ops = graph_desc.get("ops")
        if not isinstance(ops, list):
            fail("graph descriptor missing ops[]")
        if {x for x in ops if isinstance(x, str)} != EXPECTED_GRAPH_MUTATE_OPS:
            fail("graph descriptor ops mismatch")
        print("[PASS] graph descriptor validated")

        create_payload = call_tool(
            client,
            4,
            "execute",
            {
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
        )
        _ = create_payload
        print(f"[PASS] temporary asset created: {temp_asset}")

        graph_list = call_tool(client, 5, "graph.list", {"assetPath": temp_asset, "graphType": "blueprint"})
        graphs = graph_list.get("graphs")
        if not isinstance(graphs, list):
            fail(f"graph.list missing graphs[]: {graph_list}")
        if not any(isinstance(g, dict) and g.get("graphName") == "EventGraph" for g in graphs):
            fail(f"graph.list did not include EventGraph: {graph_list}")
        print("[PASS] graph.list validated")

        graph_query = call_tool(
            client,
            6,
            "graph.query",
            {"assetPath": temp_asset, "graphName": "EventGraph", "graphType": "blueprint", "limit": 200},
        )
        snapshot = graph_query.get("semanticSnapshot")
        if not isinstance(snapshot, dict):
            fail(f"graph.query missing semanticSnapshot: {graph_query}")
        if not isinstance(snapshot.get("nodes"), list) or not isinstance(snapshot.get("edges"), list):
            fail(f"graph.query invalid semanticSnapshot shape: {snapshot}")
        print("[PASS] graph.query structure validated")

        actions = call_tool(
            client,
            7,
            "graph.actions",
            {"assetPath": temp_asset, "graphName": "EventGraph", "graphType": "blueprint", "limit": 20},
        )
        items = actions.get("items")
        if not isinstance(items, list):
            items = actions.get("actions")
        if not isinstance(items, list):
            fail(f"graph.actions missing actions/items[]: {actions}")
        print("[PASS] graph.actions response validated")

        bad_query = call_tool(
            client,
            8,
            "graph.query",
            {"assetPath": temp_asset, "graphType": "blueprint"},
            expect_error=True,
        )
        _ = bad_query
        print("[PASS] graph.query error path validated")

        add_a = call_tool(
            client,
            10,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 0, "y": 0},
                        },
                    }
                ],
            },
        )
        node_a = op_ok(add_a).get("nodeId")
        if not isinstance(node_a, str) or not node_a:
            fail(f"addNode.byClass did not return nodeId: {add_a}")

        add_b = call_tool(
            client,
            11,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 320, "y": 0},
                        },
                    }
                ],
            },
        )
        node_b = op_ok(add_b).get("nodeId")
        if not isinstance(node_b, str) or not node_b:
            fail(f"addNode.byClass did not return nodeId for second node: {add_b}")
        print("[PASS] graph.mutate addNode.byClass validated")

        connect_payload = call_tool(
            client,
            12,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {"nodeId": node_a, "pin": "then"},
                            "to": {"nodeId": node_b, "pin": "execute"},
                        },
                    }
                ],
            },
        )
        op_ok(connect_payload)

        break_payload = call_tool(
            client,
            13,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "args": {
                            "target": {"nodeId": node_a, "pin": "then"},
                        },
                    }
                ],
            },
        )
        op_ok(break_payload)

        reconnect_payload = call_tool(
            client,
            14,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {"nodeId": node_a, "pin": "then"},
                            "to": {"nodeId": node_b, "pin": "execute"},
                        },
                    }
                ],
            },
        )
        op_ok(reconnect_payload)

        disconnect_payload = call_tool(
            client,
            15,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "disconnectPins",
                        "args": {
                            "from": {"nodeId": node_a, "pin": "then"},
                            "to": {"nodeId": node_b, "pin": "execute"},
                        },
                    }
                ],
            },
        )
        op_ok(disconnect_payload)

        set_default_payload = call_tool(
            client,
            16,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": node_b, "pin": "Condition"},
                            "value": "true",
                        },
                    }
                ],
            },
        )
        op_ok(set_default_payload)

        move_payload = call_tool(
            client,
            17,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "moveNode",
                        "args": {
                            "target": {"nodeId": node_b},
                            "position": {"x": 640, "y": 120},
                        },
                    }
                ],
            },
        )
        op_ok(move_payload)

        compile_payload = call_tool(
            client,
            18,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {"op": "compile"},
                ],
            },
        )
        op_ok(compile_payload)

        remove_a = call_tool(
            client,
            19,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "args": {"target": {"nodeId": node_a}},
                    }
                ],
            },
        )
        op_ok(remove_a)

        remove_b = call_tool(
            client,
            20,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "args": {"target": {"nodeId": node_b}},
                    }
                ],
            },
        )
        op_ok(remove_b)

        print("[PASS] graph.mutate core ops validated")
        print("[PASS] Bridge regression complete")
        return 0
    finally:
        try:
            _ = call_tool(
                client,
                99,
                "execute",
                {
                    "mode": "exec",
                    "code": (
                        "import unreal\n"
                        f"asset='{temp_asset}'\n"
                        "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                        "  unreal.EditorAssetLibrary.delete_asset(asset)\n"
                    ),
                },
            )
        except Exception:
            pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
