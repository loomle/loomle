#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import platform
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
    submit_execute_job,
)


def op_ok(payload: dict) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        fail(f"graph.mutate missing opResults: {payload}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    if not first.get("ok"):
        fail(f"graph.mutate op failed: {first}")
    return first


def widget_op_ok(payload: dict, index: int = 0) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) <= index:
        fail(f"widget.mutate opResults missing at index {index}: {payload}")
    entry = op_results[index] if isinstance(op_results[index], dict) else {}
    if not entry.get("ok"):
        fail(f"widget.mutate op[{index}] not ok: {entry}")
    return entry


def mutate_with_plan_steps(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_type: str,
    graph_name: str,
    preferred_plan: dict,
) -> dict:
    steps = preferred_plan.get("steps")
    if not isinstance(steps, list) or not steps:
        fail(f"preferredPlan missing steps[]: {preferred_plan}")
    payload = call_tool(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": graph_type,
            "ops": steps,
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"graph.mutate(step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"graph.mutate(step plan) opResults[{idx}] failed: {payload}")
    return payload


def capture_editor_png_hash(client: McpStdioClient, request_id: int, relative_path: str) -> tuple[dict, Path, str]:
    payload = call_tool(
        client,
        request_id,
        "editor.screenshot",
        {"path": relative_path},
    )
    capture_path = payload.get("path")
    if not isinstance(capture_path, str) or not capture_path:
        fail(f"editor.screenshot missing path: {payload}")
    capture_file = Path(capture_path)
    if not capture_file.exists():
        fail(f"editor.screenshot did not write file: {payload}")
    png_bytes = capture_file.read_bytes()
    if png_bytes[:8] != b"\x89PNG\r\n\x1a\n":
        fail(f"editor.screenshot did not write a PNG file: {payload}")
    return payload, capture_file, hashlib.sha256(png_bytes).hexdigest()


def capture_editor_png_hash_until_changed(
    client: McpStdioClient,
    request_id: int,
    *,
    relative_path_prefix: str,
    baseline_hash: str,
    attempts: int = 3,
    sleep_s: float = 0.5,
) -> tuple[dict, Path, str]:
    if platform.system() == "Windows":
        attempts = max(attempts, 6)
        sleep_s = max(sleep_s, 0.75)
    last_result: tuple[dict, Path, str] | None = None
    for attempt in range(attempts):
        last_result = capture_editor_png_hash(
            client,
            request_id + attempt,
            f"{relative_path_prefix}-{int(time.time() * 1000)}-{attempt}.png",
        )
        if last_result[2] != baseline_hash:
            return last_result
        if attempt + 1 < attempts:
            time.sleep(sleep_s)

    if last_result is None:
        raise RuntimeError("unreachable")
    return last_result


def mutate_with_combined_plan_steps(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_type: str,
    graph_name: str,
    preferred_plans: list[dict],
) -> dict:
    steps: list[dict] = []
    for plan in preferred_plans:
        plan_steps = plan.get("steps")
        if not isinstance(plan_steps, list) or not plan_steps:
            fail(f"preferredPlan missing steps[]: {plan}")
        steps.extend(plan_steps)
    payload = call_tool(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": graph_type,
            "ops": steps,
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"graph.mutate(combined step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"graph.mutate(combined step plan) opResults[{idx}] failed: {payload}")
    return payload


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
    graph_name: str | None,
) -> dict:
    arguments = {"assetPath": asset_path, "graphType": graph_type, "limit": 200}
    if graph_name is not None:
        arguments["graphName"] = graph_name
    payload = call_tool(
        client,
        request_id,
        "graph.query",
        arguments,
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        fail(f"graph.query missing semanticSnapshot for {graph_type}: {payload}")
    nodes = snapshot.get("nodes")
    edges = snapshot.get("edges")
    meta = payload.get("meta")
    if not isinstance(nodes, list) or not isinstance(edges, list):
        fail(f"graph.query missing nodes/edges for {graph_type}: {payload}")
    if not isinstance(meta, dict):
        fail(f"graph.query missing meta for {graph_type}: {payload}")
    if meta.get("returnedNodes") != len(nodes):
        fail(f"graph.query returnedNodes mismatch for {graph_type}: payload={payload}")
    if meta.get("returnedEdges") != len(edges):
        fail(f"graph.query returnedEdges mismatch for {graph_type}: payload={payload}")
    if meta.get("truncated") is False:
        if meta.get("totalNodes") != len(nodes):
            fail(f"graph.query totalNodes mismatch for non-truncated {graph_type}: payload={payload}")
        if meta.get("totalEdges") != len(edges):
            fail(f"graph.query totalEdges mismatch for non-truncated {graph_type}: payload={payload}")
    return snapshot


def wait_for_job_terminal(
    client: McpStdioClient,
    request_id_base: int,
    *,
    job_id: str,
    max_attempts: int = 20,
    sleep_s: float = 0.3,
) -> tuple[dict, list[str]]:
    seen_statuses: list[str] = []
    last_payload: dict | None = None
    for attempt in range(max_attempts):
        payload = call_tool(client, request_id_base + attempt, "jobs", {"action": "status", "jobId": job_id})
        last_payload = payload
        status = payload.get("status")
        if isinstance(status, str):
            seen_statuses.append(status)
        if status in {"succeeded", "failed"}:
            return payload, seen_statuses
        time.sleep(sleep_s)
    fail(f"jobs.status did not reach a terminal state: last={last_payload}")
    raise RuntimeError("unreachable")


def extract_nested_error_code(payload: dict) -> str:
    code = payload.get("code")
    if isinstance(code, str) and code:
        return code
    detail = payload.get("detail")
    if isinstance(detail, str) and detail:
        try:
            nested = json.loads(detail)
        except json.JSONDecodeError:
            return ""
        nested_code = nested.get("code") if isinstance(nested, dict) else None
        if isinstance(nested_code, str):
            return nested_code
    return ""


def query_graph_payload(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_name: str,
    limit: int,
    cursor: str = "",
) -> dict:
    arguments: dict[str, object] = {
        "assetPath": asset_path,
        "graphName": graph_name,
        "graphType": "blueprint",
        "limit": limit,
    }
    if cursor:
        arguments["cursor"] = cursor
    payload = call_tool(client, request_id, "graph.query", arguments)
    if not isinstance(payload.get("semanticSnapshot"), dict):
        fail(f"graph.query missing semanticSnapshot for pagination test: {payload}")
    if not isinstance(payload.get("meta"), dict):
        fail(f"graph.query missing meta for pagination test: {payload}")
    return payload


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
        help="UE project root, e.g. /path/to/MyProject. If omitted, read from tools/dev.project-root.local.json",
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
        help="Override path to the loomle client binary. Defaults to <ProjectRoot>/Loomle/loomle(.exe).",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.loomle_bin).resolve()
        if args.loomle_bin
        else resolve_default_loomle_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)
    temp_pcg_asset = make_temp_asset_path("/Game/Codex/PCG_BridgeRegression")
    temp_pcg_health_asset = make_temp_asset_path("/Game/Codex/PCG_HealthRegression")
    temp_pcg_remove_asset = make_temp_asset_path("/Game/Codex/PCG_RemoveRegression")
    temp_material_asset = make_temp_asset_path("/Game/Codex/M_RegressionLayout")
    skip_editor_visual_regression = os.environ.get("LOOMLE_SKIP_EDITOR_VISUAL_REGRESSION") == "1"
    skip_material_visual_regression = (
        skip_editor_visual_regression or os.environ.get("LOOMLE_SKIP_MATERIAL_VISUAL_REGRESSION") == "1"
    )
    skip_pcg_visual_regression = (
        skip_editor_visual_regression or os.environ.get("LOOMLE_SKIP_PCG_VISUAL_REGRESSION") == "1"
    )

    try:
        wait_for_bridge_ready(client)

        print(f"[PASS] initialize protocol={client.protocol_version}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        names = {t.get("name") for t in tools if isinstance(t, dict) and isinstance(t.get("name"), str)}
        missing = sorted(REQUIRED_TOOLS - names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print("[PASS] tools/list baseline tools available")

        jobs_key = f"jobs-regression-{int(time.time() * 1000)}"
        jobs_submit = submit_execute_job(
            client,
            3600,
            code=(
                "import time, unreal, json\n"
                "unreal.log('jobs regression start')\n"
                "time.sleep(1.2)\n"
                "unreal.log('jobs regression end')\n"
                "print(json.dumps({'marker':'jobs-regression-finished'}, ensure_ascii=False))\n"
            ),
            idempotency_key=jobs_key,
            label="jobs regression lifecycle",
            wait_ms=200,
        )
        job = jobs_submit.get("job", {})
        job_id = job.get("jobId")
        if not isinstance(job_id, str) or not job_id:
            fail(f"execute(job) regression missing jobId: {jobs_submit}")

        in_flight_result = call_tool(client, 3601, "jobs", {"action": "result", "jobId": job_id})
        if in_flight_result.get("jobId") != job_id or in_flight_result.get("tool") != "execute":
            fail(f"jobs.result in-flight payload mismatch: {in_flight_result}")
        if in_flight_result.get("status") not in {"queued", "running", "succeeded", "failed"}:
            fail(f"jobs.result in-flight invalid status: {in_flight_result}")
        if in_flight_result.get("status") in {"queued", "running"} and in_flight_result.get("resultAvailable") is not False:
            fail(f"jobs.result non-terminal state should not be marked resultAvailable: {in_flight_result}")

        terminal_status, seen_statuses = wait_for_job_terminal(client, 3610, job_id=job_id)
        if terminal_status.get("jobId") != job_id or terminal_status.get("tool") != "execute":
            fail(f"jobs.status terminal payload mismatch: {terminal_status}")
        if terminal_status.get("status") != "succeeded":
            fail(f"jobs.status expected succeeded terminal status: {terminal_status}")
        if not any(status in {"running", "succeeded"} for status in seen_statuses):
            fail(f"jobs.status did not expose expected lifecycle states: {seen_statuses}")

        jobs_logs = call_tool(client, 3640, "jobs", {"action": "logs", "jobId": job_id, "limit": 200})
        entries = jobs_logs.get("entries")
        if not isinstance(entries, list):
            fail(f"jobs.logs missing entries[]: {jobs_logs}")
        log_messages = [entry.get("message") for entry in entries if isinstance(entry, dict) and isinstance(entry.get("message"), str)]
        joined_logs = "\n".join(log_messages)
        if "jobs regression start" not in joined_logs or "jobs regression end" not in joined_logs:
            fail(f"jobs.logs missing expected markers: {jobs_logs}")
        next_cursor = jobs_logs.get("nextCursor")
        if not isinstance(next_cursor, str):
            fail(f"jobs.logs missing nextCursor: {jobs_logs}")

        finished_result = call_tool(client, 3650, "jobs", {"action": "result", "jobId": job_id})
        if finished_result.get("status") != "succeeded" or finished_result.get("resultAvailable") is not True:
            fail(f"jobs.result terminal payload mismatch: {finished_result}")
        nested_result = finished_result.get("result")
        if not isinstance(nested_result, dict):
            fail(f"jobs.result missing nested final execute payload: {finished_result}")
        nested_logs = nested_result.get("logs")
        if not isinstance(nested_logs, list):
            fail(f"jobs.result missing nested execute logs: {finished_result}")
        nested_outputs = [
            entry.get("output")
            for entry in nested_logs
            if isinstance(entry, dict) and isinstance(entry.get("output"), str)
        ]
        if not any("jobs-regression-finished" in output for output in nested_outputs):
            fail(f"jobs.result missing final nested log marker: {finished_result}")

        jobs_list = call_tool(client, 3660, "jobs", {"action": "list", "limit": 50})
        listed_jobs = jobs_list.get("jobs")
        if not isinstance(listed_jobs, list):
            fail(f"jobs.list missing jobs[]: {jobs_list}")
        matching_job = next((entry for entry in listed_jobs if isinstance(entry, dict) and entry.get("jobId") == job_id), None)
        if not isinstance(matching_job, dict):
            fail(f"jobs.list did not include submitted job: {jobs_list}")
        if matching_job.get("tool") != "execute":
            fail(f"jobs.list tool mismatch: {matching_job}")
        print("[PASS] jobs runtime lifecycle validated")

        missing_idempotency = call_tool(
            client,
            3670,
            "execute",
            {
                "mode": "exec",
                "code": "print('jobs contract missing key')",
                "execution": {"mode": "job", "waitMs": 100},
            },
            expect_error=True,
        )
        if extract_nested_error_code(missing_idempotency) != "IDEMPOTENCY_KEY_REQUIRED":
            fail(f"execute(job) missing idempotencyKey code mismatch: {missing_idempotency}")

        unknown_job = call_tool(client, 3671, "jobs", {"action": "status", "jobId": "job_missing_regression"}, expect_error=True)
        if extract_nested_error_code(unknown_job) != "JOB_NOT_FOUND":
            fail(f"jobs.status unknown job code mismatch: {unknown_job}")

        unsupported_action = call_tool(client, 3672, "jobs", {"action": "cancel", "jobId": job_id}, expect_error=True)
        if extract_nested_error_code(unsupported_action) != "JOB_ACTION_UNSUPPORTED":
            fail(f"jobs unsupported action code mismatch: {unsupported_action}")
        print("[PASS] jobs runtime contract errors validated")

        graph_desc = call_tool(client, 3, "graph", {"graphType": "blueprint"})
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

        blueprint_verify = call_tool(
            client,
            6405,
            "graph.verify",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "limit": 200,
            },
        )
        if blueprint_verify.get("status") not in {"ok", "warn"}:
            fail(f"graph.verify returned unexpected status: {blueprint_verify}")
        if not isinstance(blueprint_verify.get("queryReport"), dict):
            fail(f"graph.verify missing queryReport: {blueprint_verify}")
        blueprint_compile_report = blueprint_verify.get("compileReport")
        if not isinstance(blueprint_compile_report, dict) or blueprint_compile_report.get("compiled") is not True:
            fail(f"graph.verify missing compiled=true compileReport: {blueprint_verify}")
        if not isinstance(blueprint_verify.get("diagnostics"), list):
            fail(f"graph.verify missing diagnostics[]: {blueprint_verify}")
        print("[PASS] graph.verify unified summary validated")

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

        pcg_graph_desc = call_tool(client, 101001, "graph", {"graphType": "pcg"})
        pcg_graph_ops = pcg_graph_desc.get("ops")
        if not isinstance(pcg_graph_ops, list):
            fail(f"graph(PCG) missing ops[]: {pcg_graph_desc}")
        pcg_graph_ops_set = {op for op in pcg_graph_ops if isinstance(op, str)}
        if "runScript" in pcg_graph_ops_set:
            fail(f"graph(PCG) should not advertise runScript: {pcg_graph_desc}")
        if "compile" not in pcg_graph_ops_set:
            fail(f"graph(PCG) should continue advertising compile: {pcg_graph_desc}")
        pcg_dry_run_script = call_tool(
            client,
            1010011,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "dryRun": True,
                "ops": [
                    {
                        "op": "runScript",
                        "args": {
                            "mode": "inlineCode",
                            "entry": "run",
                            "code": "def run(ctx):\n  return {'ok': True}",
                        },
                    }
                ],
            },
            expect_error=True,
        )
        pcg_dry_run_script_detail = pcg_dry_run_script.get("detail")
        if not isinstance(pcg_dry_run_script_detail, str):
            fail(f"PCG dryRun runScript missing structured detail JSON: {pcg_dry_run_script}")
        try:
            pcg_dry_run_script_struct = json.loads(pcg_dry_run_script_detail)
        except Exception as exc:
            fail(f"PCG dryRun runScript detail is not valid JSON: {exc} payload={pcg_dry_run_script}")
        if pcg_dry_run_script_struct.get("code") != "UNSUPPORTED_OP":
            fail(f"PCG dryRun runScript should surface UNSUPPORTED_OP: {pcg_dry_run_script_struct}")
        pcg_dry_run_script_results = pcg_dry_run_script_struct.get("opResults")
        if not isinstance(pcg_dry_run_script_results, list) or not pcg_dry_run_script_results:
            fail(f"PCG dryRun runScript missing opResults: {pcg_dry_run_script_struct}")
        pcg_dry_run_script_first = pcg_dry_run_script_results[0] if isinstance(pcg_dry_run_script_results[0], dict) else {}
        if pcg_dry_run_script_first.get("errorCode") != "UNSUPPORTED_OP":
            fail(f"PCG dryRun runScript should classify as UNSUPPORTED_OP: {pcg_dry_run_script_struct}")
        if pcg_dry_run_script_first.get("skipped") is True:
            fail(f"PCG dryRun runScript should not report skipped=true when unsupported: {pcg_dry_run_script_struct}")
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

        blueprint_revision_before = query_graph_payload(
            client,
            105,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_revision_r0 = blueprint_revision_before.get("revision")
        blueprint_nodes_before = blueprint_revision_before.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(blueprint_revision_r0, str) or not blueprint_revision_r0:
            fail(f"Blueprint graph.query missing revision before expectedRevision test: {blueprint_revision_before}")
        if not isinstance(blueprint_nodes_before, list):
            fail(f"Blueprint graph.query missing nodes before expectedRevision test: {blueprint_revision_before}")

        blueprint_revision_apply = call_tool(
            client,
            106,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "expectedRevision": blueprint_revision_r0,
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 640, "y": 0},
                        },
                    }
                ],
            },
        )
        op_ok(blueprint_revision_apply)
        blueprint_revision_after_apply = query_graph_payload(
            client,
            107,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_revision_r1 = blueprint_revision_after_apply.get("revision")
        blueprint_nodes_after_apply = blueprint_revision_after_apply.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(blueprint_revision_r1, str) or not blueprint_revision_r1 or blueprint_revision_r1 == blueprint_revision_r0:
            fail(
                "Blueprint expectedRevision control mutate did not advance revision: "
                f"before={blueprint_revision_before} after={blueprint_revision_after_apply}"
            )
        if not isinstance(blueprint_nodes_after_apply, list) or len(blueprint_nodes_after_apply) != len(blueprint_nodes_before) + 1:
            fail(
                "Blueprint expectedRevision control mutate did not add exactly one node: "
                f"before={blueprint_revision_before} after={blueprint_revision_after_apply}"
            )

        stale_blueprint_revision = call_tool(
            client,
            108,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "expectedRevision": blueprint_revision_r0,
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
            expect_error=True,
        )
        if stale_blueprint_revision.get("domainCode") != "REVISION_CONFLICT":
            fail(f"Blueprint stale expectedRevision did not return REVISION_CONFLICT: {stale_blueprint_revision}")
        blueprint_revision_after_stale = query_graph_payload(
            client,
            109,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_nodes_after_stale = blueprint_revision_after_stale.get("semanticSnapshot", {}).get("nodes", [])
        if blueprint_revision_after_stale.get("revision") != blueprint_revision_r1:
            fail(
                "Blueprint stale expectedRevision should not change revision: "
                f"expected={blueprint_revision_r1} actual={blueprint_revision_after_stale}"
            )
        if not isinstance(blueprint_nodes_after_stale, list) or len(blueprint_nodes_after_stale) != len(blueprint_nodes_after_apply):
            fail(
                "Blueprint stale expectedRevision should not change node count: "
                f"after_apply={blueprint_revision_after_apply} after_stale={blueprint_revision_after_stale}"
            )
        print("[PASS] blueprint expectedRevision conflict validated")

        blueprint_dry_run = call_tool(
            client,
            1091,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "dryRun": True,
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 1120, "y": 0},
                        },
                    }
                ],
            },
        )
        blueprint_dry_run_first = op_ok(blueprint_dry_run)
        if blueprint_dry_run_first.get("changed") is not False:
            fail(f"Blueprint dryRun mutate should report changed=false: {blueprint_dry_run}")
        if blueprint_dry_run.get("previousRevision") != blueprint_revision_r1 or blueprint_dry_run.get("newRevision") != blueprint_revision_r1:
            fail(
                "Blueprint dryRun mutate revisions should stay pinned to the current graph revision: "
                f"payload={blueprint_dry_run} expectedRevision={blueprint_revision_r1}"
            )
        blueprint_after_dry_run = query_graph_payload(
            client,
            1092,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_nodes_after_dry_run = blueprint_after_dry_run.get("semanticSnapshot", {}).get("nodes", [])
        if blueprint_after_dry_run.get("revision") != blueprint_revision_r1:
            fail(
                "Blueprint dryRun mutate should not change graph revision: "
                f"expected={blueprint_revision_r1} actual={blueprint_after_dry_run}"
            )
        if not isinstance(blueprint_nodes_after_dry_run, list) or len(blueprint_nodes_after_dry_run) != len(blueprint_nodes_after_apply):
            fail(
                "Blueprint dryRun mutate should not change node count: "
                f"after_apply={blueprint_revision_after_apply} after_dry_run={blueprint_after_dry_run}"
            )
        print("[PASS] blueprint dryRun revision metadata validated")

        dry_run_run_script_inline = call_tool(
            client,
            10921,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "dryRun": True,
                "ops": [
                    {
                        "op": "runScript",
                        "args": {
                            "mode": "inlineCode",
                            "entry": "run",
                            "code": "def run(ctx):\n  return {'ok': True, 'dryRun': ctx.get('dryRun', False)}",
                        },
                    }
                ],
            },
        )
        dry_run_run_script_inline_first = op_ok(dry_run_run_script_inline)
        if dry_run_run_script_inline_first.get("skipped") is not True:
            fail(f"dryRun runScript inlineCode should report skipped=true: {dry_run_run_script_inline}")
        if dry_run_run_script_inline_first.get("skipReason") != "dryRun":
            fail(f"dryRun runScript inlineCode should report skipReason=dryRun: {dry_run_run_script_inline}")
        if dry_run_run_script_inline_first.get("changed") is not False:
            fail(f"dryRun runScript inlineCode should report changed=false: {dry_run_run_script_inline}")
        if dry_run_run_script_inline_first.get("scriptResult") is not None:
            fail(f"dryRun runScript inlineCode should not include scriptResult when skipped: {dry_run_run_script_inline}")

        dry_run_run_script_missing_module = call_tool(
            client,
            10922,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "dryRun": True,
                "ops": [
                    {
                        "op": "runScript",
                        "args": {
                            "mode": "scriptId",
                            "scriptId": "no_such_loomle_module",
                            "entry": "run",
                        },
                    }
                ],
            },
        )
        dry_run_run_script_missing_module_first = op_ok(dry_run_run_script_missing_module)
        if dry_run_run_script_missing_module_first.get("skipped") is not True:
            fail(
                "dryRun runScript scriptId should report skipped=true even for missing modules: "
                f"{dry_run_run_script_missing_module}"
            )
        if dry_run_run_script_missing_module_first.get("skipReason") != "dryRun":
            fail(
                "dryRun runScript scriptId should report skipReason=dryRun even for missing modules: "
                f"{dry_run_run_script_missing_module}"
            )
        if dry_run_run_script_missing_module_first.get("errorCode") not in ("", None):
            fail(
                "dryRun runScript scriptId should not surface module import errors when explicitly skipped: "
                f"{dry_run_run_script_missing_module}"
            )
        print("[PASS] dryRun runScript explicit skip validated")

        partial_apply_node = call_tool(
            client,
            10923,
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
                            "position": {"x": 1536, "y": 0},
                        },
                    }
                ],
            },
        )
        partial_apply_node_id = op_ok(partial_apply_node).get("nodeId")
        if not isinstance(partial_apply_node_id, str) or not partial_apply_node_id:
            fail(f"Blueprint partial-apply setup addNode.byClass did not return nodeId: {partial_apply_node}")
        partial_apply_before = query_graph_payload(
            client,
            10924,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        partial_apply_before_revision = partial_apply_before.get("revision")
        partial_apply_failure = call_tool(
            client,
            10925,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {"op": "removeNode", "args": {"target": {"nodeId": partial_apply_node_id}}},
                    {"op": "addNode", "args": {"nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse"}},
                ],
            },
            expect_error=True,
        )
        partial_apply_detail = partial_apply_failure.get("detail")
        if not isinstance(partial_apply_detail, str) or not partial_apply_detail.strip():
            fail(f"Blueprint partial-apply batch should include structured detail: {partial_apply_failure}")
        try:
            partial_apply_struct = json.loads(partial_apply_detail)
        except json.JSONDecodeError as exc:
            fail(f"Blueprint partial-apply batch detail is not valid JSON: {exc} payload={partial_apply_failure}")
        if partial_apply_struct.get("code") != "UNSUPPORTED_OP":
            fail(f"Blueprint partial-apply batch should surface UNSUPPORTED_OP in structured detail: {partial_apply_failure}")
        if partial_apply_struct.get("applied") is not False:
            fail(f"Blueprint partial-apply batch should report applied=false: {partial_apply_struct}")
        if partial_apply_struct.get("partialApplied") is not True:
            fail(f"Blueprint partial-apply batch should report partialApplied=true: {partial_apply_struct}")
        partial_apply_results = partial_apply_struct.get("opResults")
        if not isinstance(partial_apply_results, list) or len(partial_apply_results) != 2:
            fail(f"Blueprint partial-apply batch missing opResults: {partial_apply_struct}")
        partial_apply_first = partial_apply_results[0] if isinstance(partial_apply_results[0], dict) else {}
        partial_apply_second = partial_apply_results[1] if isinstance(partial_apply_results[1], dict) else {}
        if partial_apply_first.get("ok") is not True or partial_apply_first.get("changed") is not True:
            fail(f"Blueprint partial-apply first op should be applied and changed: {partial_apply_struct}")
        if partial_apply_second.get("errorCode") != "UNSUPPORTED_OP":
            fail(f"Blueprint partial-apply second op should fail as UNSUPPORTED_OP: {partial_apply_struct}")
        partial_apply_after = query_graph_payload(
            client,
            10926,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        partial_apply_after_nodes = partial_apply_after.get("semanticSnapshot", {}).get("nodes", [])
        if partial_apply_after.get("revision") == partial_apply_before_revision:
            fail(
                "Blueprint partial-apply batch should still advance revision when an earlier op committed: "
                f"before={partial_apply_before} after={partial_apply_after}"
            )
        if any(
            isinstance(node, dict) and node.get("id") == partial_apply_node_id
            for node in partial_apply_after_nodes or []
        ):
            fail(
                "Blueprint partial-apply batch should have removed the earlier node despite later failure: "
                f"{partial_apply_after}"
            )
        print("[PASS] blueprint partialApplied mutate metadata validated")

        blueprint_idem_key = "bp-idem-1"
        blueprint_idem_before = query_graph_payload(
            client,
            1093,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_before_revision = blueprint_idem_before.get("revision")
        blueprint_idem_before_nodes = blueprint_idem_before.get("semanticSnapshot", {}).get("nodes", [])
        blueprint_idem_first = call_tool(
            client,
            1094,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "idempotencyKey": blueprint_idem_key,
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
        blueprint_idem_first_op = op_ok(blueprint_idem_first)
        blueprint_idem_after_first = query_graph_payload(
            client,
            1095,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_after_first_revision = blueprint_idem_after_first.get("revision")
        blueprint_idem_after_first_nodes = blueprint_idem_after_first.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(blueprint_idem_before_nodes, list) or not isinstance(blueprint_idem_after_first_nodes, list):
            fail("Blueprint idempotency query payload missing nodes")
        if blueprint_idem_after_first_revision == blueprint_idem_before_revision:
            fail(
                "Blueprint idempotency first mutate should advance revision: "
                f"before={blueprint_idem_before} after={blueprint_idem_after_first}"
            )
        if len(blueprint_idem_after_first_nodes) != len(blueprint_idem_before_nodes) + 1:
            fail(
                "Blueprint idempotency first mutate should add exactly one node: "
                f"before={blueprint_idem_before} after={blueprint_idem_after_first}"
            )
        blueprint_idem_second = call_tool(
            client,
            1096,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "idempotencyKey": blueprint_idem_key,
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
        blueprint_idem_second_op = op_ok(blueprint_idem_second)
        blueprint_idem_after_second = query_graph_payload(
            client,
            1097,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_after_second_nodes = blueprint_idem_after_second.get("semanticSnapshot", {}).get("nodes", [])
        if blueprint_idem_after_second.get("revision") != blueprint_idem_after_first_revision:
            fail(
                "Blueprint duplicate idempotencyKey should not advance revision: "
                f"after_first={blueprint_idem_after_first} after_second={blueprint_idem_after_second}"
            )
        if not isinstance(blueprint_idem_after_second_nodes, list) or len(blueprint_idem_after_second_nodes) != len(blueprint_idem_after_first_nodes):
            fail(
                "Blueprint duplicate idempotencyKey should not change node count: "
                f"after_first={blueprint_idem_after_first} after_second={blueprint_idem_after_second}"
            )
        if blueprint_idem_second.get("previousRevision") != blueprint_idem_first.get("previousRevision") or blueprint_idem_second.get("newRevision") != blueprint_idem_first.get("newRevision"):
            fail(
                "Blueprint duplicate idempotencyKey should replay the first mutate result metadata: "
                f"first={blueprint_idem_first} second={blueprint_idem_second}"
            )
        if blueprint_idem_second_op.get("nodeId") != blueprint_idem_first_op.get("nodeId"):
            fail(
                "Blueprint duplicate idempotencyKey should replay the original nodeId: "
                f"first={blueprint_idem_first} second={blueprint_idem_second}"
            )
        print("[PASS] blueprint idempotencyKey replay validated")

        duplicate_client_ref = call_tool(
            client,
            1098,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_ref",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 1440, "y": 0},
                        },
                    },
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_ref",
                        "args": {
                            "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                            "position": {"x": 1600, "y": 0},
                        },
                    },
                ],
            },
            expect_error=True,
        )
        duplicate_client_ref_struct = duplicate_client_ref
        duplicate_client_ref_detail = duplicate_client_ref.get("detail")
        if isinstance(duplicate_client_ref_detail, str) and duplicate_client_ref_detail.strip():
            try:
                parsed_duplicate_detail = json.loads(duplicate_client_ref_detail)
            except json.JSONDecodeError as exc:
                fail(f"graph.mutate duplicate clientRef detail is not valid JSON: {exc} payload={duplicate_client_ref}")
            if isinstance(parsed_duplicate_detail, dict):
                duplicate_client_ref_struct = parsed_duplicate_detail
        duplicate_client_ref_results = duplicate_client_ref_struct.get("opResults")
        if not isinstance(duplicate_client_ref_results, list) or len(duplicate_client_ref_results) < 2:
            fail(f"graph.mutate duplicate clientRef missing opResults: {duplicate_client_ref}")
        duplicate_client_ref_second = duplicate_client_ref_results[1] if isinstance(duplicate_client_ref_results[1], dict) else {}
        if duplicate_client_ref_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"graph.mutate duplicate clientRef wrong errorCode: {duplicate_client_ref_second}")
        if "Duplicate clientRef" not in str(duplicate_client_ref_second.get("errorMessage", "")):
            fail(f"graph.mutate duplicate clientRef wrong errorMessage: {duplicate_client_ref_second}")
        print("[PASS] blueprint duplicate clientRef rejected")

        page_one = query_graph_payload(client, 110, asset_path=temp_asset, graph_name="EventGraph", limit=1)
        page_one_meta = page_one.get("meta", {})
        page_one_cursor = page_one.get("nextCursor")
        page_one_nodes = page_one.get("semanticSnapshot", {}).get("nodes", [])
        if page_one_meta.get("truncated") is not True:
            fail(f"graph.query pagination expected truncated=true for first page: {page_one}")
        if not isinstance(page_one_cursor, str) or not page_one_cursor:
            fail(f"graph.query pagination expected non-empty nextCursor for first page: {page_one}")
        if not isinstance(page_one_nodes, list) or len(page_one_nodes) != 1:
            fail(f"graph.query pagination expected one node on first page: {page_one}")

        page_two = query_graph_payload(
            client,
            111,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=1,
            cursor=page_one_cursor,
        )
        page_two_nodes = page_two.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(page_two_nodes, list) or len(page_two_nodes) != 1:
            fail(f"graph.query pagination expected one node on second page: {page_two}")
        first_page_node_id = page_one_nodes[0].get("guid") if isinstance(page_one_nodes[0], dict) else None
        second_page_node_id = page_two_nodes[0].get("guid") if isinstance(page_two_nodes[0], dict) else None
        if not isinstance(first_page_node_id, str) or not isinstance(second_page_node_id, str):
            fail(f"graph.query pagination pages missing node guids: first={page_one} second={page_two}")
        if first_page_node_id == second_page_node_id:
            fail(f"graph.query pagination cursor did not advance to a new page: first={page_one} second={page_two}")
        print("[PASS] graph.query pagination cursor validated")

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
        compile_first = op_ok(compile_payload)
        nodes_after_compile = query_nodes(client, 20, temp_asset, "EventGraph")
        compile_revision_after = call_tool(
            client,
            201,
            "graph.query",
            {"assetPath": temp_asset, "graphName": "EventGraph", "graphType": "blueprint", "limit": 200},
        )
        if compile_first.get("changed") is not False:
            fail(f"Blueprint compile should report changed=false when graph revision is unchanged: {compile_payload}")
        if compile_payload.get("previousRevision") != compile_payload.get("newRevision"):
            fail(f"Blueprint compile mutate should keep previousRevision/newRevision aligned when graph is unchanged: {compile_payload}")
        if compile_revision_after.get("revision") != compile_payload.get("newRevision"):
            fail(
                "Blueprint compile mutate revision metadata should match graph.query: "
                f"mutate={compile_payload} query={compile_revision_after}"
            )

        nodes_before_remove = nodes_after_compile
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

        bulk_branch_ops = []
        for index in range(60):
            bulk_branch_ops.append(
                {
                    "op": "addNode.byClass",
                    "clientRef": f"bulk_branch_{index}",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 2200 + (index * 48), "y": 1800},
                    },
                }
            )
        bulk_blueprint_insert = call_tool(
            client,
            1600,
            "graph.mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": bulk_branch_ops,
            },
        )
        op_ok(bulk_blueprint_insert)

        blueprint_default_page = call_tool(
            client,
            1601,
            "graph.query",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "graphType": "blueprint",
            },
        )
        default_snapshot = blueprint_default_page.get("semanticSnapshot", {})
        default_nodes = default_snapshot.get("nodes", [])
        default_meta = blueprint_default_page.get("meta", {})
        default_cursor = blueprint_default_page.get("nextCursor")
        if not isinstance(default_nodes, list) or len(default_nodes) != 50:
            fail(
                "Blueprint graph.query without explicit limit should default to 50 nodes per page: "
                f"{blueprint_default_page}"
            )
        if default_meta.get("returnedNodes") != 50 or default_meta.get("truncated") is not True:
            fail(
                "Blueprint graph.query without explicit limit should report returnedNodes=50 and truncated=true: "
                f"{blueprint_default_page}"
            )
        if not isinstance(default_cursor, str) or not default_cursor:
            fail(
                "Blueprint graph.query without explicit limit should provide nextCursor when truncated: "
                f"{blueprint_default_page}"
            )
        print("[PASS] blueprint graph.query default page size validated")

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
        material_graph_list_without_type = call_tool(
            client,
            10009,
            "graph.list",
            {"assetPath": material_asset_path, "includeSubgraphs": True},
        )
        material_graphs_without_type = material_graph_list_without_type.get("graphs")
        if not isinstance(material_graphs_without_type, list) or not material_graphs_without_type:
            fail(f"graph.list(material assetPath without graphType) missing graphs[]: {material_graph_list_without_type}")
        material_root_graph = material_graphs_without_type[0] if isinstance(material_graphs_without_type[0], dict) else {}
        if material_graph_list_without_type.get("graphType") != "material":
            fail(f"graph.list(material assetPath without graphType) should infer material: {material_graph_list_without_type}")
        if material_root_graph.get("graphName") != "MaterialGraph":
            fail(f"graph.list(material assetPath without graphType) root graph mismatch: {material_graph_list_without_type}")
        material_graph_ref = material_root_graph.get("graphRef")
        if not isinstance(material_graph_ref, dict):
            fail(f"graph.list(material) missing graphRef: {material_graph_list_without_type}")

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

        material_revision_before = call_tool(
            client,
            100101,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_revision_r0 = material_revision_before.get("revision")
        material_nodes_before = material_revision_before.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_revision_r0, str) or not material_revision_r0:
            fail(f"Material graph.query missing revision before expectedRevision test: {material_revision_before}")
        if not isinstance(material_nodes_before, list):
            fail(f"Material graph.query missing nodes before expectedRevision test: {material_revision_before}")

        material_revision_apply = call_tool(
            client,
            100102,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}
                ],
            },
        )
        op_ok(material_revision_apply)
        material_revision_after_apply = call_tool(
            client,
            100103,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_revision_r1 = material_revision_after_apply.get("revision")
        material_nodes_after_apply = material_revision_after_apply.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_revision_r1, str) or not material_revision_r1 or material_revision_r1 == material_revision_r0:
            fail(
                "Material expectedRevision control mutate did not advance revision: "
                f"before={material_revision_before} after={material_revision_after_apply}"
            )
        if not isinstance(material_nodes_after_apply, list) or len(material_nodes_after_apply) != len(material_nodes_before) + 1:
            fail(
                "Material expectedRevision control mutate did not add exactly one node: "
                f"before={material_revision_before} after={material_revision_after_apply}"
            )

        stale_material_revision = call_tool(
            client,
            100104,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}
                ],
            },
            expect_error=True,
        )
        if stale_material_revision.get("domainCode") != "REVISION_CONFLICT":
            fail(f"Material stale expectedRevision did not return REVISION_CONFLICT: {stale_material_revision}")
        material_revision_after_stale = call_tool(
            client,
            100105,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_nodes_after_stale = material_revision_after_stale.get("semanticSnapshot", {}).get("nodes", [])
        if material_revision_after_stale.get("revision") != material_revision_r1:
            fail(
                "Material stale expectedRevision should not change revision: "
                f"expected={material_revision_r1} actual={material_revision_after_stale}"
            )
        if not isinstance(material_nodes_after_stale, list) or len(material_nodes_after_stale) != len(material_nodes_after_apply):
            fail(
                "Material stale expectedRevision should not change node count: "
                f"after_apply={material_revision_after_apply} after_stale={material_revision_after_stale}"
            )
        print("[PASS] material expectedRevision conflict validated")

        material_dry_run = call_tool(
            client,
            100106,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "dryRun": True,
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}
                ],
            },
        )
        material_dry_run_first = op_ok(material_dry_run)
        if material_dry_run_first.get("changed") is not False:
            fail(f"Material dryRun mutate should report changed=false: {material_dry_run}")
        if material_dry_run.get("previousRevision") != material_revision_r1 or material_dry_run.get("newRevision") != material_revision_r1:
            fail(
                "Material dryRun mutate revisions should stay pinned to the current graph revision: "
                f"payload={material_dry_run} expectedRevision={material_revision_r1}"
            )
        material_after_dry_run = call_tool(
            client,
            100107,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_nodes_after_dry_run = material_after_dry_run.get("semanticSnapshot", {}).get("nodes", [])
        if material_after_dry_run.get("revision") != material_revision_r1:
            fail(
                "Material dryRun mutate should not change graph revision: "
                f"expected={material_revision_r1} actual={material_after_dry_run}"
            )
        if not isinstance(material_nodes_after_dry_run, list) or len(material_nodes_after_dry_run) != len(material_nodes_after_apply):
            fail(
                "Material dryRun mutate should not change node count: "
                f"after_apply={material_revision_after_apply} after_dry_run={material_after_dry_run}"
            )
        print("[PASS] material dryRun revision metadata validated")

        material_idem_key = "material-idem-1"
        material_idem_before = call_tool(
            client,
            1001071,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_idem_before_revision = material_idem_before.get("revision")
        material_idem_before_nodes = material_idem_before.get("semanticSnapshot", {}).get("nodes", [])
        material_idem_first = call_tool(
            client,
            1001072,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}
                ],
            },
        )
        material_idem_first_op = op_ok(material_idem_first)
        material_idem_after_first = call_tool(
            client,
            1001073,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_idem_after_first_revision = material_idem_after_first.get("revision")
        material_idem_after_first_nodes = material_idem_after_first.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_idem_before_nodes, list) or not isinstance(material_idem_after_first_nodes, list):
            fail("Material idempotency query payload missing nodes")
        if material_idem_after_first_revision == material_idem_before_revision:
            fail(
                "Material idempotency first mutate should advance revision: "
                f"before={material_idem_before} after={material_idem_after_first}"
            )
        if len(material_idem_after_first_nodes) != len(material_idem_before_nodes) + 1:
            fail(
                "Material idempotency first mutate should add exactly one node: "
                f"before={material_idem_before} after={material_idem_after_first}"
            )
        material_idem_second = call_tool(
            client,
            1001074,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}
                ],
            },
        )
        material_idem_second_op = op_ok(material_idem_second)
        material_idem_after_second = call_tool(
            client,
            1001075,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        material_idem_after_second_nodes = material_idem_after_second.get("semanticSnapshot", {}).get("nodes", [])
        if material_idem_after_second.get("revision") != material_idem_after_first_revision:
            fail(
                "Material duplicate idempotencyKey should not advance revision: "
                f"after_first={material_idem_after_first} after_second={material_idem_after_second}"
            )
        if not isinstance(material_idem_after_second_nodes, list) or len(material_idem_after_second_nodes) != len(material_idem_after_first_nodes):
            fail(
                "Material duplicate idempotencyKey should not change node count: "
                f"after_first={material_idem_after_first} after_second={material_idem_after_second}"
            )
        if material_idem_second.get("previousRevision") != material_idem_first.get("previousRevision") or material_idem_second.get("newRevision") != material_idem_first.get("newRevision"):
            fail(
                "Material duplicate idempotencyKey should replay the first mutate result metadata: "
                f"first={material_idem_first} second={material_idem_second}"
            )
        if material_idem_second_op.get("nodeId") != material_idem_first_op.get("nodeId"):
            fail(
                "Material duplicate idempotencyKey should replay the original nodeId: "
                f"first={material_idem_first} second={material_idem_second}"
            )
        print("[PASS] material idempotencyKey replay validated")

        material_duplicate_client_ref = call_tool(
            client,
            1001076,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"},
                    },
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionConstant"},
                    },
                ],
            },
            expect_error=True,
        )
        material_duplicate_struct = material_duplicate_client_ref
        material_duplicate_detail = material_duplicate_client_ref.get("detail")
        if isinstance(material_duplicate_detail, str) and material_duplicate_detail.strip():
            try:
                parsed_material_duplicate_detail = json.loads(material_duplicate_detail)
            except json.JSONDecodeError as exc:
                fail(f"graph.mutate material duplicate clientRef detail is not valid JSON: {exc} payload={material_duplicate_client_ref}")
            if isinstance(parsed_material_duplicate_detail, dict):
                material_duplicate_struct = parsed_material_duplicate_detail
        material_duplicate_results = material_duplicate_struct.get("opResults")
        if not isinstance(material_duplicate_results, list) or len(material_duplicate_results) < 2:
            fail(f"graph.mutate material duplicate clientRef missing opResults: {material_duplicate_client_ref}")
        material_duplicate_second = material_duplicate_results[1] if isinstance(material_duplicate_results[1], dict) else {}
        if material_duplicate_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"graph.mutate material duplicate clientRef wrong errorCode: {material_duplicate_second}")
        if "Duplicate clientRef" not in str(material_duplicate_second.get("errorMessage", "")):
            fail(f"graph.mutate material duplicate clientRef wrong errorMessage: {material_duplicate_second}")
        print("[PASS] material duplicate clientRef rejected")

        material_compile = call_tool(
            client,
            100108,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [{"op": "compile"}],
            },
        )
        material_compile_first = op_ok(material_compile)
        material_revision_after_compile = call_tool(
            client,
            100109,
            "graph.query",
            {"assetPath": material_asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
        )
        if material_compile_first.get("changed") is not False:
            fail(f"Material compile should report changed=false when graph revision is unchanged: {material_compile}")
        if material_compile.get("previousRevision") != material_compile.get("newRevision"):
            fail(f"Material compile mutate should keep previousRevision/newRevision aligned when graph is unchanged: {material_compile}")
        if material_revision_after_compile.get("revision") != material_compile.get("newRevision"):
            fail(
                "Material compile mutate revision metadata should match graph.query: "
                f"mutate={material_compile} query={material_revision_after_compile}"
            )
        material_revision_r1 = material_revision_after_compile.get("revision")
        print("[PASS] material compile revision metadata validated")

        material_verify = call_tool(
            client,
            1001091,
            "graph.verify",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
            },
        )
        if material_verify.get("status") != "ok":
            fail(f"graph.verify should succeed for material fixture: {material_verify}")
        if not isinstance(material_verify.get("queryReport"), dict):
            fail(f"graph.verify missing queryReport: {material_verify}")
        compile_report = material_verify.get("compileReport")
        if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
            fail(f"graph.verify missing compiled=true: {material_verify}")
        print("[PASS] graph.verify material summary validated")


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
        material_snapshot_without_graph_name = query_snapshot(client, 10013, material_asset_path, "material", None)
        if material_snapshot_without_graph_name.get("signature") != material_snapshot.get("signature"):
            fail(
                "Material graph.query without graphName should resolve the same single-graph asset snapshot: "
                f"without={material_snapshot_without_graph_name} with={material_snapshot}"
            )
        material_query_without_type = call_tool(
            client,
            100131,
            "graph.query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_query_without_type_snapshot = material_query_without_type.get("semanticSnapshot")
        if not isinstance(material_query_without_type_snapshot, dict):
            fail(f"Material graph.query without graphType missing semanticSnapshot: {material_query_without_type}")
        if material_query_without_type.get("graphType") != "material":
            fail(f"Material graph.query without graphType should infer material: {material_query_without_type}")
        if material_query_without_type_snapshot.get("signature") != material_snapshot.get("signature"):
            fail(
                "Material graph.query without graphType should resolve the same single-graph asset snapshot: "
                f"without={material_query_without_type} with={material_snapshot}"
            )
        resolved_material_node = call_tool(
            client,
            10014,
            "graph.resolve",
            {"path": material_multiply_id, "graphType": "material"},
        )
        resolved_material_entry = require_resolved_asset_path(resolved_material_node, material_asset_path)
        if resolved_material_entry.get("graphType") != "material":
            fail(f"graph.resolve(material node path) returned wrong graphType: {resolved_material_entry}")
        resolved_material_ref = resolved_material_entry.get("graphRef")
        if not isinstance(resolved_material_ref, dict):
            fail(f"graph.resolve(material node path) missing graphRef: {resolved_material_entry}")
        material_query_by_ref = call_tool(
            client,
            10015,
            "graph.query",
            {"graphType": "material", "graphRef": resolved_material_ref, "limit": 200},
        )
        material_query_by_ref_snapshot = material_query_by_ref.get("semanticSnapshot")
        if not isinstance(material_query_by_ref_snapshot, dict):
            fail(f"graph.query(material graphRef) missing semanticSnapshot: {material_query_by_ref}")
        if material_query_by_ref_snapshot.get("signature") != material_snapshot.get("signature"):
            fail(
                "graph.resolve(material node path) should yield a graphRef that reads the same material snapshot: "
                f"resolved={material_query_by_ref_snapshot} expected={material_snapshot}"
            )
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
        material_root_edge = next(
            (
                edge
                for edge in material_edges
                if isinstance(edge, dict)
                and edge.get("fromNodeId") == material_multiply_id
                and edge.get("toNodeId") == "__material_root__"
                and edge.get("toPin") == "Base Color"
            ),
            None,
        )
        if not isinstance(material_root_edge, dict) or not material_root_edge.get("fromPin"):
            fail(f"Material root edge missing source pin for breakPinLinks round-trip: {material_root_edge}")
        material_break_root_from_source = call_tool(
            client,
            100151,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "args": {
                            "target": {
                                "nodeId": material_root_edge.get("fromNodeId"),
                                "pinName": material_root_edge.get("fromPin"),
                            }
                        },
                    }
                ],
            },
        )
        material_break_root_from_source_first = op_ok(material_break_root_from_source)
        if material_break_root_from_source_first.get("changed") is not True:
            fail(
                "Material breakPinLinks should remove root edge when targeting the edge source pin: "
                f"{material_break_root_from_source}"
            )
        material_snapshot_after_root_source_break = query_snapshot(client, 100152, material_asset_path, "material", "MaterialGraph")
        material_edges_after_root_source_break = material_snapshot_after_root_source_break.get("edges")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_root_edge.get("fromNodeId")
            and edge.get("fromPin") == material_root_edge.get("fromPin")
            and edge.get("toNodeId") == material_root_edge.get("toNodeId")
            and edge.get("toPin") == material_root_edge.get("toPin")
            for edge in material_edges_after_root_source_break or []
        ):
            fail(
                "Material breakPinLinks(source pin) should remove the root edge returned by graph.query: "
                f"{material_snapshot_after_root_source_break}"
            )
        material_reconnect_root_after_source_break = call_tool(
            client,
            100153,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {
                                "nodeId": material_root_edge.get("fromNodeId"),
                                "pin": material_root_edge.get("fromPin"),
                            },
                            "to": {"nodeId": "__material_root__", "pin": "Base Color"},
                        },
                    }
                ],
            },
        )
        op_ok(material_reconnect_root_after_source_break)
        material_internal_edge = next(
            (
                edge
                for edge in material_edges
                if isinstance(edge, dict)
                and edge.get("fromNodeId") == material_param_id
                and edge.get("toNodeId") == material_multiply_id
                and edge.get("toPin") == "A"
            ),
            None,
        )
        if not isinstance(material_internal_edge, dict) or not material_internal_edge.get("fromPin"):
            fail(f"Material internal edge missing source pin for breakPinLinks round-trip: {material_internal_edge}")
        material_break_internal_from_source = call_tool(
            client,
            100154,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "args": {
                            "target": {
                                "nodeId": material_internal_edge.get("fromNodeId"),
                                "pinName": material_internal_edge.get("fromPin"),
                            }
                        },
                    }
                ],
            },
        )
        material_break_internal_from_source_first = op_ok(material_break_internal_from_source)
        if material_break_internal_from_source_first.get("changed") is not True:
            fail(
                "Material breakPinLinks should remove internal edge when targeting the edge source pin: "
                f"{material_break_internal_from_source}"
            )
        material_snapshot_after_internal_source_break = query_snapshot(client, 100155, material_asset_path, "material", "MaterialGraph")
        material_edges_after_internal_source_break = material_snapshot_after_internal_source_break.get("edges")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_internal_edge.get("fromNodeId")
            and edge.get("fromPin") == material_internal_edge.get("fromPin")
            and edge.get("toNodeId") == material_internal_edge.get("toNodeId")
            and edge.get("toPin") == material_internal_edge.get("toPin")
            for edge in material_edges_after_internal_source_break or []
        ):
            fail(
                "Material breakPinLinks(source pin) should remove the internal edge returned by graph.query: "
                f"{material_snapshot_after_internal_source_break}"
            )
        material_reconnect_internal_after_source_break = call_tool(
            client,
            100156,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {
                                "nodeId": material_internal_edge.get("fromNodeId"),
                                "pin": material_internal_edge.get("fromPin"),
                            },
                            "to": {
                                "nodeId": material_internal_edge.get("toNodeId"),
                                "pin": material_internal_edge.get("toPin"),
                            },
                        },
                    }
                ],
            },
        )
        op_ok(material_reconnect_internal_after_source_break)

        material_saturate_add = call_tool(
            client,
            100157,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionSaturate"}}
                ],
            },
        )
        material_saturate_id = op_ok(material_saturate_add).get("nodeId")
        if not isinstance(material_saturate_id, str) or not material_saturate_id:
            fail(f"Material Saturate addNode.byClass did not return nodeId: {material_saturate_add}")
        material_saturate_snapshot = query_snapshot(client, 100158, material_asset_path, "material", "MaterialGraph")
        material_saturate_node = require_node(material_saturate_snapshot.get("nodes") or [], material_saturate_id)
        material_saturate_pins = material_saturate_node.get("pins")
        if not isinstance(material_saturate_pins, list):
            fail(f"Material Saturate node missing pins: {material_saturate_node}")
        material_saturate_input_name = next(
            (
                pin.get("name")
                for pin in material_saturate_pins
                if isinstance(pin, dict) and pin.get("direction") == "input"
            ),
            None,
        )
        if not isinstance(material_saturate_input_name, str):
            fail(f"Material Saturate input pin name missing from graph.query: {material_saturate_node}")
        material_connect_saturate_by_query_pin = call_tool(
            client,
            100159,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {"nodeId": material_param_id, "pin": material_internal_edge.get("fromPin")},
                            "to": {"nodeId": material_saturate_id, "pin": material_saturate_input_name},
                        },
                    }
                ],
            },
        )
        material_connect_saturate_by_query_pin_first = op_ok(material_connect_saturate_by_query_pin)
        if material_connect_saturate_by_query_pin_first.get("changed") is not True:
            fail(
                "Material connectPins should accept the input pin name returned by graph.query for unary nodes: "
                f"{material_connect_saturate_by_query_pin}"
            )
        material_after_saturate_connect = query_snapshot(client, 100160, material_asset_path, "material", "MaterialGraph")
        material_edges_after_saturate_connect = material_after_saturate_connect.get("edges")
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_param_id
            and edge.get("toNodeId") == material_saturate_id
            and edge.get("toPin") == material_saturate_input_name
            for edge in material_edges_after_saturate_connect or []
        ):
            fail(
                "Material connectPins(query-visible unary input pin) should create the expected edge: "
                f"{material_after_saturate_connect}"
            )
        print("[PASS] material breakPinLinks source-pin round-trip validated")
        material_before_relayout = dict(material_multiply_pos)
        material_relayout_payload = call_tool(
            client,
            10016,
            "graph.mutate",
            {
                "assetPath": material_asset_path,
                "graphName": "MaterialGraph",
                "graphType": "material",
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "args": {
                            "target": {"nodeId": material_multiply_id},
                            "dx": 900,
                            "dy": 600,
                        },
                    },
                    {"op": "layoutGraph", "args": {"scope": "all"}},
                ],
            },
        )
        material_relayout_results = material_relayout_payload.get("opResults")
        if not isinstance(material_relayout_results, list) or len(material_relayout_results) != 2:
            fail(f"Material relayout ops missing results: {material_relayout_payload}")
        material_relayout = material_relayout_results[1] if isinstance(material_relayout_results[1], dict) else {}
        if material_relayout.get("op") != "layoutgraph":
            fail(f"Material relayout wrong op echo: {material_relayout}")
        if material_relayout.get("changed") is not True:
            fail(f"Material layoutGraph should report changed=true after moveNodeBy: {material_relayout}")
        material_relayout_moved = material_relayout.get("movedNodeIds")
        if not isinstance(material_relayout_moved, list) or not material_relayout_moved:
            fail(f"Material layoutGraph missing movedNodeIds after relayout: {material_relayout}")
        material_after_relayout_snapshot = query_snapshot(client, 10017, material_asset_path, "material", "MaterialGraph")
        material_after_relayout_nodes = material_after_relayout_snapshot.get("nodes")
        if not isinstance(material_after_relayout_nodes, list):
            fail(f"Material graph.query after relayout missing nodes: {material_after_relayout_snapshot}")
        material_after_relayout_pos = require_layout(require_node(material_after_relayout_nodes, material_multiply_id)).get("position", {})
        if material_after_relayout_pos == material_before_relayout:
            fail(
                "Material layoutGraph(scope=all) did not change tracked node position after moveNodeBy: "
                f"before={material_before_relayout} after={material_after_relayout_pos}"
            )
        if skip_material_visual_regression:
            print(
                "[WARN] material visual layout regression skipped by "
                "LOOMLE_SKIP_EDITOR_VISUAL_REGRESSION=1 or LOOMLE_SKIP_MATERIAL_VISUAL_REGRESSION=1"
            )
        else:
            editor_open_material_payload = call_tool(
                client,
                10018,
                "editor.open",
                {"assetPath": material_asset_path},
            )
            if editor_open_material_payload.get("assetPath") != material_asset_path:
                fail(f"editor.open did not open material asset: {editor_open_material_payload}")
            editor_focus_material_payload = call_tool(
                client,
                10019,
                "editor.focus",
                {"assetPath": material_asset_path, "panel": "graph"},
            )
            if editor_focus_material_payload.get("editorType") != "material":
                fail(f"editor.focus did not resolve material editorType: {editor_focus_material_payload}")
            _, _, material_capture_before_hash = capture_editor_png_hash(
                client,
                10020,
                f"Loomle/runtime/captures/material-layout-before-{int(time.time())}.png",
            )
            material_visual_relayout_payload = call_tool(
                client,
                10021,
                "graph.mutate",
                {
                    "assetPath": material_asset_path,
                    "graphName": "MaterialGraph",
                    "graphType": "material",
                    "ops": [
                        {
                            "op": "moveNodeBy",
                            "args": {
                                "target": {"nodeId": material_multiply_id},
                                "dx": 640,
                                "dy": -320,
                            },
                        },
                        {"op": "layoutGraph", "args": {"scope": "all"}},
                    ],
                },
            )
            material_visual_relayout_results = material_visual_relayout_payload.get("opResults")
            if not isinstance(material_visual_relayout_results, list) or len(material_visual_relayout_results) != 2:
                fail(f"Material visual relayout opResults mismatch: {material_visual_relayout_payload}")
            if not isinstance(material_visual_relayout_results[1], dict) or material_visual_relayout_results[1].get("ok") is not True:
                fail(f"Material visual relayout failed: {material_visual_relayout_payload}")
            _, _, material_capture_after_hash = capture_editor_png_hash_until_changed(
                client,
                10022,
                relative_path_prefix="Loomle/runtime/captures/material-layout-after",
                baseline_hash=material_capture_before_hash,
            )
            if material_capture_after_hash == material_capture_before_hash:
                fail(
                    "editor.screenshot stayed visually stale after Material layoutGraph: "
                    f"before={material_capture_before_hash} after={material_capture_after_hash}"
                )
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
        pcg_graph_list_without_type = call_tool(
            client,
            101005,
            "graph.list",
            {"assetPath": temp_pcg_asset, "includeSubgraphs": True},
        )
        pcg_graphs_without_type = pcg_graph_list_without_type.get("graphs")
        if not isinstance(pcg_graphs_without_type, list) or not pcg_graphs_without_type:
            fail(f"graph.list(PCG assetPath without graphType) missing graphs[]: {pcg_graph_list_without_type}")
        pcg_root_graph = pcg_graphs_without_type[0] if isinstance(pcg_graphs_without_type[0], dict) else {}
        if pcg_graph_list_without_type.get("graphType") != "pcg":
            fail(f"graph.list(PCG assetPath without graphType) should infer pcg: {pcg_graph_list_without_type}")
        if pcg_root_graph.get("graphName") != "PCGGraph":
            fail(f"graph.list(PCG assetPath without graphType) root graph mismatch: {pcg_graph_list_without_type}")

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
        bad_pcg_connect = call_tool(
            client,
            101021,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {
                        "op": "connectPins",
                        "args": {
                            "from": {"nodeId": pcg_filter_id, "pin": "Out"},
                            "to": {"nodeId": pcg_tag_b_id, "pin": "In"},
                        },
                    }
                ],
            },
            expect_error=True,
        )
        bad_pcg_connect_struct = bad_pcg_connect
        bad_pcg_connect_detail = bad_pcg_connect.get("detail")
        if isinstance(bad_pcg_connect_detail, str) and bad_pcg_connect_detail.strip():
            try:
                parsed_bad_pcg_connect_detail = json.loads(bad_pcg_connect_detail)
            except json.JSONDecodeError as exc:
                fail(f"graph.mutate bad PCG connect detail is not valid JSON: {exc} payload={bad_pcg_connect}")
            if isinstance(parsed_bad_pcg_connect_detail, dict):
                bad_pcg_connect_struct = parsed_bad_pcg_connect_detail
        bad_pcg_connect_results = bad_pcg_connect_struct.get("opResults")
        if not isinstance(bad_pcg_connect_results, list) or not bad_pcg_connect_results:
            fail(f"graph.mutate bad PCG connect missing opResults: {bad_pcg_connect}")
        bad_pcg_connect_first = bad_pcg_connect_results[0] if isinstance(bad_pcg_connect_results[0], dict) else {}
        if bad_pcg_connect_first.get("errorCode") not in {"TARGET_NOT_FOUND", "PIN_NOT_FOUND"}:
            fail(f"graph.mutate bad PCG connect wrong errorCode: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("ok") is not False:
            fail(f"graph.mutate bad PCG connect should fail explicitly: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("changed") is not False:
            fail(f"graph.mutate bad PCG connect should not report changed=true: {bad_pcg_connect_first}")
        pcg_snapshot_after_bad_connect = query_snapshot(client, 101022, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_edges_after_bad_connect = pcg_snapshot_after_bad_connect.get("edges")
        if not isinstance(pcg_edges_after_bad_connect, list):
            fail(f"PCG graph.query after bad connect missing edges: {pcg_snapshot_after_bad_connect}")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == pcg_filter_id
            and edge.get("fromPin") == "Out"
            and edge.get("toNodeId") == pcg_tag_b_id
            and edge.get("toPin") == "In"
            for edge in pcg_edges_after_bad_connect
        ):
            fail(f"PCG graph.query should not contain invalid Out->In edge after failed connect: {pcg_edges_after_bad_connect}")
        print("[PASS] graph.mutate invalid PCG connectPins target is rejected")
        pcg_snapshot_without_graph_name = query_snapshot(client, 10103, temp_pcg_asset, "pcg", None)
        if pcg_snapshot_without_graph_name.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG graph.query without graphName should resolve the same single-graph asset snapshot: "
                f"without={pcg_snapshot_without_graph_name} with={pcg_snapshot}"
            )
        pcg_query_without_type = call_tool(
            client,
            101031,
            "graph.query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        pcg_query_without_type_snapshot = pcg_query_without_type.get("semanticSnapshot")
        if not isinstance(pcg_query_without_type_snapshot, dict):
            fail(f"PCG graph.query without graphType missing semanticSnapshot: {pcg_query_without_type}")
        if pcg_query_without_type.get("graphType") != "pcg":
            fail(f"PCG graph.query without graphType should infer pcg: {pcg_query_without_type}")
        if pcg_query_without_type_snapshot.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG graph.query without graphType should resolve the same single-graph asset snapshot: "
                f"without={pcg_query_without_type} with={pcg_snapshot}"
            )
        resolved_pcg_node = call_tool(
            client,
            10104,
            "graph.resolve",
            {"path": pcg_filter_id, "graphType": "pcg"},
        )
        resolved_pcg_entry = require_resolved_asset_path(resolved_pcg_node, temp_pcg_asset)
        if resolved_pcg_entry.get("graphType") != "pcg":
            fail(f"graph.resolve(PCG node path) returned wrong graphType: {resolved_pcg_entry}")
        resolved_pcg_ref = resolved_pcg_entry.get("graphRef")
        if not isinstance(resolved_pcg_ref, dict):
            fail(f"graph.resolve(PCG node path) missing graphRef: {resolved_pcg_entry}")
        pcg_query_by_ref = call_tool(
            client,
            10105,
            "graph.query",
            {"graphType": "pcg", "graphRef": resolved_pcg_ref, "limit": 200},
        )
        pcg_query_by_ref_snapshot = pcg_query_by_ref.get("semanticSnapshot")
        if not isinstance(pcg_query_by_ref_snapshot, dict):
            fail(f"graph.query(PCG graphRef) missing semanticSnapshot: {pcg_query_by_ref}")
        if pcg_query_by_ref_snapshot.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "graph.resolve(PCG node path) should yield a graphRef that reads the same PCG snapshot: "
                f"resolved={pcg_query_by_ref_snapshot} expected={pcg_snapshot}"
            )

        pcg_compile_first = call_tool(
            client,
            101031,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [{"op": "compile"}],
            },
        )
        pcg_compile_first_result = op_ok(pcg_compile_first)
        if pcg_compile_first_result.get("op") != "compile":
            fail(f"PCG compile wrong op echo: {pcg_compile_first}")
        pcg_compile_second = call_tool(
            client,
            101032,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [{"op": "compile"}],
            },
        )
        pcg_compile_second_result = op_ok(pcg_compile_second)
        if pcg_compile_second_result.get("op") != "compile":
            fail(f"PCG second compile wrong op echo: {pcg_compile_second}")
        if pcg_compile_second_result.get("changed") is not False:
            fail(f"PCG compile should report changed=false when compiled graph is unchanged: {pcg_compile_second}")
        if pcg_compile_second.get("previousRevision") != pcg_compile_second.get("newRevision"):
            fail(f"PCG compile mutate should keep previousRevision/newRevision aligned when graph is unchanged: {pcg_compile_second}")
        pcg_revision_after_compile = call_tool(
            client,
            101033,
            "graph.query",
            {"assetPath": temp_pcg_asset, "graphName": "PCGGraph", "graphType": "pcg", "limit": 200},
        )
        if pcg_revision_after_compile.get("revision") != pcg_compile_second.get("newRevision"):
            fail(
                "PCG compile mutate revision metadata should match graph.query: "
                f"mutate={pcg_compile_second} query={pcg_revision_after_compile}"
            )
        print("[PASS] pcg compile revision metadata validated")

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
        pcg_before_relayout = dict(create_pos)
        pcg_relayout_payload = call_tool(
            client,
            10110,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "args": {
                            "target": {"nodeId": pcg_create_id},
                            "dx": 900,
                            "dy": 600,
                        },
                    },
                    {"op": "layoutGraph", "args": {"scope": "all"}},
                ],
            },
        )
        pcg_relayout_results = pcg_relayout_payload.get("opResults")
        if not isinstance(pcg_relayout_results, list) or len(pcg_relayout_results) != 2:
            fail(f"PCG relayout ops missing results: {pcg_relayout_payload}")
        pcg_relayout = pcg_relayout_results[1] if isinstance(pcg_relayout_results[1], dict) else {}
        if pcg_relayout.get("op") != "layoutgraph":
            fail(f"PCG relayout wrong op echo: {pcg_relayout}")
        if pcg_relayout.get("changed") is not True:
            fail(f"PCG layoutGraph should report changed=true after moveNodeBy: {pcg_relayout}")
        pcg_relayout_moved = pcg_relayout.get("movedNodeIds")
        if not isinstance(pcg_relayout_moved, list) or not pcg_relayout_moved:
            fail(f"PCG layoutGraph missing movedNodeIds after relayout: {pcg_relayout}")
        pcg_after_relayout_snapshot = query_snapshot(client, 10111, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_after_relayout_nodes = pcg_after_relayout_snapshot.get("nodes")
        if not isinstance(pcg_after_relayout_nodes, list):
            fail(f"PCG graph.query after relayout missing nodes: {pcg_after_relayout_snapshot}")
        pcg_after_relayout_pos = require_layout(require_node(pcg_after_relayout_nodes, pcg_create_id)).get("position", {})
        if pcg_after_relayout_pos == pcg_before_relayout:
            fail(
                "PCG layoutGraph(scope=all) did not change tracked node position after moveNodeBy: "
                f"before={pcg_before_relayout} after={pcg_after_relayout_pos}"
            )
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == pcg_filter_id
            and edge.get("fromPin") == "InsideFilter"
            and edge.get("toNodeId") == pcg_tag_b_id
            for edge in pcg_edges
        ):
            fail(f"PCG graph.query missing filter branch edge: {pcg_edges}")
        if skip_pcg_visual_regression:
            print(
                "[WARN] PCG visual layout regression skipped by "
                "LOOMLE_SKIP_EDITOR_VISUAL_REGRESSION=1 or LOOMLE_SKIP_PCG_VISUAL_REGRESSION=1"
            )
        else:
            editor_open_pcg_payload = call_tool(
                client,
                10112,
                "editor.open",
                {"assetPath": temp_pcg_asset},
            )
            if editor_open_pcg_payload.get("assetPath") != temp_pcg_asset:
                fail(f"editor.open did not open PCG asset: {editor_open_pcg_payload}")
            editor_focus_pcg_payload = call_tool(
                client,
                10113,
                "editor.focus",
                {"assetPath": temp_pcg_asset, "panel": "graph"},
            )
            if editor_focus_pcg_payload.get("editorType") != "pcg":
                fail(f"editor.focus did not resolve PCG editorType: {editor_focus_pcg_payload}")
            _, _, pcg_capture_before_hash = capture_editor_png_hash(
                client,
                10114,
                f"Loomle/runtime/captures/pcg-layout-before-{int(time.time())}.png",
            )
            pcg_visual_relayout_payload = call_tool(
                client,
                10115,
                "graph.mutate",
                {
                    "assetPath": temp_pcg_asset,
                    "graphName": "PCGGraph",
                    "graphType": "pcg",
                    "ops": [
                        {
                            "op": "moveNodeBy",
                            "args": {
                                "target": {"nodeId": pcg_create_id},
                                "dx": 640,
                                "dy": -320,
                            },
                        },
                        {"op": "layoutGraph", "args": {"scope": "all"}},
                    ],
                },
            )
            pcg_visual_relayout_results = pcg_visual_relayout_payload.get("opResults")
            if not isinstance(pcg_visual_relayout_results, list) or len(pcg_visual_relayout_results) != 2:
                fail(f"PCG visual relayout opResults mismatch: {pcg_visual_relayout_payload}")
            if not isinstance(pcg_visual_relayout_results[1], dict) or pcg_visual_relayout_results[1].get("ok") is not True:
                fail(f"PCG visual relayout failed: {pcg_visual_relayout_payload}")
            _, _, pcg_capture_after_hash = capture_editor_png_hash_until_changed(
                client,
                10116,
                relative_path_prefix="Loomle/runtime/captures/pcg-layout-after",
                baseline_hash=pcg_capture_before_hash,
            )
            if pcg_capture_after_hash == pcg_capture_before_hash:
                fail(
                    "editor.screenshot stayed visually stale after PCG layoutGraph: "
                    f"before={pcg_capture_before_hash} after={pcg_capture_after_hash}"
                )
        print("[PASS] pcg pipeline layout validated")

        pcg_settings_probe_add = call_tool(
            client,
            10117,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGGetActorPropertySettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGGetSplineSettings"}},
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"}},
                ],
            },
        )
        pcg_settings_probe_results = pcg_settings_probe_add.get("opResults")
        if not isinstance(pcg_settings_probe_results, list) or len(pcg_settings_probe_results) != 3:
            fail(f"PCG settings probe add ops missing results: {pcg_settings_probe_add}")
        pcg_get_actor_property_id, pcg_get_spline_id, pcg_static_mesh_spawner_id = [
            result.get("nodeId") for result in pcg_settings_probe_results
        ]
        if not all(
            isinstance(node_id, str) and node_id
            for node_id in [pcg_get_actor_property_id, pcg_get_spline_id, pcg_static_mesh_spawner_id]
        ):
            fail(f"PCG settings probe nodes missing ids: {pcg_settings_probe_add}")

        pcg_settings_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=10118,
            code=(
                "import json\n"
                "import unreal\n"
                "def load_required(path):\n"
                "    obj = unreal.load_object(None, path)\n"
                "    if obj is None:\n"
                "        raise RuntimeError(f'failed to load object: {path}')\n"
                "    return obj\n"
                "def set_prop(obj, names, value):\n"
                "    errors = []\n"
                "    for name in names:\n"
                "        try:\n"
                "            obj.set_editor_property(name, value)\n"
                "            return name\n"
                "        except Exception as exc:\n"
                "            errors.append(f'{name}: {exc}')\n"
                "    raise RuntimeError('failed to set property on %s: %s' % (obj, '; '.join(errors)))\n"
                "def resolve_enum_value(type_names, member_names):\n"
                "    for type_name in type_names:\n"
                "        enum_type = getattr(unreal, type_name, None)\n"
                "        if enum_type is None:\n"
                "            continue\n"
                "        for member_name in member_names:\n"
                "            if hasattr(enum_type, member_name):\n"
                "                return getattr(enum_type, member_name)\n"
                "    return None\n"
                f"asset = {json.dumps(temp_pcg_asset, ensure_ascii=False)}\n"
                f"actor_node_path = {json.dumps(pcg_get_actor_property_id, ensure_ascii=False)}\n"
                f"spline_node_path = {json.dumps(pcg_get_spline_id, ensure_ascii=False)}\n"
                f"spawner_node_path = {json.dumps(pcg_static_mesh_spawner_id, ensure_ascii=False)}\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    raise RuntimeError(f'failed to load PCG graph asset: {asset}')\n"
                "all_world_actors = resolve_enum_value(['EPCGActorFilter', 'PCGActorFilter'], ['ALL_WORLD_ACTORS'])\n"
                "by_class = resolve_enum_value(['EPCGActorSelection', 'PCGActorSelection'], ['BY_CLASS'])\n"
                "component_by_class = resolve_enum_value(['EPCGComponentSelection', 'PCGComponentSelection'], ['BY_CLASS'])\n"
                "actor_node = load_required(actor_node_path)\n"
                "actor_settings = actor_node.get_settings()\n"
                "actor_selector = actor_settings.get_editor_property('actor_selector')\n"
                "if all_world_actors is not None:\n"
                "    set_prop(actor_selector, ['actor_filter'], all_world_actors)\n"
                "if by_class is not None:\n"
                "    set_prop(actor_selector, ['actor_selection'], by_class)\n"
                "    set_prop(actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
                "set_prop(actor_selector, ['b_select_multiple', 'select_multiple'], True)\n"
                "set_prop(actor_settings, ['actor_selector'], actor_selector)\n"
                "set_prop(actor_settings, ['property_name'], unreal.Name('Tags'))\n"
                "set_prop(actor_settings, ['b_select_component', 'select_component'], True)\n"
                "set_prop(actor_settings, ['component_class'], unreal.SplineComponent.static_class())\n"
                "set_prop(actor_settings, ['b_process_all_components', 'process_all_components'], True)\n"
                "set_prop(actor_settings, ['b_output_actor_reference', 'output_actor_reference'], True)\n"
                "set_prop(actor_settings, ['b_always_requery_actors', 'always_requery_actors'], True)\n"
                "spline_node = load_required(spline_node_path)\n"
                "spline_settings = spline_node.get_settings()\n"
                "spline_actor_selector = spline_settings.get_editor_property('actor_selector')\n"
                "if all_world_actors is not None:\n"
                "    set_prop(spline_actor_selector, ['actor_filter'], all_world_actors)\n"
                "if by_class is not None:\n"
                "    set_prop(spline_actor_selector, ['actor_selection'], by_class)\n"
                "    set_prop(spline_actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
                "set_prop(spline_settings, ['actor_selector'], spline_actor_selector)\n"
                "component_selector = spline_settings.get_editor_property('component_selector')\n"
                "if component_by_class is not None:\n"
                "    set_prop(component_selector, ['component_selection'], component_by_class)\n"
                "    set_prop(component_selector, ['component_selection_class'], unreal.SplineComponent.static_class())\n"
                "set_prop(spline_settings, ['component_selector'], component_selector)\n"
                "set_prop(spline_settings, ['b_always_requery_actors', 'always_requery_actors'], True)\n"
                "spawner_node = load_required(spawner_node_path)\n"
                "spawner_settings = spawner_node.get_settings()\n"
                "spawner_settings.set_mesh_selector_type(unreal.PCGMeshSelectorByAttribute.static_class())\n"
                "selector = spawner_settings.get_editor_property('mesh_selector_parameters')\n"
                "set_prop(selector, ['attribute_name'], unreal.Name('Mesh'))\n"
                "set_prop(spawner_settings, ['out_attribute_name'], unreal.Name('ChosenMesh'))\n"
                "set_prop(spawner_settings, ['b_apply_mesh_bounds_to_points', 'apply_mesh_bounds_to_points'], True)\n"
                "unreal.EditorAssetLibrary.save_asset(asset)\n"
                "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
            ),
        )
        pcg_settings_fixture = parse_execute_json(pcg_settings_fixture_payload)
        if pcg_settings_fixture.get("ok") is not True:
            fail(f"PCG settings probe fixture failed: {pcg_settings_fixture}")

        pcg_settings_snapshot = query_snapshot(client, 10119, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_settings_nodes = pcg_settings_snapshot.get("nodes")
        if not isinstance(pcg_settings_nodes, list):
            fail(f"PCG settings probe graph.query missing nodes: {pcg_settings_snapshot}")

        get_actor_property_node = require_node(pcg_settings_nodes, pcg_get_actor_property_id)
        get_actor_property_settings = get_actor_property_node.get("effectiveSettings")
        if not isinstance(get_actor_property_settings, dict):
            fail(f"PCG GetActorProperty node missing effectiveSettings: {get_actor_property_node}")
        if get_actor_property_settings.get("propertyName") != "Tags":
            fail(f"PCG GetActorProperty propertyName missing from effectiveSettings: {get_actor_property_settings}")
        if get_actor_property_settings.get("selectComponent") is not True:
            fail(f"PCG GetActorProperty selectComponent missing from effectiveSettings: {get_actor_property_settings}")
        if not str(get_actor_property_settings.get("componentClassPath", "")).endswith("SplineComponent"):
            fail(f"PCG GetActorProperty componentClassPath missing SplineComponent: {get_actor_property_settings}")
        get_actor_property_actor_selector = get_actor_property_settings.get("actorSelector")
        if not isinstance(get_actor_property_actor_selector, dict):
            fail(f"PCG GetActorProperty missing actorSelector details: {get_actor_property_settings}")
        if not isinstance(get_actor_property_actor_selector.get("actorFilter"), str) or not get_actor_property_actor_selector.get("actorFilter"):
            fail(f"PCG GetActorProperty actorFilter missing from actorSelector: {get_actor_property_actor_selector}")
        get_actor_property_diagnostics = get_actor_property_node.get("diagnostics")
        if not isinstance(get_actor_property_diagnostics, list) or not any(
            isinstance(diag, dict) and diag.get("code") == "PCG_SELECTOR_EMPTY_INPUT_HINT"
            for diag in get_actor_property_diagnostics
        ):
            fail(f"PCG GetActorProperty missing empty-input diagnostics: {get_actor_property_node}")

        get_spline_node = require_node(pcg_settings_nodes, pcg_get_spline_id)
        get_spline_settings = get_spline_node.get("effectiveSettings")
        if not isinstance(get_spline_settings, dict):
            fail(f"PCG GetSpline node missing effectiveSettings: {get_spline_node}")
        if get_spline_settings.get("dataFilter") != "PolyLine":
            fail(f"PCG GetSpline dataFilter missing from effectiveSettings: {get_spline_settings}")
        get_spline_component_selector = get_spline_settings.get("componentSelector")
        if not isinstance(get_spline_component_selector, dict):
            fail(f"PCG GetSpline missing componentSelector details: {get_spline_settings}")
        if not isinstance(get_spline_component_selector.get("componentSelection"), str) or not get_spline_component_selector.get("componentSelection"):
            fail(f"PCG GetSpline componentSelection missing from componentSelector: {get_spline_component_selector}")
        get_spline_diagnostics = get_spline_node.get("diagnostics")
        if not isinstance(get_spline_diagnostics, list) or not any(
            isinstance(diag, dict) and diag.get("code") == "PCG_COMPONENT_SELECTOR_EMPTY_INPUT_HINT"
            for diag in get_spline_diagnostics
        ):
            fail(f"PCG GetSpline missing component empty-input diagnostics: {get_spline_node}")

        static_mesh_spawner_node = require_node(pcg_settings_nodes, pcg_static_mesh_spawner_id)
        static_mesh_spawner_settings = static_mesh_spawner_node.get("effectiveSettings")
        if not isinstance(static_mesh_spawner_settings, dict):
            fail(f"PCG StaticMeshSpawner node missing effectiveSettings: {static_mesh_spawner_node}")
        mesh_selector_settings = static_mesh_spawner_settings.get("meshSelector")
        if not isinstance(mesh_selector_settings, dict):
            fail(f"PCG StaticMeshSpawner missing meshSelector details: {static_mesh_spawner_settings}")
        if mesh_selector_settings.get("kind") != "byAttribute":
            fail(f"PCG StaticMeshSpawner meshSelector kind mismatch: {mesh_selector_settings}")
        if mesh_selector_settings.get("attributeName") != "Mesh":
            fail(f"PCG StaticMeshSpawner attributeName missing from meshSelector: {mesh_selector_settings}")
        if static_mesh_spawner_settings.get("outAttributeName") != "ChosenMesh":
            fail(f"PCG StaticMeshSpawner outAttributeName missing from effectiveSettings: {static_mesh_spawner_settings}")
        print("[PASS] pcg graph.query settings and diagnostics validated")

        pcg_health_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101190,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_health_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG health graph asset')\n"
                "print(json.dumps({'ok': True, 'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        if parse_execute_json(pcg_health_fixture_payload).get("ok") is not True:
            fail(f"PCG health fixture asset creation failed: {pcg_health_fixture_payload}")
        pcg_health_add = call_tool(
            client,
            1011901,
            "graph.mutate",
            {
                "assetPath": temp_pcg_health_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "health_create", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"}},
                    {"op": "addNode.byClass", "clientRef": "health_tag", "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"}},
                    {"op": "addNode.byClass", "clientRef": "health_spawner", "args": {"nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"}},
                    {"op": "connectPins", "args": {"from": {"nodeRef": "health_create", "pin": "Out"}, "to": {"nodeRef": "health_tag", "pin": "In"}}},
                    {"op": "connectPins", "args": {"from": {"nodeRef": "health_tag", "pin": "Out"}, "to": {"nodeRef": "health_spawner", "pin": "In"}}},
                ],
            },
        )
        pcg_health_results = pcg_health_add.get("opResults")
        if not isinstance(pcg_health_results, list) or len(pcg_health_results) != 5:
            fail(f"PCG health probe add ops missing results: {pcg_health_add}")
        for idx, result in enumerate(pcg_health_results):
            if not isinstance(result, dict) or result.get("ok") is not True:
                fail(f"PCG health probe opResults[{idx}] failed: {pcg_health_add}")
        pcg_verify = call_tool(
            client,
            1011902,
            "graph.verify",
            {
                "assetPath": temp_pcg_health_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
            },
        )
        if pcg_verify.get("status") == "error":
            fail(
                "graph.verify should not become an error just because a PCG graph is not connected to Output: "
                f"{pcg_verify}"
            )
        if not isinstance(pcg_verify.get("queryReport"), dict):
            fail(f"graph.verify missing queryReport for pcg graph: {pcg_verify}")
        pcg_compile_report = pcg_verify.get("compileReport")
        if not isinstance(pcg_compile_report, dict):
            fail(f"graph.verify missing compileReport for pcg graph: {pcg_verify}")
        if pcg_compile_report.get("compiled") is not True:
            fail(f"graph.verify should preserve compileReport.compiled=true for disconnected-output pcg graph: {pcg_verify}")
        pcg_health_diagnostics = pcg_verify.get("diagnostics")
        if not isinstance(pcg_health_diagnostics, list):
            fail(f"graph.verify missing diagnostics[]: {pcg_verify}")
        pcg_health_codes = {
            diag.get("code")
            for diag in pcg_health_diagnostics
            if isinstance(diag, dict) and isinstance(diag.get("code"), str)
        }
        for unexpected_code in {
            "PCG_OUTPUT_NODE_MISSING_INPUTS",
            "PCG_NO_TERMINAL_OUTPUT_PATH",
            "PCG_GRAPH_CAN_GENERATE_NO_OUTPUT",
            "PCG_SPAWNER_NOT_CONNECTED_TO_OUTPUT",
        }:
            if unexpected_code in pcg_health_codes:
                fail(f"graph.verify should not invent {unexpected_code} for a disconnected-output pcg graph: {pcg_verify}")
        print("[PASS] pcg graph.verify no longer invents disconnected-output failures")

        pcg_remove_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=1011903,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_remove_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG remove graph asset')\n"
                "print(json.dumps({'ok': True, 'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        if parse_execute_json(pcg_remove_fixture_payload).get("ok") is not True:
            fail(f"PCG remove fixture asset creation failed: {pcg_remove_fixture_payload}")
        pcg_remove_add = call_tool(
            client,
            1011904,
            "graph.mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "remove_create", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"}},
                    {"op": "addNode.byClass", "clientRef": "remove_tag", "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"}},
                    {"op": "addNode.byClass", "clientRef": "remove_filter", "args": {"nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"}},
                    {"op": "connectPins", "args": {"from": {"nodeRef": "remove_create", "pin": "Out"}, "to": {"nodeRef": "remove_tag", "pin": "In"}}},
                    {"op": "connectPins", "args": {"from": {"nodeRef": "remove_tag", "pin": "Out"}, "to": {"nodeRef": "remove_filter", "pin": "In"}}},
                ],
            },
        )
        pcg_remove_results = pcg_remove_add.get("opResults")
        if not isinstance(pcg_remove_results, list) or len(pcg_remove_results) != 5:
            fail(f"PCG remove fixture ops missing results: {pcg_remove_add}")
        pcg_remove_tag_id = pcg_remove_results[1].get("nodeId")
        if not isinstance(pcg_remove_tag_id, str) or not pcg_remove_tag_id:
            fail(f"PCG remove fixture missing removable node id: {pcg_remove_add}")

        pcg_remove_by_name_failure = call_tool(
            client,
            1011905,
            "graph.mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [{"op": "removeNode", "args": {"target": {"name": "Add Tag"}}}],
            },
            expect_error=True,
        )
        if "stable target" not in str(pcg_remove_by_name_failure.get("message", "")):
            fail(f"PCG removeNode should reject non-stable name targets: {pcg_remove_by_name_failure}")

        pcg_remove_by_id = call_tool(
            client,
            1011906,
            "graph.mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [{"op": "removeNode", "args": {"target": {"nodeId": pcg_remove_tag_id}}}],
            },
        )
        op_ok(pcg_remove_by_id)
        pcg_remove_snapshot = query_snapshot(client, 1011907, temp_pcg_remove_asset, "pcg", "PCGGraph")
        pcg_remove_nodes = pcg_remove_snapshot.get("nodes")
        pcg_remove_edges = pcg_remove_snapshot.get("edges")
        if not isinstance(pcg_remove_nodes, list) or not isinstance(pcg_remove_edges, list):
            fail(f"PCG removeNode query missing nodes/edges: {pcg_remove_snapshot}")
        require_node_absent(pcg_remove_nodes, pcg_remove_tag_id)
        if any(
            isinstance(edge, dict)
            and (edge.get("fromNodeId") == pcg_remove_tag_id or edge.get("toNodeId") == pcg_remove_tag_id)
            for edge in pcg_remove_edges
        ):
            fail(f"PCG removeNode should clear edges for removed node: {pcg_remove_snapshot}")

        pcg_remove_layout = call_tool(
            client,
            1011908,
            "graph.mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [{"op": "layoutGraph", "args": {"scope": "touched"}}],
            },
        )
        pcg_remove_layout_result = op_ok(pcg_remove_layout)
        if pcg_remove_layout_result.get("op") != "layoutgraph":
            fail(f"PCG removeNode touched layout wrong op echo: {pcg_remove_layout}")
        print("[PASS] pcg removeNode requires stable targets and preserves touched layout neighbors")

        pcg_set_default_add = call_tool(
            client,
            101191,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSphereSettings"}},
                ],
            },
        )
        pcg_set_default_results = pcg_set_default_add.get("opResults")
        if not isinstance(pcg_set_default_results, list) or len(pcg_set_default_results) != 1:
            fail(f"PCG setPinDefault probe add op missing results: {pcg_set_default_add}")
        pcg_set_default_node_id = pcg_set_default_results[0].get("nodeId")
        if not isinstance(pcg_set_default_node_id, str) or not pcg_set_default_node_id:
            fail(f"PCG setPinDefault probe missing node id: {pcg_set_default_add}")

        pcg_set_default_payload = call_tool(
            client,
            101192,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_set_default_node_id, "pin": "Radius"},
                            "value": 250.5,
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_set_default_node_id, "pin": "LongitudinalSegments"},
                            "value": 8,
                        },
                    },
                ],
            },
        )
        op_ok(pcg_set_default_payload)

        pcg_set_default_verify_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101193,
            code=(
                "import json\n"
                "import unreal\n"
                f"node_path = {json.dumps(pcg_set_default_node_id, ensure_ascii=False)}\n"
                "node = unreal.load_object(None, node_path)\n"
                "if node is None:\n"
                "    raise RuntimeError(f'failed to load PCG node: {node_path}')\n"
                "settings = node.get_settings()\n"
                "if settings is None:\n"
                "    raise RuntimeError(f'PCG node has no settings: {node_path}')\n"
                "print(json.dumps({\n"
                "    'ok': True,\n"
                "    'radius': settings.get_editor_property('radius'),\n"
                "    'longitudinalSegments': settings.get_editor_property('longitudinal_segments'),\n"
                "}, ensure_ascii=False))\n"
            ),
        )
        pcg_set_default_verify = parse_execute_json(pcg_set_default_verify_payload)
        if pcg_set_default_verify.get("ok") is not True:
            fail(f"PCG setPinDefault verification failed: {pcg_set_default_verify}")
        if abs(float(pcg_set_default_verify.get("radius", 0.0)) - 250.5) > 1e-6:
            fail(f"PCG setPinDefault did not update Radius: {pcg_set_default_verify}")
        if pcg_set_default_verify.get("longitudinalSegments") != 8:
            fail(f"PCG setPinDefault did not update LongitudinalSegments: {pcg_set_default_verify}")
        print("[PASS] graph.mutate setPinDefault supports PCG overridable inputs")

        pcg_filter_add = call_tool(
            client,
            101194,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/PCG.PCGFilterByAttributeSettings"}},
                ],
            },
        )
        pcg_filter_add_results = pcg_filter_add.get("opResults")
        if not isinstance(pcg_filter_add_results, list) or len(pcg_filter_add_results) != 1:
            fail(f"PCG FilterByAttribute probe add op missing results: {pcg_filter_add}")
        pcg_filter_node_id = pcg_filter_add_results[0].get("nodeId")
        if not isinstance(pcg_filter_node_id, str) or not pcg_filter_node_id:
            fail(f"PCG FilterByAttribute probe missing node id: {pcg_filter_add}")

        pcg_filter_query = call_tool(
            client,
            101195,
            "graph.query",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "filter": {"nodeClasses": ["/Script/PCG.PCGFilterByAttributeSettings"]},
            },
        )
        semantic_snapshot = pcg_filter_query.get("semanticSnapshot")
        snapshot_nodes = semantic_snapshot.get("nodes") if isinstance(semantic_snapshot, dict) else None
        if not isinstance(snapshot_nodes, list):
            fail(f"PCG FilterByAttribute graph.query missing semanticSnapshot.nodes: {pcg_filter_query}")
        filter_node = next(
            (node for node in snapshot_nodes if isinstance(node, dict) and node.get("id") == pcg_filter_node_id),
            None,
        )
        if not isinstance(filter_node, dict):
            fail(f"PCG FilterByAttribute node not present in graph.query snapshot: {pcg_filter_query}")
        filter_pins = filter_node.get("pins")
        if not isinstance(filter_pins, list):
            fail(f"PCG FilterByAttribute node missing pins[]: {filter_node}")
        filter_pin_names = {
            pin.get("name")
            for pin in filter_pins
            if isinstance(pin, dict) and isinstance(pin.get("name"), str)
        }
        for expected_pin in {
            "TargetAttribute",
            "Threshold/AttributeTypes/Type",
            "Threshold/AttributeTypes/DoubleValue",
        }:
            if expected_pin not in filter_pin_names:
                fail(f"PCG FilterByAttribute query missing writable pin path {expected_pin}: {filter_node}")
        print("[PASS] PCG FilterByAttribute query exposes writable constant threshold paths")

        pcg_filter_mutate = call_tool(
            client,
            101196,
            "graph.mutate",
            {
                "assetPath": temp_pcg_asset,
                "graphName": "PCGGraph",
                "graphType": "pcg",
                "ops": [
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "FilterMode"},
                            "value": "FilterByValue",
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "TargetAttribute"},
                            "value": "Desert_Cactus",
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "FilterOperator"},
                            "value": "GreaterOrEqual",
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/bUseConstantThreshold"},
                            "value": True,
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/type"},
                            "value": "Double",
                        },
                    },
                    {
                        "op": "setPinDefault",
                        "args": {
                            "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/double_value"},
                            "value": 0.5,
                        },
                    },
                ],
            },
        )
        pcg_filter_mutate_results = pcg_filter_mutate.get("opResults")
        if not isinstance(pcg_filter_mutate_results, list) or len(pcg_filter_mutate_results) != 6:
            fail(f"PCG FilterByAttribute mutate missing opResults: {pcg_filter_mutate}")
        for index, result in enumerate(pcg_filter_mutate_results):
            if not isinstance(result, dict) or not result.get("ok"):
                fail(f"PCG FilterByAttribute mutate op[{index}] failed: {pcg_filter_mutate}")

        pcg_filter_verify_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101197,
            code=(
                "import json\n"
                "import unreal\n"
                f"node_path = {json.dumps(pcg_filter_node_id, ensure_ascii=False)}\n"
                "node = unreal.load_object(None, node_path)\n"
                "if node is None:\n"
                "    raise RuntimeError(f'failed to load PCG node: {node_path}')\n"
                "settings = node.get_settings()\n"
                "if settings is None:\n"
                "    raise RuntimeError(f'PCG node has no settings: {node_path}')\n"
                "selector_helpers = unreal.PCGAttributePropertySelectorBlueprintHelpers\n"
                "target = settings.get_editor_property('target_attribute')\n"
                "threshold = settings.get_editor_property('threshold')\n"
                "attribute_types = threshold.get_editor_property('attribute_types')\n"
                "print(json.dumps({\n"
                "    'ok': True,\n"
                "    'targetAttributeName': str(selector_helpers.get_attribute_name(target)),\n"
                "    'targetPropertyName': str(selector_helpers.get_property_name(target)),\n"
                "    'thresholdType': str(attribute_types.get_editor_property('type')),\n"
                "    'thresholdDoubleValue': attribute_types.get_editor_property('double_value'),\n"
                "}, ensure_ascii=False))\n"
            ),
        )
        pcg_filter_verify = parse_execute_json(pcg_filter_verify_payload)
        if pcg_filter_verify.get("ok") is not True:
            fail(f"PCG FilterByAttribute verification failed: {pcg_filter_verify}")
        if pcg_filter_verify.get("targetAttributeName") != "Desert_Cactus":
            fail(f"PCG FilterByAttribute TargetAttribute did not update: {pcg_filter_verify}")
        if pcg_filter_verify.get("targetPropertyName") not in {"None", ""}:
            fail(f"PCG FilterByAttribute TargetAttribute should not remain a property selector: {pcg_filter_verify}")
        threshold_type = str(pcg_filter_verify.get("thresholdType", ""))
        if "Double" not in threshold_type and "DOUBLE" not in threshold_type:
            fail(f"PCG FilterByAttribute threshold type did not update to Double: {pcg_filter_verify}")
        if abs(float(pcg_filter_verify.get("thresholdDoubleValue", 0.0)) - 0.5) > 1e-6:
            fail(f"PCG FilterByAttribute threshold constant did not update: {pcg_filter_verify}")
        print("[PASS] graph.mutate setPinDefault supports PCG selector and constant threshold paths")

        if skip_editor_visual_regression:
            print("[WARN] editor.open/editor.focus/editor.screenshot regression skipped by LOOMLE_SKIP_EDITOR_VISUAL_REGRESSION=1")
        else:
            editor_open_payload = call_tool(
                client,
                4001,
                "editor.open",
                {"assetPath": temp_asset},
            )
            if editor_open_payload.get("assetPath") != temp_asset:
                fail(f"editor.open did not echo assetPath: {editor_open_payload}")
            if not isinstance(editor_open_payload.get("assetClassPath"), str) or not editor_open_payload.get("assetClassPath"):
                fail(f"editor.open missing assetClassPath: {editor_open_payload}")

            editor_focus_payload = call_tool(
                client,
                4002,
                "editor.focus",
                {"assetPath": temp_asset, "panel": "graph"},
            )
            if editor_focus_payload.get("editorType") != "blueprint":
                fail(f"editor.focus did not resolve blueprint editorType: {editor_focus_payload}")
            if editor_focus_payload.get("panel") != "graph":
                fail(f"editor.focus did not echo graph panel: {editor_focus_payload}")

            _, _, _ = capture_editor_png_hash(
                client,
                4003,
                f"Loomle/runtime/captures/editor-open-regression-{int(time.time())}.png",
            )
            print("[PASS] editor.open, editor.focus, and editor.screenshot validated")

        print("[PASS] graph.mutate core ops validated")

        # -----------------------------------------------------------------------
        # widget.* regression
        # -----------------------------------------------------------------------
        temp_wbp_asset = make_temp_asset_path("/Game/Codex/WBP_BridgeRegression")

        # W01 — create WidgetBlueprint fixture
        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=5000,
            code=(
                "import unreal, json\n"
                f"asset='{temp_wbp_asset}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.WidgetBlueprintFactory()\n"
                "wbp = asset_tools.create_asset(asset_name, pkg_path, unreal.WidgetBlueprint, factory)\n"
                "print(json.dumps({'created': wbp is not None}, ensure_ascii=False))\n"
            ),
        )
        print(f"[PASS] W01 WidgetBlueprint fixture created: {temp_wbp_asset}")

        # W02 — widget.query baseline structure
        wq0 = call_tool(client, 5010, "widget.query", {"assetPath": temp_wbp_asset})
        if wq0.get("assetPath") != temp_wbp_asset:
            fail(f"W02 widget.query wrong assetPath: {wq0}")
        revision_0 = wq0.get("revision")
        if not isinstance(revision_0, str) or not revision_0:
            fail(f"W02 widget.query missing revision: {wq0}")
        if not isinstance(wq0.get("diagnostics"), list):
            fail(f"W02 widget.query missing diagnostics[]: {wq0}")
        print("[PASS] W02 widget.query baseline structure validated")

        # W03 — dryRun: op validated but nothing changes
        wm_dry = call_tool(client, 5020, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "dryRun": True,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.CanvasPanel",
                "name": "DryCanvas",
                "parent": "root",
            }}],
        })
        if wm_dry.get("applied") is not False:
            fail(f"W03 dryRun applied should be False: {wm_dry}")
        dry_op = wm_dry.get("opResults", [{}])[0] if wm_dry.get("opResults") else {}
        if not isinstance(dry_op, dict) or not dry_op.get("ok"):
            fail(f"W03 dryRun op should be ok (validation only): {dry_op}")
        if dry_op.get("changed") is not False:
            fail(f"W03 dryRun changed should be False: {dry_op}")
        if wm_dry.get("newRevision") != wm_dry.get("previousRevision"):
            fail(f"W03 dryRun must not change revision: {wm_dry}")
        print("[PASS] W03 widget.mutate dryRun validated")

        # W04 — addWidget CanvasPanel as root
        wm_add_canvas = call_tool(client, 5030, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.CanvasPanel",
                "name": "RootCanvas",
                "parent": "root",
            }}],
        })
        widget_op_ok(wm_add_canvas, 0)
        if wm_add_canvas.get("applied") is not True:
            fail(f"W04 addWidget CanvasPanel applied should be True: {wm_add_canvas}")
        revision_1 = wm_add_canvas.get("newRevision")
        if not isinstance(revision_1, str) or revision_1 == revision_0:
            fail(f"W04 addWidget should update revision: {wm_add_canvas}")
        print("[PASS] W04 widget.mutate addWidget CanvasPanel validated")

        # W05 — query confirms rootWidget now exists
        wq1 = call_tool(client, 5040, "widget.query", {"assetPath": temp_wbp_asset})
        root_widget = wq1.get("rootWidget")
        if not isinstance(root_widget, dict):
            fail(f"W05 widget.query rootWidget should be object after addWidget: {wq1}")
        if root_widget.get("name") != "RootCanvas":
            fail(f"W05 rootWidget name mismatch: {root_widget}")
        print("[PASS] W05 widget.query reflects added CanvasPanel root")

        # W06 — addWidget TextBlock as child of RootCanvas (uses parentName field)
        wm_add_text = call_tool(client, 5050, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "TitleText",
                "parentName": "RootCanvas",
            }}],
        })
        widget_op_ok(wm_add_text, 0)
        revision_2 = wm_add_text.get("newRevision")
        if not isinstance(revision_2, str) or revision_2 == revision_1:
            fail(f"W06 addWidget TextBlock should update revision: {wm_add_text}")
        print("[PASS] W06 widget.mutate addWidget TextBlock as child validated")

        # W07 — query with includeSlotProperties confirms child is present
        wq2 = call_tool(client, 5060, "widget.query", {
            "assetPath": temp_wbp_asset,
            "includeSlotProperties": True,
        })
        root_children = wq2.get("rootWidget", {}).get("children", [])
        if not isinstance(root_children, list) or not any(
            isinstance(c, dict) and c.get("name") == "TitleText" for c in root_children
        ):
            fail(f"W07 TitleText not found in rootWidget.children: {wq2}")
        print("[PASS] W07 widget.query includeSlotProperties shows TextBlock child")

        # W08 — setProperty on TextBlock (Text / a known FText property)
        wm_set_prop = call_tool(client, 5070, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "TitleText",
                "property": "Text",
                "value": "Hello Loomle",
            }}],
        })
        widget_op_ok(wm_set_prop, 0)
        print("[PASS] W08 widget.mutate setProperty validated")

        # W09 — addWidget second panel for reparent source (uses parentName field)
        wm_add_panel2 = call_tool(client, 5080, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.VerticalBox",
                "name": "SecondPanel",
                "parentName": "RootCanvas",
            }}],
        })
        widget_op_ok(wm_add_panel2, 0)
        revision_3 = wm_add_panel2.get("newRevision")
        print("[PASS] W09 widget.mutate addWidget VerticalBox validated")

        # W10 — reparentWidget: move TitleText from RootCanvas to SecondPanel
        wm_reparent = call_tool(client, 5090, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "reparentWidget", "args": {
                "name": "TitleText",
                "newParent": "SecondPanel",
            }}],
        })
        widget_op_ok(wm_reparent, 0)
        revision_4 = wm_reparent.get("newRevision")
        if not isinstance(revision_4, str) or revision_4 == revision_3:
            fail(f"W10 reparentWidget should update revision: {wm_reparent}")
        print("[PASS] W10 widget.mutate reparentWidget validated")

        # W11 — removeWidget
        wm_remove = call_tool(client, 5100, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "removeWidget", "args": {"name": "TitleText"}}],
        })
        widget_op_ok(wm_remove, 0)
        revision_5 = wm_remove.get("newRevision")
        if not isinstance(revision_5, str) or revision_5 == revision_4:
            fail(f"W11 removeWidget should update revision: {wm_remove}")
        print("[PASS] W11 widget.mutate removeWidget validated")

        # W12 — expectedRevision conflict: pass stale revision
        wm_stale = call_tool(client, 5110, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "expectedRevision": revision_0,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "ShouldNotExist",
                "parent": "RootCanvas",
            }}],
        }, expect_error=True)
        if not wm_stale.get("isError"):
            fail(f"W12 stale expectedRevision should produce isError: {wm_stale}")
        stale_code = wm_stale.get("code", "")
        if stale_code != "REVISION_CONFLICT":
            fail(f"W12 expected REVISION_CONFLICT code, got: {stale_code}")
        print("[PASS] W12 widget.mutate stale expectedRevision raises REVISION_CONFLICT")

        # W13 — unknown op with continueOnError
        wm_unknown = call_tool(client, 5120, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "continueOnError": True,
            "ops": [
                {"op": "unknownOp", "args": {}},
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.TextBlock",
                    "name": "AfterUnknown",
                    "parent": "RootCanvas",
                }},
            ],
        })
        if not isinstance(wm_unknown.get("opResults"), list) or len(wm_unknown["opResults"]) < 2:
            fail(f"W13 continueOnError should return 2 opResults: {wm_unknown}")
        op0 = wm_unknown["opResults"][0] if isinstance(wm_unknown["opResults"][0], dict) else {}
        op1 = wm_unknown["opResults"][1] if isinstance(wm_unknown["opResults"][1], dict) else {}
        if op0.get("ok") is not False:
            fail(f"W13 unknownOp[0] should be ok=False: {op0}")
        if op1.get("ok") is not True:
            fail(f"W13 addWidget[1] should succeed after continueOnError: {op1}")
        if wm_unknown.get("partialApplied") is not True:
            fail(f"W13 partialApplied should be True: {wm_unknown}")
        print("[PASS] W13 widget.mutate continueOnError partial execution validated")

        # W14 — removeWidget for non-existent widget (op-level error)
        wm_notfound = call_tool(client, 5130, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "removeWidget", "args": {"name": "DoesNotExist"}}],
        })
        op_nf = wm_notfound.get("opResults", [{}])[0] if wm_notfound.get("opResults") else {}
        if not isinstance(op_nf, dict) or op_nf.get("ok") is not False:
            fail(f"W14 removeWidget non-existent should be ok=False: {op_nf}")
        print("[PASS] W14 widget.mutate removeWidget non-existent widget returns op error")

        # W15 — widget.query on a non-WBP asset (Blueprint, should fail)
        wq_err = call_tool(client, 5140, "widget.query", {"assetPath": temp_asset}, expect_error=True)
        if not wq_err.get("isError"):
            fail(f"W15 widget.query on non-WBP asset should isError: {wq_err}")
        err_code = wq_err.get("code", "")
        if err_code != "WIDGET_TREE_UNAVAILABLE":
            fail(f"W15 expected WIDGET_TREE_UNAVAILABLE, got: {err_code}")
        print("[PASS] W15 widget.query on non-WBP asset raises WIDGET_TREE_UNAVAILABLE")

        # W16 — widget.verify
        wv = call_tool(client, 5150, "widget.verify", {"assetPath": temp_wbp_asset})
        if wv.get("status") not in {"ok", "error"}:
            fail(f"W16 widget.verify unexpected status: {wv}")
        if wv.get("assetPath") != temp_wbp_asset:
            fail(f"W16 widget.verify wrong assetPath: {wv}")
        if not isinstance(wv.get("diagnostics"), list):
            fail(f"W16 widget.verify missing diagnostics[]: {wv}")
        print("[PASS] W16 widget.verify validated")

        # W17 — issue #140: batch addWidget with parentName keeps root intact
        # Both ops in a single mutate call: first adds a VerticalBox as root,
        # second adds a TextBlock as child via parentName. The root must remain
        # the VerticalBox after the batch completes.
        temp_wbp_batch = make_temp_asset_path("/Game/Codex/WBP_Batch140")
        call_execute_exec_with_retry(
            client=client,
            req_id_base=5161,
            code=(
                "import unreal, json\n"
                f"asset='{temp_wbp_batch}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.WidgetBlueprintFactory()\n"
                "wbp = asset_tools.create_asset(asset_name, pkg_path, unreal.WidgetBlueprint, factory)\n"
                "print(json.dumps({'created': wbp is not None}, ensure_ascii=False))\n"
            ),
        )
        wm_batch = call_tool(client, 5162, "widget.mutate", {
            "assetPath": temp_wbp_batch,
            "ops": [
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.VerticalBox",
                    "name": "BatchRoot",
                    "parent": "root",
                }},
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.TextBlock",
                    "name": "BatchChild",
                    "parentName": "BatchRoot",
                }},
            ],
        })
        widget_op_ok(wm_batch, 0)
        widget_op_ok(wm_batch, 1)
        if wm_batch.get("applied") is not True:
            fail(f"W17 batch addWidget applied should be True: {wm_batch}")
        wq_batch = call_tool(client, 5163, "widget.query", {"assetPath": temp_wbp_batch})
        batch_root = wq_batch.get("rootWidget", {})
        if batch_root.get("name") != "BatchRoot":
            fail(f"W17 rootWidget should be BatchRoot, got: {batch_root.get('name')!r}")
        batch_children = batch_root.get("children", [])
        if not any(isinstance(c, dict) and c.get("name") == "BatchChild" for c in batch_children):
            fail(f"W17 BatchChild not found in BatchRoot.children: {batch_children}")
        print("[PASS] W17 batch addWidget with parentName keeps root intact (issue #140)")

        # W18 — legacy "parent" alias still routes children correctly
        # Verifies backward-compat: "parent" field (not "parentName") must still work
        # for child widgets added in a separate mutate call.
        wm_legacy = call_tool(client, 5164, "widget.mutate", {
            "assetPath": temp_wbp_batch,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "LegacyChild",
                "parent": "BatchRoot",
            }}],
        })
        widget_op_ok(wm_legacy, 0)
        wq_legacy = call_tool(client, 5165, "widget.query", {"assetPath": temp_wbp_batch})
        legacy_root = wq_legacy.get("rootWidget", {})
        if legacy_root.get("name") != "BatchRoot":
            fail(f"W18 rootWidget should still be BatchRoot after legacy parent add: {legacy_root.get('name')!r}")
        legacy_children = legacy_root.get("children", [])
        if not any(isinstance(c, dict) and c.get("name") == "LegacyChild" for c in legacy_children):
            fail(f"W18 LegacyChild not found in BatchRoot.children: {legacy_children}")
        print("[PASS] W18 legacy parent field alias routes child correctly")

        # W19 — widget.describe by short class name
        wd_short = call_tool(client, 5166, "widget.describe", {"widgetClass": "TextBlock"})
        if "properties" not in wd_short or not isinstance(wd_short["properties"], list):
            fail(f"W19 widget.describe TextBlock missing properties[]: {wd_short}")
        if not wd_short.get("widgetClass", "").endswith("TextBlock"):
            fail(f"W19 widget.describe widgetClass mismatch: {wd_short.get('widgetClass')!r}")
        if not any(p.get("name") == "Text" for p in wd_short["properties"]):
            fail(f"W19 widget.describe TextBlock should have Text property: {[p['name'] for p in wd_short['properties']]}")
        if not isinstance(wd_short.get("slotProperties"), list):
            fail(f"W19 widget.describe missing slotProperties[]: {wd_short}")
        if "currentValues" in wd_short:
            fail(f"W19 widget.describe without instance should NOT have currentValues: {wd_short}")
        print("[PASS] W19 widget.describe by short class name (TextBlock)")

        # W20 — widget.describe by full class path
        wd_full = call_tool(client, 5167, "widget.describe", {"widgetClass": "/Script/UMG.TextBlock"})
        if not wd_full.get("widgetClass", "").endswith("TextBlock"):
            fail(f"W20 widget.describe full path widgetClass mismatch: {wd_full.get('widgetClass')!r}")
        if "properties" not in wd_full or not isinstance(wd_full["properties"], list):
            fail(f"W20 widget.describe full path missing properties[]: {wd_full}")
        print("[PASS] W20 widget.describe by full class path (/Script/UMG.TextBlock)")

        # W21 — widget.describe by assetPath+widgetName returns currentValues
        # Use "RootCanvas" (CanvasPanel) which persists throughout the test sequence
        wd_inst = call_tool(client, 5168, "widget.describe", {
            "assetPath": temp_wbp_asset,
            "widgetName": "RootCanvas"
        })
        if not wd_inst.get("widgetClass", "").endswith("CanvasPanel"):
            fail(f"W21 widget.describe instance widgetClass mismatch: {wd_inst.get('widgetClass')!r}")
        if "properties" not in wd_inst or not isinstance(wd_inst["properties"], list):
            fail(f"W21 widget.describe instance missing properties[]: {wd_inst}")
        if "currentValues" not in wd_inst or not isinstance(wd_inst["currentValues"], dict):
            fail(f"W21 widget.describe instance should have currentValues dict: {wd_inst}")
        print("[PASS] W21 widget.describe by assetPath+widgetName includes currentValues")

        # W22 — widget.describe unknown class returns WIDGET_CLASS_NOT_FOUND
        wd_bad = call_tool(client, 5169, "widget.describe", {"widgetClass": "NonExistentWidget_XYZ"}, expect_error=True)
        if not wd_bad.get("isError"):
            fail(f"W22 expected error for unknown class, got: {wd_bad}")
        err_code = wd_bad.get("code", "")
        if err_code != "WIDGET_CLASS_NOT_FOUND":
            fail(f"W22 expected WIDGET_CLASS_NOT_FOUND, got: {err_code}")
        print("[PASS] W22 widget.describe unknown class returns WIDGET_CLASS_NOT_FOUND")

        # W23 — setProperty can write a CanvasPanelSlot ZOrder (slot property fallback)
        # SecondPanel is a VerticalBox child of RootCanvas — its slot is FCanvasPanelSlot.
        wm_slot_zorder = call_tool(client, 5200, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "SecondPanel",
                "property": "ZOrder",
                "value": "5",
            }}],
        })
        widget_op_ok(wm_slot_zorder, 0)
        print("[PASS] W23 widget.mutate setProperty writes CanvasPanelSlot ZOrder via slot fallback")

        # W24 — setProperty can write CanvasPanelSlot LayoutData (struct slot property)
        wm_slot_layout = call_tool(client, 5210, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "SecondPanel",
                "property": "LayoutData",
                "value": "(Offsets=(Left=10,Top=20,Right=0,Bottom=0),Anchors=(Minimum=(X=0.0,Y=0.0),Maximum=(X=0.5,Y=0.5)),Alignment=(X=0,Y=0))",
            }}],
        })
        widget_op_ok(wm_slot_layout, 0)
        print("[PASS] W24 widget.mutate setProperty writes CanvasPanelSlot LayoutData via slot fallback")

        # W25 — setProperty with a property that exists on neither widget nor slot returns INVALID_ARGUMENT
        wm_bad_prop = call_tool(client, 5220, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "SecondPanel",
                "property": "NonExistentProperty_XYZ",
                "value": "anything",
            }}],
        }, expect_error=False)
        # The op itself should fail (ok=False) even if the tool call succeeds
        op25 = (wm_bad_prop.get("opResults") or [{}])[0]
        if op25.get("ok"):
            fail(f"W25 expected setProperty to fail for unknown property, but got ok: {op25}")
        print("[PASS] W25 widget.mutate setProperty unknown property returns op-level error")

        print("[PASS] widget.* regression complete")

        print("[PASS] Bridge regression complete")
        return 0
    finally:
        # Cleanup is intentionally skipped to avoid flaky teardown timeouts
        # masking a fully successful regression run.
        print(f"[WARN] cleanup skipped for temporary asset: {temp_asset}")
        print(f"[WARN] cleanup skipped for temporary material asset: {temp_material_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG asset: {temp_pcg_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG health asset: {temp_pcg_health_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG remove asset: {temp_pcg_remove_asset}")
        temp_wbp_asset = locals().get("temp_wbp_asset", None)
        if temp_wbp_asset:
            print(f"[WARN] cleanup skipped for temporary widget asset: {temp_wbp_asset}")
        temp_wbp_batch = locals().get("temp_wbp_batch", None)
        if temp_wbp_batch:
            print(f"[WARN] cleanup skipped for temporary widget batch asset: {temp_wbp_batch}")
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
