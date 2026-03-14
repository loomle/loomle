#!/usr/bin/env python3
import argparse
import json
import time
from pathlib import Path

from test_bridge_smoke import (
    EXPECTED_GRAPH_MUTATE_OPS,
    REQUIRED_TOOLS,
    McpStdioClient,
    call_execute_exec_with_retry,
    call_tool,
    fail,
    make_temp_asset_path,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_project_root,
)


def op_ok(payload: dict) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        fail(f"graph.mutate missing opResults: {payload}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    if not first.get("ok"):
        fail(f"graph.mutate op failed: {first}")
    return first


def query_nodes(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_name: str,
    max_attempts: int = 3,
    retry_delay_s: float = 1.0,
) -> list[dict]:
    payload: dict | None = None
    for attempt in range(1, max_attempts + 1):
        try:
            payload = call_tool(
                client,
                request_id,
                "graph.query",
                {"assetPath": asset_path, "graphName": graph_name, "graphType": "blueprint", "limit": 200},
            )
            break
        except SystemExit:
            if attempt >= max_attempts:
                raise
            print(f"[WARN] graph.query retrying after failure ({attempt}/{max_attempts})...")
            time.sleep(retry_delay_s)

    if payload is None:
        fail("graph.query retry loop ended without payload")

    snapshot = payload.get("semanticSnapshot", {})
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        fail(f"graph.query missing nodes[]: {payload}")
    return [node for node in nodes if isinstance(node, dict)]


def query_snapshot(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_type: str,
    graph_name: str,
) -> dict:
    payload = call_tool(
        client,
        request_id,
        "graph.query",
        {"assetPath": asset_path, "graphName": graph_name, "graphType": graph_type, "limit": 200},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        fail(f"graph.query missing semanticSnapshot for {graph_type}: {payload}")
    return snapshot


def require_node(nodes: list[dict], node_id: str) -> dict:
    for node in nodes:
        if node.get("id") == node_id:
            return node
    fail(f"graph.query did not return node {node_id}: {nodes}")


def require_node_absent(nodes: list[dict], node_id: str) -> None:
    for node in nodes:
        if node.get("id") == node_id:
            fail(f"graph.query still returned removed node {node_id}: {node}")


def require_layout(node: dict) -> dict:
    layout = node.get("layout")
    if not isinstance(layout, dict):
        fail(f"graph.query node missing layout object: {node}")
    position = layout.get("position")
    if not isinstance(position, dict):
        fail(f"graph.query node layout missing position: {node}")
    if not isinstance(position.get("x"), (int, float)) or not isinstance(position.get("y"), (int, float)):
        fail(f"graph.query node layout position invalid: {node}")
    if not isinstance(layout.get("source"), str) or not layout.get("source"):
        fail(f"graph.query node layout missing source: {node}")
    if not isinstance(layout.get("reliable"), bool):
        fail(f"graph.query node layout missing reliable flag: {node}")
    if not isinstance(layout.get("sizeSource"), str) or not isinstance(layout.get("boundsSource"), str):
        fail(f"graph.query node layout missing source metadata: {node}")
    return layout


def wait_for_bridge_ready(client: McpStdioClient, timeout_s: float = 120.0, interval_s: float = 2.0) -> None:
    deadline = time.time() + timeout_s
    attempt = 0
    while time.time() < deadline:
        attempt += 1
        try:
            loomle = call_tool(client, 9000 + attempt, "loomle", {})
            status = loomle.get("status")
            rpc_health = loomle.get("runtime", {}).get("rpcHealth", {})
            if status not in {"ok", "degraded"} or rpc_health.get("status") not in {"ok", "degraded"}:
                print(f"[WARN] bridge not ready yet (attempt {attempt}): status={status}, rpc={rpc_health}")
                time.sleep(interval_s)
                continue

            _ = call_execute_exec_with_retry(
                client=client,
                req_id_base=9500 + (attempt * 10),
                code="import unreal\nunreal.log('loomle regression warmup')",
                max_attempts=10,
                retry_delay_s=1.0,
            )
            print(f"[PASS] bridge ready after {attempt} attempt(s)")
            return
        except BaseException as exc:
            print(f"[WARN] bridge readiness probe failed (attempt {attempt}): {exc}")
            time.sleep(interval_s)

    fail(f"bridge did not become ready within {timeout_s:.0f}s")


def require_resolved_asset_path(payload: dict, expected_asset_path: str) -> dict:
    refs = payload.get("resolvedGraphRefs")
    if not isinstance(refs, list) or not refs:
        fail(f"graph.resolve missing resolvedGraphRefs: {payload}")

    for entry in refs:
        if not isinstance(entry, dict):
            continue
        graph_ref = entry.get("graphRef")
        if not isinstance(graph_ref, dict):
            continue
        if graph_ref.get("assetPath") == expected_asset_path:
            return entry

    fail(f"graph.resolve did not include expected assetPath {expected_asset_path}: {payload}")
    raise RuntimeError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser(description="Deep regression validation for LOOMLE bridge through loomle session")
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
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeRegression",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Override path to the loomle client binary. Defaults to <repo>/mcp/client/target/release/loomle(.exe).",
    )
    parser.add_argument(
        "--mcp-server-bin",
        dest="loomle_bin_compat",
        default="",
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.loomle_bin).resolve()
        if args.loomle_bin
        else Path(args.loomle_bin_compat).resolve()
        if args.loomle_bin_compat
        else resolve_default_loomle_binary()
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)
    temp_pcg_asset = make_temp_asset_path("/Game/Codex/PCG_BridgeRegression")
    temp_material_asset = make_temp_asset_path("/Game/Codex/M_RegressionLayout")

    try:
        wait_for_bridge_ready(client)

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
        event_graph = next(
            (g for g in graphs if isinstance(g, dict) and g.get("graphName") == "EventGraph"),
            None,
        )
        if not isinstance(event_graph, dict):
            fail(f"graph.list event graph entry missing: {graph_list}")
        event_graph_ref = event_graph.get("graphRef")
        if not isinstance(event_graph_ref, dict) or event_graph_ref.get("kind") != "asset":
            fail(f"graph.list event graph graphRef missing/invalid: {event_graph}")
        print("[PASS] graph.list validated")

        graph_query = call_tool(
            client,
            6,
            "graph.query",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "limit": 200,
                "layoutDetail": "measured",
            },
        )
        snapshot = graph_query.get("semanticSnapshot")
        if not isinstance(snapshot, dict):
            fail(f"graph.query missing semanticSnapshot: {graph_query}")
        if not isinstance(snapshot.get("nodes"), list) or not isinstance(snapshot.get("edges"), list):
            fail(f"graph.query invalid semanticSnapshot shape: {snapshot}")
        query_meta = graph_query.get("meta")
        if not isinstance(query_meta, dict):
            fail(f"graph.query missing meta: {graph_query}")
        layout_caps = query_meta.get("layoutCapabilities")
        if not isinstance(layout_caps, dict):
            fail(f"graph.query missing layoutCapabilities: {graph_query}")
        if layout_caps.get("canReadPosition") is not True:
            fail(f"graph.query layoutCapabilities missing canReadPosition=true: {query_meta}")
        if query_meta.get("layoutDetailRequested") != "measured":
            fail(f"graph.query layoutDetailRequested mismatch: {query_meta}")
        if query_meta.get("layoutDetailApplied") != "basic":
            fail(f"graph.query layoutDetailApplied mismatch: {query_meta}")
        query_diagnostics = graph_query.get("diagnostics")
        if not isinstance(query_diagnostics, list):
            fail(f"graph.query diagnostics missing or invalid: {graph_query}")
        if not any(isinstance(d, dict) and d.get("code") == "LAYOUT_DETAIL_DOWNGRADED" for d in query_diagnostics):
            fail(f"graph.query missing LAYOUT_DETAIL_DOWNGRADED diagnostic: {graph_query}")
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
        if len(items) == 0:
            fail(f"graph.actions returned empty actions list: {actions}")

        for idx, item in enumerate(items):
            if not isinstance(item, dict):
                fail(f"graph.actions item[{idx}] is not a dict: {item}")
            action_token = item.get("actionToken")
            if not isinstance(action_token, str) or not action_token.startswith("act:"):
                fail(f"graph.actions item[{idx}] missing valid actionToken: {item}")

        if not isinstance(actions.get("graphType"), str):
            fail(f"graph.actions missing graphType echo: {actions}")
        if not isinstance(actions.get("assetPath"), str):
            fail(f"graph.actions missing assetPath echo: {actions}")
        if not isinstance(actions.get("graphName"), str):
            fail(f"graph.actions missing graphName echo: {actions}")
        meta = actions.get("meta")
        if not isinstance(meta, dict) or "total" not in meta or "returned" not in meta:
            fail(f"graph.actions missing or invalid meta: {actions}")
        action_source = meta.get("actionSource")
        if action_source not in {"typed", "generic_fallback", "curated_catalog"}:
            fail(f"graph.actions meta missing valid actionSource: {meta}")
        diagnostics = actions.get("diagnostics")
        if not isinstance(diagnostics, list):
            fail(f"graph.actions diagnostics missing or invalid: {actions}")
        if any(isinstance(d, dict) and d.get("code") == "ADDABLE_FALLBACK_USED" for d in diagnostics):
            if not isinstance(meta.get("fallbackReason"), str) or not meta.get("fallbackReason"):
                fail(f"graph.actions fallback meta missing fallbackReason: {meta}")
            if not isinstance(meta.get("recommendedRecovery"), str) or not meta.get("recommendedRecovery"):
                fail(f"graph.actions fallback meta missing recommendedRecovery: {meta}")
        print(f"[PASS] graph.actions response validated ({len(items)} actions returned)")

        actions_by_ref = call_tool(
            client,
            7001,
            "graph.actions",
            {"graphRef": event_graph_ref, "graphType": "blueprint", "limit": 5},
        )
        by_ref_items = actions_by_ref.get("items")
        if not isinstance(by_ref_items, list):
            by_ref_items = actions_by_ref.get("actions")
        if not isinstance(by_ref_items, list) or len(by_ref_items) == 0:
            fail(f"graph.actions(graphRef) returned no actions/items: {actions_by_ref}")
        by_ref_echo = actions_by_ref.get("graphRef")
        if not isinstance(by_ref_echo, dict) or by_ref_echo.get("kind") != "asset":
            fail(f"graph.actions(graphRef) missing graphRef echo: {actions_by_ref}")
        print("[PASS] graph.actions graphRef(asset) addressing validated")

        pcg_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=7050,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG graph asset')\n"
                "volume = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PCGVolume, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))\n"
                "if volume is None:\n"
                "    raise RuntimeError('failed to spawn PCGVolume')\n"
                "component = volume.get_editor_property('pcg_component')\n"
                "if component is None:\n"
                "    raise RuntimeError('spawned PCGVolume has no pcg_component')\n"
                "component.set_graph(graph)\n"
                "unreal.EditorLevelLibrary.set_selected_level_actors([volume])\n"
                "result = {\n"
                "    'assetPath': asset,\n"
                "    'actorPath': volume.get_path_name(),\n"
                "    'componentPath': component.get_path_name(),\n"
                "}\n"
                "print(json.dumps(result, ensure_ascii=False))\n"
            ),
        )
        pcg_fixture = parse_execute_json(pcg_fixture_payload)
        pcg_actor_path = pcg_fixture.get("actorPath")
        pcg_component_path = pcg_fixture.get("componentPath")
        if not isinstance(pcg_actor_path, str) or not pcg_actor_path:
            fail(f"PCG fixture missing actorPath: {pcg_fixture}")
        if not isinstance(pcg_component_path, str) or not pcg_component_path:
            fail(f"PCG fixture missing componentPath: {pcg_fixture}")
        print("[PASS] temporary PCG fixture created")

        pcg_context = call_tool(client, 7056, "context", {})
        selection = pcg_context.get("selection")
        if not isinstance(selection, dict):
            fail(f"context missing selection after PCG fixture setup: {pcg_context}")
        selected_pcg_entry = require_resolved_asset_path(selection, temp_pcg_asset)
        if selected_pcg_entry.get("graphType") != "pcg":
            fail(f"context selection resolved wrong graphType for PCG fixture: {selected_pcg_entry}")
        print("[PASS] context selection exposes resolvedGraphRefs for selected PCG actor")

        resolved_from_actor = call_tool(
            client,
            7057,
            "graph.resolve",
            {"actorPath": pcg_actor_path, "graphType": "pcg"},
        )
        actor_entry = require_resolved_asset_path(resolved_from_actor, temp_pcg_asset)
        if actor_entry.get("graphType") != "pcg":
            fail(f"graph.resolve(actorPath) returned wrong graphType: {actor_entry}")

        resolved_from_component = call_tool(
            client,
            7058,
            "graph.resolve",
            {"componentPath": pcg_component_path, "graphType": "pcg"},
        )
        component_entry = require_resolved_asset_path(resolved_from_component, temp_pcg_asset)
        if component_entry.get("graphType") != "pcg":
            fail(f"graph.resolve(componentPath) returned wrong graphType: {component_entry}")

        graph_ref = actor_entry.get("graphRef")
        if not isinstance(graph_ref, dict):
            fail(f"graph.resolve(actorPath) missing graphRef object: {actor_entry}")

        queried_pcg = call_tool(
            client,
            7059,
            "graph.query",
            {"graphType": "pcg", "graphRef": graph_ref, "limit": 200},
        )
        queried_graph_ref = queried_pcg.get("graphRef")
        if not isinstance(queried_graph_ref, dict) or queried_graph_ref.get("assetPath") != temp_pcg_asset:
            fail(f"graph.query(graphRef) did not echo expected PCG graphRef: {queried_pcg}")
        print("[PASS] graph.resolve PCG actor/component addressing validated")

        first_token = items[0].get("actionToken", "")
        add_by_action = call_tool(
            client,
            9007,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byAction",
                        "args": {
                            "actionToken": first_token,
                            "position": {"x": 1200, "y": 0},
                        },
                    }
                ],
            },
        )
        by_action_result = op_ok(add_by_action)
        by_action_node = by_action_result.get("nodeId")
        if not isinstance(by_action_node, str) or not by_action_node:
            fail(f"addNode.byAction did not return nodeId: {add_by_action}")
        print(f"[PASS] graph.actions addNode.byAction via actionToken succeeded (nodeId={by_action_node})")

        call_tool(
            client,
            9008,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [{"op": "removeNode", "args": {"target": {"nodeId": by_action_node}}}],
            },
        )

        bad_actions = call_tool(
            client,
            9009,
            "graph.actions",
            {"assetPath": temp_asset, "graphName": "EventGraph", "graphType": "notreal", "limit": 5},
            expect_error=True,
        )
        _ = bad_actions
        print("[PASS] graph.actions error path validated (unsupported graphType)")

        stale_token_mutate = call_tool(
            client,
            9010,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byAction",
                        "args": {"actionToken": "act:blueprint:00000000-0000-0000-0000-000000000000", "position": {"x": 0, "y": 0}},
                    }
                ],
            },
            expect_error=True,
        )
        stale_message = str(stale_token_mutate.get("message", ""))
        stale_detail = str(stale_token_mutate.get("detail", ""))
        if "ACTION_TOKEN_INVALID" not in (stale_message + " " + stale_detail):
            fail(f"stale actionToken error missing ACTION_TOKEN_INVALID marker: {stale_token_mutate}")
        print("[PASS] graph.actions stale actionToken correctly rejected")

        bad_query = call_tool(
            client,
            8,
            "graph.query",
            {"assetPath": temp_asset, "graphType": "blueprint"},
            expect_error=True,
        )
        _ = bad_query
        print("[PASS] graph.query error path validated")

        bad_remove = call_tool(
            client,
            8008,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [{"op": "removeNode", "args": {"target": {}}}],
            },
            expect_error=True,
        )
        bad_remove_detail = bad_remove.get("detail")
        if not isinstance(bad_remove_detail, str):
            fail(f"graph.mutate error payload missing detail JSON: {bad_remove}")
        try:
            bad_remove_struct = json.loads(bad_remove_detail)
        except Exception as exc:
            fail(f"graph.mutate error detail is not valid JSON: {exc} payload={bad_remove}")
        bad_remove_results = bad_remove_struct.get("opResults")
        if not isinstance(bad_remove_results, list) or not bad_remove_results:
            fail(f"graph.mutate error detail missing opResults: {bad_remove_struct}")
        first_bad_remove = bad_remove_results[0] if isinstance(bad_remove_results[0], dict) else {}
        error_code = first_bad_remove.get("errorCode")
        error_message = first_bad_remove.get("errorMessage")
        if not isinstance(error_code, str) or not error_code:
            fail(f"graph.mutate opResults[0] missing errorCode: {bad_remove_struct}")
        if not isinstance(error_message, str) or not error_message:
            fail(f"graph.mutate opResults[0] missing errorMessage: {bad_remove_struct}")
        print("[PASS] graph.mutate structured op error fields validated")

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

        bad_set_default_payload = call_tool(
            client,
            17,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": node_b, "pinName": "DefinitelyMissingPin"},
                            "value": "true",
                        },
                    }
                ],
            },
            expect_error=True,
        )
        bad_set_default_results = bad_set_default_payload.get("opResults")
        if not isinstance(bad_set_default_results, list):
            bad_set_default_detail = bad_set_default_payload.get("detail")
            if isinstance(bad_set_default_detail, str) and bad_set_default_detail:
                try:
                    bad_set_default_struct = json.loads(bad_set_default_detail)
                except Exception as exc:
                    fail(f"graph.mutate bad setPinDefault detail is not valid JSON: {exc} payload={bad_set_default_payload}")
                bad_set_default_results = bad_set_default_struct.get("opResults")
        if not isinstance(bad_set_default_results, list) or not bad_set_default_results:
            fail(f"graph.mutate bad setPinDefault missing opResults: {bad_set_default_payload}")
        bad_set_default_first = bad_set_default_results[0] if isinstance(bad_set_default_results[0], dict) else {}
        if bad_set_default_first.get("errorCode") not in {"TARGET_NOT_FOUND", "INVALID_ARGUMENT"}:
            fail(f"graph.mutate bad setPinDefault wrong errorCode: {bad_set_default_first}")
        details = bad_set_default_first.get("details")
        if not isinstance(details, dict):
            fail(f"graph.mutate bad setPinDefault missing details object: {bad_set_default_first}")
        expected_target_forms = details.get("expectedTargetForms")
        if not isinstance(expected_target_forms, list) or not expected_target_forms:
            fail(f"graph.mutate bad setPinDefault missing expectedTargetForms: {details}")
        candidate_pins = details.get("candidatePins")
        if not isinstance(candidate_pins, list) or not candidate_pins:
            fail(f"graph.mutate bad setPinDefault missing candidatePins: {details}")
        if not any(isinstance(pin, dict) and pin.get("pinName") == "Condition" for pin in candidate_pins):
            fail(f"graph.mutate bad setPinDefault candidatePins missing Condition: {candidate_pins}")
        print("[PASS] graph.mutate setPinDefault diagnostics validated")

        move_payload = call_tool(
            client,
            18,
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

        move_by_payload = call_tool(
            client,
            1801,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "args": {
                            "target": {"nodeId": node_b},
                            "dx": 16,
                            "dy": 32,
                        },
                    }
                ],
            },
        )
        op_ok(move_by_payload)

        move_nodes_payload = call_tool(
            client,
            1802,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "moveNodes",
                        "args": {
                            "nodeIds": [node_a, node_b],
                            "delta": {"x": 16, "y": 0},
                        },
                    }
                ],
            },
        )
        move_nodes_first = op_ok(move_nodes_payload)
        if move_nodes_first.get("op") != "movenodes":
            fail(f"graph.mutate moveNodes wrong op echo: {move_nodes_first}")

        compile_payload = call_tool(
            client,
            19,
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

        nodes_before_remove = query_nodes(client, 20, temp_asset, "EventGraph")
        node_a_info = require_node(nodes_before_remove, node_a)
        node_b_info = require_node(nodes_before_remove, node_b)
        node_a_layout = require_layout(node_a_info)
        node_b_layout = require_layout(node_b_info)
        node_a_path = node_a_info.get("path")
        node_b_name = node_b_info.get("name")
        if not isinstance(node_a_path, str) or not node_a_path:
            fail(f"graph.query did not return path for {node_a}: {node_a_info}")
        if not isinstance(node_b_name, str) or not node_b_name:
            fail(f"graph.query did not return name for {node_b}: {node_b_info}")
        if node_a_layout.get("position", {}).get("x") != 16 or node_a_layout.get("position", {}).get("y") != 0:
            fail(f"graph.mutate moveNodes did not update node_a layout as expected: {node_a_info}")
        node_b_pos = node_b_layout.get("position", {})
        if node_b_pos.get("x") != 672 or node_b_pos.get("y") not in {144, 160}:
            fail(f"graph.mutate moveNode/moveNodeBy/moveNodes did not update node_b layout as expected: {node_b_info}")

        reconnect_for_layout_payload = call_tool(
            client,
            1803,
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
        op_ok(reconnect_for_layout_payload)

        layout_payload = call_tool(
            client,
            1804,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [{"op": "layoutGraph", "args": {"scope": "touched"}}],
            },
        )
        layout_first = op_ok(layout_payload)
        if layout_first.get("op") != "layoutgraph":
            fail(f"graph.mutate layoutGraph wrong op echo: {layout_first}")
        moved_node_ids = layout_first.get("movedNodeIds")
        if not isinstance(moved_node_ids, list):
            fail(f"graph.mutate layoutGraph missing movedNodeIds: {layout_first}")
        if not moved_node_ids:
            fail(f"graph.mutate layoutGraph moved no nodes after touched edits: {layout_first}")

        nodes_after_layout = query_nodes(client, 1805, temp_asset, "EventGraph")
        node_a_after_layout = require_layout(require_node(nodes_after_layout, node_a))
        node_b_after_layout = require_layout(require_node(nodes_after_layout, node_b))
        if (
            node_a_after_layout.get("position") == node_a_layout.get("position")
            and node_b_after_layout.get("position") == node_b_layout.get("position")
        ):
            fail(
                "graph.mutate layoutGraph did not change any tracked node positions: "
                f"nodeA={node_a_after_layout} nodeB={node_b_after_layout}"
            )
        print("[PASS] graph.mutate layoutGraph touched scope validated")

        add_without_position = call_tool(
            client,
            1806,
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
                        },
                    }
                ],
            },
        )
        node_e = op_ok(add_without_position).get("nodeId")
        if not isinstance(node_e, str) or not node_e:
            fail(f"addNode.byClass without position did not return nodeId: {add_without_position}")
        nodes_after_auto_place = query_nodes(client, 1807, temp_asset, "EventGraph")
        node_e_info = require_node(nodes_after_auto_place, node_e)
        node_e_layout = require_layout(node_e_info)
        node_e_pos = node_e_layout.get("position", {})
        if node_e_pos.get("x") == 0 and node_e_pos.get("y") == 0:
            fail(f"addNode.byClass without position still defaulted to origin: {node_e_info}")
        remove_e = call_tool(
            client,
            1808,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [{"op": "removeNode", "args": {"target": {"nodeId": node_e}}}],
            },
        )
        op_ok(remove_e)
        print("[PASS] graph.mutate addNode.byClass auto-placement validated")

        remove_a = call_tool(
            client,
            21,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "args": {"target": {"nodePath": node_a_path}},
                    }
                ],
            },
        )
        op_ok(remove_a)
        nodes_after_remove_a = query_nodes(client, 22, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_a, node_a)

        remove_b = call_tool(
            client,
            23,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "args": {"target": {"nodeName": node_b_name}},
                    }
                ],
            },
        )
        op_ok(remove_b)
        nodes_after_remove_b = query_nodes(client, 24, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_b, node_b)

        add_c = call_tool(
            client,
            25,
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
                            "position": {"x": 960, "y": 0},
                        },
                    }
                ],
            },
        )
        node_c = op_ok(add_c).get("nodeId")
        if not isinstance(node_c, str) or not node_c:
            fail(f"addNode.byClass did not return nodeId for third node: {add_c}")

        remove_c = call_tool(
            client,
            26,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "args": {"target": {"nodeId": node_c}},
                    }
                ],
            },
        )
        op_ok(remove_c)
        nodes_after_remove_c = query_nodes(client, 27, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_c, node_c)

        add_via_graph_ref = call_tool(
            client,
            28,
            "graph.mutate",
            {
                "graphRef": {"kind": "asset", "assetPath": temp_asset, "graphName": "EventGraph"},
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 1280, "y": 0},
                        },
                    }
                ],
            },
        )
        node_d = op_ok(add_via_graph_ref).get("nodeId")
        if not isinstance(node_d, str) or not node_d:
            fail(f"graphRef(asset) mutate addNode.byClass did not return nodeId: {add_via_graph_ref}")

        remove_via_target_graph_ref = call_tool(
            client,
            29,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "removeNode",
                        "targetGraphRef": {"kind": "asset", "assetPath": temp_asset, "graphName": "EventGraph"},
                        "args": {"target": {"nodeId": node_d}},
                    }
                ],
            },
        )
        op_ok(remove_via_target_graph_ref)
        nodes_after_remove_d = query_nodes(client, 30, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_d, node_d)

        print("[PASS] graph.mutate removeNode validated for nodeId/nodePath/nodeName")
        print("[PASS] graph.mutate graphRef(asset) and targetGraphRef(asset) validated")

        material_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=10000,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_material_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.MaterialFactoryNew()\n"
                "material = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if material is None:\n"
                "    material = asset_tools.create_asset(asset_name, pkg_path, unreal.Material, factory)\n"
                "if material is None:\n"
                "    raise RuntimeError('failed to create material asset')\n"
                "unreal.EditorAssetLibrary.save_loaded_asset(material)\n"
                "print(json.dumps({'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        material_fixture = parse_execute_json(material_fixture_payload)
        material_asset_path = material_fixture.get("assetPath")
        if not isinstance(material_asset_path, str) or not material_asset_path:
            fail(f"Material fixture missing assetPath: {material_fixture}")
        print("[PASS] temporary material fixture created")

        material_add = call_tool(
            client,
            10010,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionConstant"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"}},
                ],
            },
        )
        material_add_results = material_add.get("opResults")
        if not isinstance(material_add_results, list) or len(material_add_results) != 3:
            fail(f"Material fixture add ops missing results: {material_add}")
        material_param_id = material_add_results[0].get("nodeId")
        material_constant_id = material_add_results[1].get("nodeId")
        material_multiply_id = material_add_results[2].get("nodeId")
        if not all(isinstance(node_id, str) and node_id for node_id in [material_param_id, material_constant_id, material_multiply_id]):
            fail(f"Material fixture add ops missing node ids: {material_add}")

        material_connect = call_tool(
            client,
            10011,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {"from": {"nodeId": material_param_id}, "to": {"nodeId": material_multiply_id, "pin": "A"}},
                    },
                    {
                        "op": "connectPins",
                        "args": {"from": {"nodeId": material_constant_id}, "to": {"nodeId": material_multiply_id, "pin": "B"}},
                    },
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {"nodeId": material_multiply_id},
                            "to": {"nodeId": "__material_root__", "pin": "Base Color"},
                        },
                    },
                    {"op": "layoutGraph", "args": {"scope": "touched"}},
                    {"op": "compile"},
                ],
            },
        )
        material_layout_result = op_ok(material_connect)
        if material_layout_result.get("op") != "connectpins":
            fail(f"Material connectPins wrong op echo: {material_layout_result}")

        material_snapshot = query_snapshot(client, 10012, material_asset_path, "material", "MaterialGraph")
        material_nodes = material_snapshot.get("nodes")
        material_edges = material_snapshot.get("edges")
        if not isinstance(material_nodes, list) or not isinstance(material_edges, list):
            fail(f"Material graph.query missing nodes/edges: {material_snapshot}")
        material_root = require_node(material_nodes, "__material_root__")
        if material_root.get("nodeRole") != "materialRoot":
            fail(f"Material root nodeRole mismatch: {material_root}")
        material_root_pos = require_layout(material_root).get("position", {})
        material_multiply_node = require_node(material_nodes, material_multiply_id)
        material_multiply_pos = require_layout(material_multiply_node).get("position", {})
        if material_multiply_pos.get("x", 0) >= material_root_pos.get("x", 0):
            fail(
                "Material sink node was not placed left of the material root: "
                f"sink={material_multiply_pos} root={material_root_pos}"
            )
        for node in material_nodes:
            if not isinstance(node, dict) or node.get("id") == "__material_root__":
                continue
            node_pos = require_layout(node).get("position", {})
            if node_pos.get("x", 0) >= material_root_pos.get("x", 0):
                fail(f"Material non-root node was placed at or right of the material root: node={node} root={material_root}")
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_multiply_id
            and edge.get("toNodeId") == "__material_root__"
            and edge.get("toPin") == "Base Color"
            for edge in material_edges
        ):
            fail(f"Material graph.query did not return multiply->root edge: {material_edges}")
        print("[PASS] material root-aware layout validated")

        pcg_layout_add = call_tool(
            client,
            10100,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGSurfaceSamplerSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"}},
                ],
            },
        )
        pcg_add_results = pcg_layout_add.get("opResults")
        if not isinstance(pcg_add_results, list) or len(pcg_add_results) != 6:
            fail(f"PCG layout add ops missing results: {pcg_layout_add}")
        pcg_create_id, pcg_tag_a_id, pcg_filter_id, pcg_tag_b_id, pcg_sampler_id, pcg_tag_c_id = [
            result.get("nodeId") for result in pcg_add_results
        ]
        if not all(
            isinstance(node_id, str) and node_id
            for node_id in [pcg_create_id, pcg_tag_a_id, pcg_filter_id, pcg_tag_b_id, pcg_sampler_id, pcg_tag_c_id]
        ):
            fail(f"PCG layout add ops missing node ids: {pcg_layout_add}")

        pcg_connect = call_tool(
            client,
            10101,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "connectPins", "args": {"from": {"nodeId": pcg_create_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_a_id, "pin": "In"}}},
                    {"op": "connectPins", "args": {"from": {"nodeId": pcg_tag_a_id, "pin": "Out"}, "to": {"nodeId": pcg_filter_id, "pin": "In"}}},
                    {"op": "connectPins", "args": {"from": {"nodeId": pcg_filter_id, "pin": "InsideFilter"}, "to": {"nodeId": pcg_tag_b_id, "pin": "In"}}},
                    {"op": "connectPins", "args": {"from": {"nodeId": pcg_sampler_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_c_id, "pin": "In"}}},
                    {"op": "layoutGraph", "args": {"scope": "touched"}},
                ],
            },
        )
        pcg_layout_result = op_ok(pcg_connect)
        if pcg_layout_result.get("op") != "connectpins":
            fail(f"PCG connectPins wrong op echo: {pcg_layout_result}")

        pcg_snapshot = query_snapshot(client, 10102, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_nodes = pcg_snapshot.get("nodes")
        pcg_edges = pcg_snapshot.get("edges")
        if not isinstance(pcg_nodes, list) or not isinstance(pcg_edges, list):
            fail(f"PCG graph.query missing nodes/edges: {pcg_snapshot}")

        create_pos = require_layout(require_node(pcg_nodes, pcg_create_id)).get("position", {})
        tag_a_pos = require_layout(require_node(pcg_nodes, pcg_tag_a_id)).get("position", {})
        filter_pos = require_layout(require_node(pcg_nodes, pcg_filter_id)).get("position", {})
        tag_b_pos = require_layout(require_node(pcg_nodes, pcg_tag_b_id)).get("position", {})
        sampler_pos = require_layout(require_node(pcg_nodes, pcg_sampler_id)).get("position", {})
        tag_c_pos = require_layout(require_node(pcg_nodes, pcg_tag_c_id)).get("position", {})

        if not (create_pos.get("x", 0) < tag_a_pos.get("x", 0) < filter_pos.get("x", 0) < tag_b_pos.get("x", 0)):
            fail(
                "PCG primary pipeline did not layout left-to-right: "
                f"{create_pos}, {tag_a_pos}, {filter_pos}, {tag_b_pos}"
            )
        if not (sampler_pos.get("x", 0) < tag_c_pos.get("x", 0)):
            fail(f"PCG secondary pipeline did not layout left-to-right: {sampler_pos}, {tag_c_pos}")
        if abs(sampler_pos.get("y", 0) - create_pos.get("y", 0)) < 32:
            fail(f"PCG parallel rows were not separated vertically enough: {create_pos}, {sampler_pos}")
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == pcg_filter_id
            and edge.get("fromPin") == "InsideFilter"
            and edge.get("toNodeId") == pcg_tag_b_id
            for edge in pcg_edges
        ):
            fail(f"PCG graph.query missing filter branch edge: {pcg_edges}")
        print("[PASS] pcg pipeline layout validated")

        print("[PASS] graph.mutate core ops validated")
        print("[PASS] Bridge regression complete")
        return 0
    finally:
        # Cleanup is intentionally skipped to avoid flaky teardown timeouts
        # masking a fully successful regression run.
        print(f"[WARN] cleanup skipped for temporary asset: {temp_asset}")
        print(f"[WARN] cleanup skipped for temporary material asset: {temp_material_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG asset: {temp_pcg_asset}")
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
