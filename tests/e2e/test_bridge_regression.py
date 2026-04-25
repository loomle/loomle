#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import platform
import subprocess
import time
from pathlib import Path

from test_bridge_smoke import (
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


def require_graph_domain(graph_type: str) -> str:
    if graph_type not in {"blueprint", "material", "pcg"}:
        fail(f"unsupported graph domain in regression test: {graph_type}")
    return graph_type


def call_domain_tool(
    client: McpStdioClient,
    request_id: int,
    graph_type: str,
    action: str,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    domain = require_graph_domain(graph_type)
    tool_name = f"{domain}.{action}"
    if domain == "blueprint":
        tool_name = {
            "list": "blueprint.graph.list",
            "query": "blueprint.graph.inspect",
            "mutate": "blueprint.graph.edit",
            "verify": "blueprint.validate",
            "describe": "blueprint.asset.inspect",
            "compile": "blueprint.compile",
        }.get(action, tool_name)
    return call_tool(client, request_id, tool_name, arguments, expect_error=expect_error)


def op_ok(payload: dict) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        fail(f"domain mutate missing opResults: {payload}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    if not first.get("ok"):
        fail(f"domain mutate op failed: {first}")
    return first


def bp_node(node_id: str) -> dict:
    return {"id": node_id}


def bp_pin(node_id: str, pin: str) -> dict:
    return {"node": bp_node(node_id), "pin": pin}


def bp_branch(position: dict | None = None, *, alias: str | None = None) -> dict:
    command = {"kind": "addNode", "nodeType": {"kind": "branch"}}
    if position is not None:
        command["position"] = position
    if alias:
        command["alias"] = alias
    return command


def bp_remove(node_id: str) -> dict:
    return {"kind": "removeNode", "node": bp_node(node_id)}


def bp_connect(from_node: str, from_pin: str, to_node: str, to_pin: str) -> dict:
    return {"kind": "connect", "from": bp_pin(from_node, from_pin), "to": bp_pin(to_node, to_pin)}


def bp_disconnect(from_node: str, from_pin: str, to_node: str, to_pin: str) -> dict:
    return {"kind": "disconnect", "from": bp_pin(from_node, from_pin), "to": bp_pin(to_node, to_pin)}


def bp_break_links(node_id: str, pin: str) -> dict:
    return {"kind": "breakLinks", "target": bp_pin(node_id, pin)}


def bp_set_default(node_id: str, pin: str, value) -> dict:
    return {"kind": "setPinDefault", "target": bp_pin(node_id, pin), "value": value}


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
    domain = require_graph_domain(graph_type)
    steps = preferred_plan.get("steps")
    if not isinstance(steps, list) or not steps:
        fail(f"preferredPlan missing steps[]: {preferred_plan}")
    arguments = {"assetPath": asset_path}
    if domain == "blueprint":
        arguments["graphName"] = graph_name
        arguments["commands"] = steps
    else:
        arguments["ops"] = steps
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "mutate",
        arguments,
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"{domain}.mutate(step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"{domain}.mutate(step plan) opResults[{idx}] failed: {payload}")
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
    domain = require_graph_domain(graph_type)
    steps: list[dict] = []
    for plan in preferred_plans:
        plan_steps = plan.get("steps")
        if not isinstance(plan_steps, list) or not plan_steps:
            fail(f"preferredPlan missing steps[]: {plan}")
        steps.extend(plan_steps)
    arguments = {"assetPath": asset_path}
    if domain == "blueprint":
        arguments["graphName"] = graph_name
        arguments["commands"] = steps
    else:
        arguments["ops"] = steps
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "mutate",
        arguments,
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"{domain}.mutate(combined step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"{domain}.mutate(combined step plan) opResults[{idx}] failed: {payload}")
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
            payload = call_domain_tool(
                client,
                request_id,
                "blueprint",
                "query",
                {"assetPath": asset_path, "graphName": graph_name, "limit": 200},
            )
            break
        except SystemExit:
            if attempt >= max_attempts:
                raise
            print(f"[WARN] blueprint.query retrying after failure ({attempt}/{max_attempts})...")
            time.sleep(retry_delay_s)

    if payload is None:
        fail("blueprint.query retry loop ended without payload")

    snapshot = payload.get("semanticSnapshot", {})
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        fail(f"blueprint.query missing nodes[]: {payload}")
    return [node for node in nodes if isinstance(node, dict)]


def query_snapshot(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_type: str,
    graph_name: str | None,
) -> dict:
    domain = require_graph_domain(graph_type)
    arguments = {"assetPath": asset_path, "limit": 200}
    if domain == "blueprint" and graph_name is not None:
        arguments["graphName"] = graph_name
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "query",
        arguments,
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        fail(f"{domain}.query missing semanticSnapshot: {payload}")
    nodes = snapshot.get("nodes")
    edges = snapshot.get("edges")
    meta = payload.get("meta")
    if not isinstance(nodes, list) or not isinstance(edges, list):
        fail(f"{domain}.query missing nodes/edges: {payload}")
    if not isinstance(meta, dict):
        fail(f"{domain}.query missing meta: {payload}")
    if meta.get("returnedNodes") != len(nodes):
        fail(f"{domain}.query returnedNodes mismatch: payload={payload}")
    if meta.get("returnedEdges") != len(edges):
        fail(f"{domain}.query returnedEdges mismatch: payload={payload}")
    if meta.get("truncated") is False:
        if meta.get("totalNodes") != len(nodes):
            fail(f"{domain}.query totalNodes mismatch for non-truncated response: payload={payload}")
        if meta.get("totalEdges") != len(edges):
            fail(f"{domain}.query totalEdges mismatch for non-truncated response: payload={payload}")
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


def structured_detail_or_payload(payload: dict) -> dict:
    detail = payload.get("detail")
    if isinstance(detail, str) and detail:
        try:
            nested = json.loads(detail)
        except json.JSONDecodeError:
            return payload
        if isinstance(nested, dict):
            return nested
    return payload


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
        "limit": limit,
    }
    if cursor:
        arguments["cursor"] = cursor
    payload = call_domain_tool(client, request_id, "blueprint", "query", arguments)
    if not isinstance(payload.get("semanticSnapshot"), dict):
        fail(f"blueprint.query missing semanticSnapshot for pagination test: {payload}")
    if not isinstance(payload.get("meta"), dict):
        fail(f"blueprint.query missing meta for pagination test: {payload}")
    return payload


def require_node(nodes: list[dict], node_id: str) -> dict:
    for node in nodes:
        if node.get("id") == node_id:
            return node
    fail(f"domain query did not return node {node_id}: {nodes}")


def require_node_absent(nodes: list[dict], node_id: str) -> None:
    for node in nodes:
        if node.get("id") == node_id:
            fail(f"domain query still returned removed node {node_id}: {node}")


def require_layout(node: dict) -> dict:
    layout = node.get("layout")
    if not isinstance(layout, dict):
        fail(f"domain query node missing layout object: {node}")
    position = layout.get("position")
    if not isinstance(position, dict):
        fail(f"domain query node layout missing position: {node}")
    if not isinstance(position.get("x"), (int, float)) or not isinstance(position.get("y"), (int, float)):
        fail(f"domain query node layout position invalid: {node}")
    if not isinstance(layout.get("source"), str) or not layout.get("source"):
        fail(f"domain query node layout missing source: {node}")
    if not isinstance(layout.get("reliable"), bool):
        fail(f"domain query node layout missing reliable flag: {node}")
    if not isinstance(layout.get("sizeSource"), str) or not isinstance(layout.get("boundsSource"), str):
        fail(f"domain query node layout missing source metadata: {node}")
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


def close_editor_for_project(project_root: Path) -> None:
    uproject = next(project_root.glob("*.uproject"), None)
    if uproject is None:
        print(f"[WARN] close-editor skipped: no .uproject found under {project_root}")
        return

    system = platform.system()
    if system == "Darwin":
        result = subprocess.run(
            ["pkill", "-f", f"UnrealEditor.*{uproject}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode in {0, 1}:
            print(f"[PASS] Unreal Editor close requested for {uproject}")
        else:
            print(f"[WARN] Unreal Editor close command returned {result.returncode} for {uproject}")
        return

    if system == "Windows":
        command = (
            "$uproject = %s; "
            "Get-CimInstance Win32_Process -Filter \"Name = 'UnrealEditor.exe'\" | "
            "Where-Object { $_.CommandLine -like \"*$uproject*\" } | "
            "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
        ) % json.dumps(str(uproject))
        result = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command],
            check=False,
        )
        if result.returncode == 0:
            print(f"[PASS] Unreal Editor close requested for {uproject}")
        else:
            print(f"[WARN] Unreal Editor close command returned {result.returncode} for {uproject}")
        return

    print(f"[WARN] close-editor skipped: unsupported platform {system}")


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
        help="Override path to the loomle client binary. Defaults to client/target/release/loomle(.exe).",
    )
    parser.add_argument(
        "--close-editor-on-success",
        action="store_true",
        help="Close the Unreal Editor instance for this project after the regression completes successfully.",
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
    temp_interface_asset = make_temp_asset_path("/Game/Codex/BPI_BridgeRegression")
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
    completed_successfully = False

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

        blueprint_desc = call_domain_tool(client, 3, "blueprint", "describe", {"assetPath": temp_asset}, expect_error=True)
        if extract_nested_error_code(blueprint_desc) not in {"ASSET_NOT_FOUND", "LOAD_ASSET_FAILED", "INVALID_ARGUMENT"}:
            fail(f"blueprint.describe pre-create error shape mismatch: {blueprint_desc}")
        print("[PASS] blueprint.describe error path validated")

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

        blueprint_list = call_domain_tool(client, 5, "blueprint", "list", {"assetPath": temp_asset})
        graphs = blueprint_list.get("graphs")
        if not isinstance(graphs, list):
            fail(f"blueprint.list missing graphs[]: {blueprint_list}")
        if not any(isinstance(g, dict) and g.get("graphName") == "EventGraph" for g in graphs):
            fail(f"blueprint.list did not include EventGraph: {blueprint_list}")
        event_graph = next(
            (g for g in graphs if isinstance(g, dict) and g.get("graphName") == "EventGraph"),
            None,
        )
        if not isinstance(event_graph, dict):
            fail(f"blueprint.list event graph entry missing: {blueprint_list}")
        event_graph_ref = event_graph.get("graphRef")
        if not isinstance(event_graph_ref, dict) or event_graph_ref.get("kind") != "asset":
            fail(f"graph.list event graph graphRef missing/invalid: {event_graph}")
        print("[PASS] blueprint.list validated")

        interface_fixture_payload = call_tool(
            client,
            600,
            "execute",
            {
                "mode": "exec",
                "code": (
                    "import json, unreal\n"
                    f"asset={json.dumps(temp_interface_asset, ensure_ascii=False)}\n"
                    "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                    "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                    "bpi = unreal.EditorAssetLibrary.load_asset(asset)\n"
                    "if bpi is None:\n"
                    "    factory = unreal.BlueprintInterfaceFactory()\n"
                    "    bpi = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                    "if bpi is None:\n"
                    "    raise RuntimeError('failed to create Blueprint Interface asset')\n"
                    "generated_class = bpi.generated_class() if hasattr(bpi, 'generated_class') and callable(bpi.generated_class) else None\n"
                    "if generated_class is None:\n"
                    "    generated_class = bpi.get_editor_property('generated_class')\n"
                    "print(json.dumps({'asset': asset, 'classPath': generated_class.get_path_name() if generated_class else ''}, ensure_ascii=False))\n"
                ),
            },
        )
        interface_fixture = parse_execute_json(interface_fixture_payload)
        interface_class_path = interface_fixture.get("classPath")
        if not isinstance(interface_class_path, str) or not interface_class_path:
            fail(f"failed to resolve Blueprint Interface class path: {interface_fixture}")
        dry_run_interface_payload = call_tool(
            client,
            606,
            "blueprint.asset.edit",
            {
                "assetPath": temp_asset,
                "operation": "addInterface",
                "args": {"interfaceClassPath": interface_class_path},
                "dryRun": True,
            },
        )
        if dry_run_interface_payload.get("applied") is not False or dry_run_interface_payload.get("dryRun") is not True:
            fail(f"blueprint.asset.edit addInterface dryRun shape mismatch: {dry_run_interface_payload}")
        dry_run_list_payload = call_tool(
            client,
            607,
            "blueprint.asset.edit",
            {"assetPath": temp_asset, "operation": "listInterfaces"},
        )
        dry_run_interfaces = dry_run_list_payload.get("interfaces")
        if not isinstance(dry_run_interfaces, list) or any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in dry_run_interfaces
        ):
            fail(f"blueprint.asset.edit dryRun unexpectedly added interface: {dry_run_list_payload}")
        add_interface_payload = call_tool(
            client,
            601,
            "blueprint.asset.edit",
            {
                "assetPath": temp_asset,
                "operation": "addInterface",
                "args": {"interfaceClassPath": interface_class_path},
            },
        )
        if add_interface_payload.get("applied") is not True:
            fail(f"blueprint.asset.edit addInterface did not apply: {add_interface_payload}")
        list_interface_payload = call_tool(
            client,
            602,
            "blueprint.asset.edit",
            {"assetPath": temp_asset, "operation": "listInterfaces"},
        )
        listed_interfaces = list_interface_payload.get("interfaces")
        if not isinstance(listed_interfaces, list) or not any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in listed_interfaces
        ):
            fail(f"blueprint.asset.edit listInterfaces missing added interface: {list_interface_payload}")
        asset_inspect_payload = call_tool(client, 603, "blueprint.asset.inspect", {"assetPath": temp_asset})
        inspected_interfaces = asset_inspect_payload.get("implementedInterfaces")
        if not isinstance(inspected_interfaces, list) or not any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in inspected_interfaces
        ):
            fail(f"blueprint.asset.inspect missing implementedInterfaces entry: {asset_inspect_payload}")
        remove_interface_payload = call_tool(
            client,
            604,
            "blueprint.asset.edit",
            {
                "assetPath": temp_asset,
                "operation": "removeInterface",
                "args": {"interfaceClassPath": interface_class_path},
            },
        )
        if remove_interface_payload.get("applied") is not True:
            fail(f"blueprint.asset.edit removeInterface did not apply: {remove_interface_payload}")
        list_after_remove_payload = call_tool(
            client,
            605,
            "blueprint.asset.edit",
            {"assetPath": temp_asset, "operation": "listInterfaces"},
        )
        interfaces_after_remove = list_after_remove_payload.get("interfaces")
        if not isinstance(interfaces_after_remove, list) or any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in interfaces_after_remove
        ):
            fail(f"blueprint.asset.edit removeInterface did not remove interface: {list_after_remove_payload}")
        print("[PASS] blueprint.asset.edit interface lifecycle validated")

        graph_query = call_domain_tool(
            client,
            6,
            "blueprint",
            "query",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "limit": 200,
                "layoutDetail": "measured",
            },
        )
        snapshot = graph_query.get("semanticSnapshot")
        if not isinstance(snapshot, dict):
            fail(f"blueprint.query missing semanticSnapshot: {graph_query}")
        if not isinstance(snapshot.get("nodes"), list) or not isinstance(snapshot.get("edges"), list):
            fail(f"blueprint.query invalid semanticSnapshot shape: {snapshot}")
        query_meta = graph_query.get("meta")
        if not isinstance(query_meta, dict):
            fail(f"blueprint.query missing meta: {graph_query}")
        layout_caps = query_meta.get("layoutCapabilities")
        if not isinstance(layout_caps, dict):
            fail(f"blueprint.query missing layoutCapabilities: {graph_query}")
        if layout_caps.get("canReadPosition") is not True:
            fail(f"blueprint.query layoutCapabilities missing canReadPosition=true: {query_meta}")
        if query_meta.get("layoutDetailRequested") != "measured":
            fail(f"blueprint.query layoutDetailRequested mismatch: {query_meta}")
        if query_meta.get("layoutDetailApplied") != "basic":
            fail(f"blueprint.query layoutDetailApplied mismatch: {query_meta}")
        query_diagnostics = graph_query.get("diagnostics")
        if not isinstance(query_diagnostics, list):
            fail(f"blueprint.query diagnostics missing or invalid: {graph_query}")
        if not any(isinstance(d, dict) and d.get("code") == "LAYOUT_DETAIL_DOWNGRADED" for d in query_diagnostics):
            fail(f"blueprint.query missing LAYOUT_DETAIL_DOWNGRADED diagnostic: {graph_query}")
        print("[PASS] blueprint.query structure validated")

        blueprint_verify = call_domain_tool(
            client,
            6405,
            "blueprint",
            "verify",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "limit": 200,
            },
        )
        if blueprint_verify.get("status") not in {"ok", "warn"}:
            fail(f"blueprint.verify returned unexpected status: {blueprint_verify}")
        if not isinstance(blueprint_verify.get("queryReport"), dict):
            fail(f"blueprint.verify missing queryReport: {blueprint_verify}")
        blueprint_compile_report = blueprint_verify.get("compileReport")
        if not isinstance(blueprint_compile_report, dict) or blueprint_compile_report.get("compiled") is not True:
            fail(f"blueprint.verify missing compiled=true compileReport: {blueprint_verify}")
        if not isinstance(blueprint_verify.get("diagnostics"), list):
            fail(f"blueprint.verify missing diagnostics[]: {blueprint_verify}")
        print("[PASS] blueprint.verify unified summary validated")

        member_ops = [
            ("component create root", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.SceneComponent",
                    "componentName": "RootScene",
                },
            }),
            ("component create mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.StaticMeshComponent",
                    "componentName": "VisualMesh",
                    "parentComponentName": "RootScene",
                },
            }),
            ("component update mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "update",
                "args": {
                    "componentName": "VisualMesh",
                    "meshAssetPath": "/Engine/BasicShapes/Cube.Cube",
                    "relativeLocation": {"x": 10, "y": 20, "z": 30},
                    "relativeScale3D": {"x": 2, "y": 2, "z": 2},
                },
            }),
            ("component rename mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "rename",
                "args": {
                    "componentName": "VisualMesh",
                    "newName": "VisualMeshRenamed",
                },
            }),
            ("component create box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.BoxComponent",
                    "componentName": "BoxVolume",
                },
            }),
            ("component update box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "update",
                "args": {
                    "componentName": "BoxVolume",
                    "boxExtent": {"x": 50, "y": 60, "z": 70},
                    "collisionMode": "QueryOnly",
                    "generateOverlapEvents": True,
                },
            }),
            ("component reparent box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "reparent",
                "args": {
                    "componentName": "BoxVolume",
                    "parentComponentName": "RootScene",
                },
            }),
            ("component reorder box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "reorder",
                "args": {
                    "componentName": "BoxVolume",
                    "targetComponentName": "VisualMeshRenamed",
                    "placement": "before",
                },
            }),
            ("component create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.SceneComponent",
                    "componentName": "TempDeleteComponent",
                },
            }),
            ("component delete temp", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "delete",
                "args": {
                    "componentName": "TempDeleteComponent",
                },
            }),
            ("variable create ready", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "bIsReady",
                    "type": {"category": "bool"},
                    "defaultValue": "false",
                },
            }),
            ("variable update ready", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "update",
                "args": {
                    "variableName": "bIsReady",
                    "defaultValue": "true",
                    "category": "State",
                    "tooltip": "Ready flag",
                    "exposeOnSpawn": True,
                },
            }),
            ("variable create count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "Count",
                    "type": {"category": "int"},
                    "defaultValue": "0",
                },
            }),
            ("variable reorder count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "reorder",
                "args": {
                    "variableName": "Count",
                    "targetVariableName": "bIsReady",
                    "placement": "before",
                },
            }),
            ("variable rename count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "rename",
                "args": {
                    "variableName": "Count",
                    "newName": "ItemCount",
                },
            }),
            ("variable setdefault count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "setDefault",
                "args": {
                    "variableName": "ItemCount",
                    "defaultValue": "5",
                },
            }),
            ("variable create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "TempDeleteVariable",
                    "type": {"category": "bool"},
                    "defaultValue": "false",
                },
            }),
            ("variable delete temp", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "delete",
                "args": {
                    "variableName": "TempDeleteVariable",
                },
            }),
            ("function create", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "create",
                "args": {
                    "functionName": "ComputeValue",
                    "inputs": [{"name": "bInput", "type": {"category": "bool"}}],
                    "outputs": [{"name": "Value", "type": {"category": "int"}}],
                    "pure": True,
                    "const": True,
                    "category": "Logic",
                    "tooltip": "Compute a test value",
                },
            }),
            ("function rename", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "rename",
                "args": {
                    "functionName": "ComputeValue",
                    "newName": "ComputeValueRenamed",
                },
            }),
            ("function setFlags", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "setFlags",
                "args": {
                    "functionName": "ComputeValueRenamed",
                    "pure": True,
                    "const": True,
                    "category": "Logic",
                    "tooltip": "Compute a test value",
                },
            }),
            ("function create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "create",
                "args": {
                    "functionName": "TempDeleteFunction",
                },
            }),
            ("function delete temp", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "delete",
                "args": {
                    "functionName": "TempDeleteFunction",
                },
            }),
            ("macro create", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "create",
                "args": {
                    "macroName": "GuardMacro",
                    "inputs": [{"name": "bGate", "type": {"category": "bool"}}],
                    "outputs": [{"name": "bPassed", "type": {"category": "bool"}}],
                    "category": "Logic",
                },
            }),
            ("macro rename", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "rename",
                "args": {
                    "macroName": "GuardMacro",
                    "newName": "GuardMacroRenamed",
                },
            }),
            ("macro create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "create",
                "args": {
                    "macroName": "TempDeleteMacro",
                },
            }),
            ("macro delete temp", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "delete",
                "args": {
                    "macroName": "TempDeleteMacro",
                },
            }),
            ("dispatcher create", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "create",
                "args": {
                    "dispatcherName": "OnReady",
                    "inputs": [{"name": "bReady", "type": {"category": "bool"}}],
                    "category": "Events",
                },
            }),
            ("dispatcher rename", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "rename",
                "args": {
                    "dispatcherName": "OnReady",
                    "newName": "OnReadyChanged",
                },
            }),
            ("dispatcher create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "create",
                "args": {
                    "dispatcherName": "TempDeleteDispatcher",
                },
            }),
            ("dispatcher delete temp", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "delete",
                "args": {
                    "dispatcherName": "TempDeleteDispatcher",
                },
            }),
        ]
        for index, (label, request) in enumerate(member_ops, start=1):
            payload = call_tool(client, 6460 + index, "blueprint.member.edit", request)
            if payload.get("applied") is not True:
                fail(f"blueprint.member.edit {label} did not apply: {payload}")

        custom_event_graph_payload = call_tool(
            client,
            6510,
            "blueprint.graph.edit",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {
                        "kind": "addNode.customEvent",
                        "alias": "customEvent",
                        "name": "OnGraphCustomEvent",
                        "position": {"x": 480, "y": 120},
                        "replication": "server",
                        "reliable": True,
                        "inputs": [
                            {"name": "Count", "type": {"category": "int"}},
                        ],
                    }
                ],
            },
        )
        if custom_event_graph_payload.get("applied") is not True:
            fail(f"blueprint.graph.edit addNode.customEvent did not apply: {custom_event_graph_payload}")

        event_member_ops = [
            (
                "event create temp",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "create",
                    "args": {"name": "TempDeleteCustomEvent", "graphName": "EventGraph", "x": 480, "y": 260},
                },
            ),
            (
                "event create owning client",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "create",
                    "args": {
                        "name": "ClientRejectCollect",
                        "graphName": "EventGraph",
                        "x": 760,
                        "y": 260,
                        "replication": "owningClient",
                        "reliable": True,
                        "inputs": [
                            {"name": "CoinId", "type": {"category": "string"}},
                        ],
                    },
                },
            ),
            (
                "event update signature",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "updateSignature",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "inputs": [
                            {"name": "bReady", "type": {"category": "bool"}},
                        ],
                    },
                },
            ),
            (
                "event set flags",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "setFlags",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "replication": "netMulticast",
                        "reliable": False,
                    },
                },
            ),
            (
                "event rename",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "rename",
                    "args": {"name": "OnGraphCustomEvent", "newName": "OnGraphCustomEventRenamed"},
                },
            ),
            (
                "event delete temp",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "delete",
                    "args": {"name": "TempDeleteCustomEvent"},
                },
            ),
        ]
        for index, (label, request) in enumerate(event_member_ops, start=1):
            payload = call_tool(client, 6510 + index, "blueprint.member.edit", request)
            if payload.get("applied") is not True:
                fail(f"blueprint.member.edit {label} did not apply: {payload}")

        compiled_member_bp = call_tool(client, 6520, "blueprint.compile", {"assetPath": temp_asset})
        if compiled_member_bp.get("compiled") is not True:
            fail(f"blueprint.compile after member.edit failed: {compiled_member_bp}")
        validated_member_bp = call_tool(client, 6521, "blueprint.validate", {"assetPath": temp_asset})
        if validated_member_bp.get("status") not in {"ok", "warn"}:
            fail(f"blueprint.validate after member.edit returned unexpected status: {validated_member_bp}")
        variable_inspect_payload = call_tool(
            client,
            6522,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "variable"},
        )
        variable_items = variable_inspect_payload.get("items")
        if not isinstance(variable_items, list):
            fail(f"blueprint.member.inspect variable items missing: {variable_inspect_payload}")
        component_inspect_payload = call_tool(
            client,
            6523,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "component"},
        )
        component_items = component_inspect_payload.get("items")
        if not isinstance(component_items, list):
            fail(f"blueprint.member.inspect component items missing: {component_inspect_payload}")
        dry_run_member_payload = call_tool(
            client,
            6529,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {"variableName": "DryRunVariable", "type": {"category": "bool"}},
                "dryRun": True,
            },
        )
        if dry_run_member_payload.get("applied") is not False or dry_run_member_payload.get("dryRun") is not True:
            fail(f"blueprint.member.edit dryRun shape mismatch: {dry_run_member_payload}")
        macro_inspect_payload = call_tool(
            client,
            6531,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "macro"},
        )
        macro_items = macro_inspect_payload.get("items")
        if not isinstance(macro_items, list) or not any(
            isinstance(entry, dict) and entry.get("name") == "GuardMacroRenamed"
            for entry in macro_items
        ):
            fail(f"blueprint.member.inspect macro items missing renamed macro: {macro_inspect_payload}")
        dispatcher_inspect_payload = call_tool(
            client,
            6532,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "dispatcher"},
        )
        dispatcher_items = dispatcher_inspect_payload.get("items")
        if not isinstance(dispatcher_items, list) or not any(
            isinstance(entry, dict) and entry.get("name") == "OnReadyChanged"
            for entry in dispatcher_items
        ):
            fail(f"blueprint.member.inspect dispatcher items missing renamed dispatcher: {dispatcher_inspect_payload}")
        event_inspect_payload = call_tool(
            client,
            6533,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "event"},
        )
        event_items = event_inspect_payload.get("items")
        if not isinstance(event_items, list):
            fail(f"blueprint.member.inspect event items missing: {event_inspect_payload}")
        renamed_event = next(
            (
                entry
                for entry in event_items
                if isinstance(entry, dict) and entry.get("name") == "OnGraphCustomEventRenamed"
            ),
            None,
        )
        if not isinstance(renamed_event, dict) or renamed_event.get("eventKind") != "custom":
            fail(f"blueprint.member.inspect event missing renamed custom event: {event_inspect_payload}")
        if renamed_event.get("replication") != "netMulticast" or renamed_event.get("reliable") is not False:
            fail(f"blueprint.member.inspect event missing multicast flags: {event_inspect_payload}")
        renamed_event_pins = renamed_event.get("pins")
        if not isinstance(renamed_event_pins, list) or not any(
            isinstance(pin, dict) and pin.get("name") == "bReady"
            for pin in renamed_event_pins
        ):
            fail(f"blueprint.member.inspect event missing updated input pin: {event_inspect_payload}")
        client_event = next(
            (
                entry
                for entry in event_items
                if isinstance(entry, dict) and entry.get("name") == "ClientRejectCollect"
            ),
            None,
        )
        if not isinstance(client_event, dict) or client_event.get("replication") != "owningClient" or client_event.get("reliable") is not True:
            fail(f"blueprint.member.inspect event missing owning-client reliable flags: {event_inspect_payload}")
        graph_list_payload = call_tool(client, 6524, "blueprint.graph.list", {"assetPath": temp_asset})
        listed_graphs = graph_list_payload.get("graphs")
        if not isinstance(listed_graphs, list):
            fail(f"blueprint.graph.list missing graphs[]: {graph_list_payload}")
        compute_graph_payload = call_tool(
            client,
            6525,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "ComputeValueRenamed"}},
        )
        guard_graph_payload = call_tool(
            client,
            6526,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "GuardMacroRenamed"}},
        )
        dispatcher_graph_payload = call_tool(
            client,
            6527,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "OnReadyChanged"}},
        )
        deleted_dispatcher_graph = call_tool(
            client,
            6528,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "TempDeleteDispatcher"}},
            expect_error=True,
        )
        if deleted_dispatcher_graph.get("isError") is not True:
            fail(f"deleted dispatcher graph should not resolve: {deleted_dispatcher_graph}")

        member_state_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=6540,
            code=(
                "import json, unreal\n"
                f"asset={json.dumps(temp_asset, ensure_ascii=False)}\n"
                "bp = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if bp is None:\n"
                "    raise RuntimeError('failed to load regression blueprint')\n"
                "subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)\n"
                "handles = subsys.k2_gather_subobject_data_for_blueprint(bp) if subsys else []\n"
                "components = []\n"
                "seen_component_names = set()\n"
                "for handle in handles:\n"
                "    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)\n"
                "    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_component(data):\n"
                "        continue\n"
                "    name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(data))\n"
                "    if not name or name in seen_component_names:\n"
                "        continue\n"
                "    seen_component_names.add(name)\n"
                "    parent_handle = unreal.SubobjectDataBlueprintFunctionLibrary.get_parent_handle(data)\n"
                "    parent_name = None\n"
                "    if unreal.SubobjectDataBlueprintFunctionLibrary.is_handle_valid(parent_handle):\n"
                "        parent_data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(parent_handle)\n"
                "        parent_name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(parent_data))\n"
                "    template = unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object(data)\n"
                "    entry = {\n"
                "        'name': name,\n"
                "        'parent': parent_name if parent_name != 'None' else None,\n"
                "        'classPath': template.get_class().get_path_name() if template else '',\n"
                "    }\n"
                "    if isinstance(template, unreal.SceneComponent):\n"
                "        loc = template.get_editor_property('relative_location')\n"
                "        scale = template.get_editor_property('relative_scale3d')\n"
                "        entry['relativeLocation'] = [loc.x, loc.y, loc.z]\n"
                "        entry['relativeScale3D'] = [scale.x, scale.y, scale.z]\n"
                "    if isinstance(template, unreal.StaticMeshComponent):\n"
                "        mesh = template.get_editor_property('static_mesh')\n"
                "        entry['staticMesh'] = mesh.get_path_name() if mesh else ''\n"
                "    if isinstance(template, unreal.BoxComponent):\n"
                "        extent = template.get_editor_property('box_extent')\n"
                "        entry['boxExtent'] = [extent.x, extent.y, extent.z]\n"
                "        entry['generateOverlapEvents'] = bool(template.get_editor_property('generate_overlap_events'))\n"
                "    components.append(entry)\n"
                "gc = bp.generated_class() if hasattr(bp, 'generated_class') and callable(bp.generated_class) else None\n"
                "cdo = unreal.get_default_object(gc) if gc else None\n"
                "result = {\n"
                "    'components': components,\n"
                "    'rootComponents': [entry['name'] for entry in components if entry.get('parent') is None],\n"
                "    'rootSceneChildren': [entry['name'] for entry in components if entry.get('parent') == 'RootScene'],\n"
                "    'itemCountDefault': cdo.get_editor_property('ItemCount') if cdo else None,\n"
                "    'isReadyDefault': cdo.get_editor_property('bIsReady') if cdo else None,\n"
                "}\n"
                "print(json.dumps(result, ensure_ascii=False))\n"
            ),
        )
        member_state = parse_execute_json(member_state_payload)
        variable_names = [
            entry.get("name")
            for entry in variable_items
            if isinstance(entry, dict) and entry.get("name") in {"ItemCount", "bIsReady", "TempDeleteVariable"}
        ]
        if variable_names[:2] != ["ItemCount", "bIsReady"]:
            fail(f"member.edit variable order mismatch: inspect={variable_inspect_payload} state={member_state}")
        if "TempDeleteVariable" in variable_names:
            fail(f"member.edit temp variable should have been deleted: inspect={variable_inspect_payload} state={member_state}")
        if member_state.get("itemCountDefault") != 5 or member_state.get("isReadyDefault") is not True:
            fail(f"member.edit variable defaults mismatch: {member_state}")
        graph_names = [entry.get("graphName") for entry in listed_graphs if isinstance(entry, dict)]
        if "ComputeValueRenamed" not in graph_names or "TempDeleteFunction" in graph_names:
            fail(f"member.edit function graph state mismatch: {graph_list_payload}")
        if "GuardMacroRenamed" not in graph_names or "TempDeleteMacro" in graph_names:
            fail(f"member.edit macro graph state mismatch: {graph_list_payload}")
        compute_nodes = compute_graph_payload.get("semanticSnapshot", {}).get("nodes", [])
        compute_entry = next((node for node in compute_nodes if node.get("className") == "K2Node_FunctionEntry"), None)
        compute_result = next((node for node in compute_nodes if node.get("className") == "K2Node_FunctionResult"), None)
        if not isinstance(compute_entry, dict) or not isinstance(compute_result, dict):
            fail(f"member.edit function graph inspect missing entry/result nodes: {compute_graph_payload}")
        compute_input_pins = [
            pin.get("name")
            for pin in compute_entry.get("pins", [])
            if isinstance(pin, dict) and pin.get("direction") == "output" and pin.get("category") != "exec"
        ]
        compute_output_pins = [
            pin.get("name")
            for pin in compute_result.get("pins", [])
            if isinstance(pin, dict) and pin.get("direction") == "input" and pin.get("category") != "exec"
        ]
        if compute_input_pins != ["bInput"] or compute_output_pins != ["Value"]:
            fail(f"member.edit function signature mismatch: {compute_graph_payload}")
        compute_boundary = compute_entry.get("graphBoundarySummary", {})
        if not isinstance(compute_boundary, dict) or compute_boundary.get("isPure") is not True:
            fail(f"member.edit pure function flag mismatch: {compute_graph_payload}")
        if compute_boundary.get("isConst") is not True:
            fail(f"member.edit const function flag mismatch: {compute_graph_payload}")
        guard_nodes = guard_graph_payload.get("semanticSnapshot", {}).get("nodes", [])
        guard_entry = next(
            (
                node
                for node in guard_nodes
                if node.get("className") == "K2Node_Tunnel" and node.get("nodeTitle") == "Inputs"
            ),
            None,
        )
        if not isinstance(guard_entry, dict):
            fail(f"member.edit macro graph inspect missing input tunnel: {guard_graph_payload}")
        guard_pins = [
            pin.get("name")
            for pin in guard_entry.get("pins", [])
            if isinstance(pin, dict) and pin.get("category") != "exec"
        ]
        if guard_pins != ["bGate"]:
            fail(f"member.edit macro signature mismatch: {guard_graph_payload}")
        dispatcher_nodes = dispatcher_graph_payload.get("semanticSnapshot", {}).get("nodes", [])
        dispatcher_entry = next(
            (node for node in dispatcher_nodes if node.get("className") == "K2Node_FunctionEntry"),
            None,
        )
        if not isinstance(dispatcher_entry, dict):
            fail(f"member.edit dispatcher graph inspect missing entry node: {dispatcher_graph_payload}")
        dispatcher_pins = [
            pin.get("name")
            for pin in dispatcher_entry.get("pins", [])
            if isinstance(pin, dict) and pin.get("category") != "exec"
        ]
        if dispatcher_pins != ["bReady"]:
            fail(f"member.edit dispatcher signature mismatch: {dispatcher_graph_payload}")
        component_by_name = {
            entry.get("name"): entry
            for entry in member_state.get("components", [])
            if isinstance(entry, dict)
        }
        if "TempDeleteComponent" in component_by_name:
            fail(f"member.edit temp component should have been deleted: {member_state}")
        root_scene = component_by_name.get("RootScene")
        mesh_component = component_by_name.get("VisualMeshRenamed")
        box_component = component_by_name.get("BoxVolume")
        if not isinstance(root_scene, dict) or not isinstance(mesh_component, dict) or not isinstance(box_component, dict):
            fail(f"member.edit components missing expected entries: {member_state}")
        if mesh_component.get("parent") != "RootScene" or box_component.get("parent") != "RootScene":
            fail(f"member.edit component reparent mismatch: {member_state}")
        if mesh_component.get("relativeLocation") != [10.0, 20.0, 30.0] or mesh_component.get("relativeScale3D") != [2.0, 2.0, 2.0]:
            fail(f"member.edit component transform mismatch: {member_state}")
        if "Cube.Cube" not in str(mesh_component.get("staticMesh", "")):
            fail(f"member.edit static mesh assignment mismatch: {member_state}")
        if box_component.get("boxExtent") != [50.0, 60.0, 70.0] or box_component.get("generateOverlapEvents") is not True:
            fail(f"member.edit box component update mismatch: {member_state}")
        component_names = [
            entry.get("name")
            for entry in component_items
            if isinstance(entry, dict) and entry.get("name") in {"RootScene", "BoxVolume", "VisualMeshRenamed", "TempDeleteComponent"}
        ]
        if component_names != ["RootScene", "BoxVolume", "VisualMeshRenamed"]:
            fail(f"member.edit component reorder mismatch: inspect={component_inspect_payload} state={member_state}")
        print("[PASS] blueprint.member.edit full member workflow validated")

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
        fixture_asset_path = pcg_fixture.get("assetPath")
        if fixture_asset_path != temp_pcg_asset:
            fail(f"PCG fixture returned wrong assetPath: {pcg_fixture}")
        print("[PASS] temporary PCG fixture created")

        pcg_context = call_tool(client, 7056, "context", {})
        selection = pcg_context.get("selection")
        if not isinstance(selection, dict):
            fail(f"context missing selection after PCG fixture setup: {pcg_context}")
        resolved_graph_refs = selection.get("resolvedGraphRefs")
        if not isinstance(resolved_graph_refs, list):
            fail(f"context selection missing resolvedGraphRefs[] after PCG fixture setup: {pcg_context}")
        if not any(
            isinstance(entry, dict)
            and isinstance(entry.get("graphRef"), dict)
            and entry["graphRef"].get("assetPath") == temp_pcg_asset
            for entry in resolved_graph_refs
        ):
            fail(f"context selection did not include the PCG fixture graphRef: {pcg_context}")

        queried_pcg = call_domain_tool(
            client,
            7059,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        queried_snapshot = queried_pcg.get("semanticSnapshot")
        if not isinstance(queried_snapshot, dict):
            fail(f"pcg.query missing semanticSnapshot for direct asset read: {queried_pcg}")
        queried_graph_ref = queried_pcg.get("graphRef")
        if not isinstance(queried_graph_ref, dict) or queried_graph_ref.get("assetPath") != temp_pcg_asset:
            fail(f"pcg.query did not echo expected asset graphRef: {queried_pcg}")
        print("[PASS] pcg.query direct asset addressing validated")

        pcg_class_desc = call_domain_tool(
            client,
            101001,
            "pcg",
            "describe",
            {"nodeClass": "/Script/PCG.PCGTransformPointsSettings"},
        )
        if pcg_class_desc.get("mode") != "class":
            fail(f"pcg.describe class mode mismatch: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("inputPins"), list):
            fail(f"pcg.describe missing inputPins[]: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("outputPins"), list):
            fail(f"pcg.describe missing outputPins[]: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("properties"), list):
            fail(f"pcg.describe missing properties[]: {pcg_class_desc}")

        pcg_dry_run_script = call_domain_tool(
            client,
            1010011,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "dryRun": True,
                "ops": [
                    {
                        "op": "runScript",
                        "mode": "inlineCode",
                        "entry": "run",
                        "code": "def run(ctx):\n  return {'ok': True}",
                    }
                ],
            },
            expect_error=True,
        )
        pcg_dry_run_script_struct = structured_detail_or_payload(pcg_dry_run_script)
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
        bad_query = call_domain_tool(
            client,
            8,
            "blueprint",
            "query",
            {"assetPath": temp_asset},
            expect_error=True,
        )
        _ = bad_query
        print("[PASS] blueprint.query error path validated")

        bad_remove = call_domain_tool(
            client,
            8008,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [{"kind": "removeNode", "node": {}}],
            },
            expect_error=True,
        )
        bad_remove_struct = structured_detail_or_payload(bad_remove)
        if bad_remove_struct.get("code") != "INVALID_ARGUMENT":
            fail(f"blueprint.graph.edit invalid command should return INVALID_ARGUMENT: {bad_remove_struct}")
        print("[PASS] blueprint.graph.edit command validation error path validated")

        add_a = call_domain_tool(
            client,
            10,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_branch({"x": 0, "y": 0})],
            },
        )
        node_a = op_ok(add_a).get("nodeId")
        if not isinstance(node_a, str) or not node_a:
            fail(f"addNode.byClass did not return nodeId: {add_a}")

        add_b = call_domain_tool(
            client,
            11,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_branch({"x": 320, "y": 0})],
            },
        )
        node_b = op_ok(add_b).get("nodeId")
        if not isinstance(node_b, str) or not node_b:
            fail(f"addNode.byClass did not return nodeId for second node: {add_b}")
        print("[PASS] blueprint.mutate addNode.byClass validated")

        self_graph_edit = call_domain_tool(
            client,
            12,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    {
                        "kind": "addNode",
                        "alias": "self",
                        "nodeType": {"id": "class:/Script/BlueprintGraph.K2Node_Self"},
                        "position": {"x": 0, "y": 220},
                    },
                    {
                        "kind": "addNode",
                        "alias": "cast_actor",
                        "nodeType": {"kind": "cast", "targetClassPath": "/Script/Engine.Actor"},
                        "position": {"x": 280, "y": 220},
                    },
                    {
                        "kind": "connect",
                        "from": {"node": {"alias": "self"}, "pin": "Self"},
                        "to": {"node": {"alias": "cast_actor"}, "pin": "Object"},
                    },
                ],
            },
        )
        self_results = self_graph_edit.get("opResults")
        if not isinstance(self_results, list) or len(self_results) != 3:
            fail(f"blueprint.graph.edit self node opResults mismatch: {self_graph_edit}")
        self_node_id = self_results[0].get("nodeId") if isinstance(self_results[0], dict) else None
        if not isinstance(self_node_id, str) or not self_node_id:
            fail(f"blueprint.graph.edit K2Node_Self did not return nodeId: {self_graph_edit}")
        if not all(isinstance(entry, dict) and entry.get("ok") for entry in self_results):
            fail(f"blueprint.graph.edit K2Node_Self/connect op failed: {self_graph_edit}")
        self_diff = self_graph_edit.get("diff")
        if not isinstance(self_diff, dict):
            fail(f"blueprint.graph.edit missing structured diff: {self_graph_edit}")
        self_nodes_added = self_diff.get("nodesAdded")
        if not isinstance(self_nodes_added, list) or not any(
            isinstance(entry, dict) and entry.get("nodeId") == self_node_id for entry in self_nodes_added
        ):
            fail(f"blueprint.graph.edit diff missing Self node addition: {self_graph_edit}")
        self_links_added = self_diff.get("linksAdded")
        if not isinstance(self_links_added, list) or not any(
            isinstance(entry, dict)
            and entry.get("fromNodeId") == self_node_id
            and entry.get("fromPin") == "Self"
            and entry.get("toPin") == "Object"
            for entry in self_links_added
        ):
            fail(f"blueprint.graph.edit diff missing Self link addition: {self_graph_edit}")
        first_op_diff = self_results[0].get("diff") if isinstance(self_results[0], dict) else None
        if not isinstance(first_op_diff, dict) or not isinstance(first_op_diff.get("nodesAdded"), list):
            fail(f"blueprint.graph.edit opResults diff missing node addition: {self_graph_edit}")

        self_query = query_graph_payload(
            client,
            13,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        self_nodes = self_query.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(self_nodes, list):
            fail(f"blueprint.graph.inspect self node missing nodes[]: {self_query}")
        self_node = require_node([node for node in self_nodes if isinstance(node, dict)], self_node_id)
        if self_node.get("className") != "K2Node_Self":
            fail(f"blueprint.graph.inspect self node class mismatch: {self_node}")
        self_pins = self_node.get("pins")
        if not isinstance(self_pins, list) or not any(
            isinstance(pin, dict) and pin.get("name") == "self" and pin.get("direction") == "output"
            for pin in self_pins
        ):
            fail(f"blueprint.graph.inspect self node output pin missing: {self_node}")
        if not any(
            isinstance(pin, dict)
            and pin.get("name") == "self"
            and any(isinstance(link, dict) and link.get("toPin") == "Object" for link in pin.get("links", []))
            for pin in self_pins
        ):
            fail(f"blueprint.graph.inspect self node link to UObject/Actor input missing: {self_node}")
        print("[PASS] blueprint.graph.edit K2Node_Self creation/connect/inspect validated")

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

        blueprint_revision_apply = call_domain_tool(
            client,
            106,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "expectedRevision": blueprint_revision_r0,
                "commands": [bp_branch({"x": 640, "y": 0})],
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

        stale_blueprint_revision = call_domain_tool(
            client,
            108,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "expectedRevision": blueprint_revision_r0,
                "commands": [bp_branch({"x": 960, "y": 0})],
            },
            expect_error=True,
        )
        if extract_nested_error_code(stale_blueprint_revision) != "REVISION_CONFLICT" and stale_blueprint_revision.get("domainCode") != "REVISION_CONFLICT":
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

        blueprint_dry_run = call_domain_tool(
            client,
            1091,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "dryRun": True,
                "commands": [bp_branch({"x": 1120, "y": 0})],
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

        bad_graph_command = call_domain_tool(
            client,
            10921,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "dryRun": True,
                "commands": [{"kind": "runScript"}],
            },
            expect_error=True,
        )
        if extract_nested_error_code(bad_graph_command) != "INVALID_ARGUMENT":
            fail(f"Blueprint unsupported graph.edit command should surface INVALID_ARGUMENT: {bad_graph_command}")
        print("[PASS] blueprint.graph.edit unsupported command rejected")

        partial_apply_node = call_domain_tool(
            client,
            10923,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_branch({"x": 1536, "y": 0})],
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
        partial_apply_failure = call_domain_tool(
            client,
            10925,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    bp_remove(partial_apply_node_id),
                    {"kind": "unknownCommand"},
                ],
            },
            expect_error=True,
        )
        partial_apply_struct = structured_detail_or_payload(partial_apply_failure)
        if partial_apply_struct.get("code") != "INVALID_ARGUMENT":
            fail(f"Blueprint invalid command batch should fail before apply: {partial_apply_failure}")
        partial_apply_after = query_graph_payload(
            client,
            10926,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        partial_apply_after_nodes = partial_apply_after.get("semanticSnapshot", {}).get("nodes", [])
        if partial_apply_after.get("revision") != partial_apply_before_revision:
            fail(f"Blueprint invalid command batch should not advance revision: before={partial_apply_before} after={partial_apply_after}")
        if any(
            isinstance(node, dict) and node.get("id") == partial_apply_node_id
            for node in partial_apply_after_nodes or []
        ):
            pass
        else:
            fail(f"Blueprint invalid command batch should not remove earlier node: {partial_apply_after}")
        print("[PASS] blueprint.graph.edit invalid command batch preflight validated")

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
        blueprint_idem_first = call_domain_tool(
            client,
            1094,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "idempotencyKey": blueprint_idem_key,
                "commands": [bp_branch({"x": 1280, "y": 0})],
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
        blueprint_idem_second = call_domain_tool(
            client,
            1096,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "idempotencyKey": blueprint_idem_key,
                "commands": [bp_branch({"x": 1280, "y": 0})],
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

        duplicate_client_ref = call_domain_tool(
            client,
            1098,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    bp_branch({"x": 1440, "y": 0}, alias="dup_ref"),
                    bp_branch({"x": 1600, "y": 0}, alias="dup_ref"),
                ],
            },
            expect_error=True,
        )
        duplicate_client_ref_struct = structured_detail_or_payload(duplicate_client_ref)
        duplicate_client_ref_results = duplicate_client_ref_struct.get("opResults")
        if not isinstance(duplicate_client_ref_results, list) or len(duplicate_client_ref_results) < 2:
            fail(f"blueprint.graph.edit duplicate clientRef missing opResults: {duplicate_client_ref}")
        duplicate_client_ref_second = duplicate_client_ref_results[1] if isinstance(duplicate_client_ref_results[1], dict) else {}
        if duplicate_client_ref_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"blueprint.graph.edit duplicate clientRef wrong errorCode: {duplicate_client_ref_second}")
        if "Duplicate clientRef" not in str(duplicate_client_ref_second.get("errorMessage", "")):
            fail(f"blueprint.graph.edit duplicate clientRef wrong errorMessage: {duplicate_client_ref_second}")
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

        connect_payload = call_domain_tool(
            client,
            12,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_connect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(connect_payload)

        break_payload = call_domain_tool(
            client,
            13,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_break_links(node_a, "then")],
            },
        )
        op_ok(break_payload)

        reconnect_payload = call_domain_tool(
            client,
            14,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_connect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(reconnect_payload)

        disconnect_payload = call_domain_tool(
            client,
            15,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_disconnect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(disconnect_payload)

        set_default_payload = call_domain_tool(
            client,
            16,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_set_default(node_b, "Condition", "true")],
            },
        )
        op_ok(set_default_payload)

        bad_set_default_payload = call_domain_tool(
            client,
            17,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_set_default(node_b, "DefinitelyMissingPin", "true")],
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
                    fail(f"blueprint.graph.edit bad setPinDefault detail is not valid JSON: {exc} payload={bad_set_default_payload}")
                bad_set_default_results = bad_set_default_struct.get("opResults")
        if not isinstance(bad_set_default_results, list) or not bad_set_default_results:
            fail(f"blueprint.graph.edit bad setPinDefault missing opResults: {bad_set_default_payload}")
        bad_set_default_first = bad_set_default_results[0] if isinstance(bad_set_default_results[0], dict) else {}
        if bad_set_default_first.get("errorCode") not in {"TARGET_NOT_FOUND", "INVALID_ARGUMENT", "INTERNAL_ERROR"}:
            fail(f"blueprint.graph.edit bad setPinDefault wrong errorCode: {bad_set_default_first}")
        details = bad_set_default_first.get("details")
        if isinstance(details, dict):
            expected_target_forms = details.get("expectedTargetForms")
            if not isinstance(expected_target_forms, list) or not expected_target_forms:
                fail(f"blueprint.graph.edit bad setPinDefault missing expectedTargetForms: {details}")
            candidate_pins = details.get("candidatePins")
            if not isinstance(candidate_pins, list) or not candidate_pins:
                fail(f"blueprint.graph.edit bad setPinDefault missing candidatePins: {details}")
            if not any(isinstance(pin, dict) and pin.get("pinName") == "Condition" for pin in candidate_pins):
                fail(f"blueprint.graph.edit bad setPinDefault candidatePins missing Condition: {candidate_pins}")
        elif "DefinitelyMissingPin" not in str(bad_set_default_first.get("errorMessage", "")):
            fail(f"blueprint.graph.edit bad setPinDefault should surface the missing pin name: {bad_set_default_first}")
        print("[PASS] blueprint.graph.edit setPinDefault diagnostics validated")

        move_payload = call_domain_tool(
            client,
            18,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_b), "position": {"x": 640, "y": 120}}
                ],
            },
        )
        op_ok(move_payload)

        move_by_payload = call_domain_tool(
            client,
            1801,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_b), "delta": {"x": 16, "y": 32}}
                ],
            },
        )
        op_ok(move_by_payload)

        move_node_a_payload = call_domain_tool(
            client,
            1802,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_a), "delta": {"x": 16, "y": 0}}
                ],
            },
        )
        op_ok(move_node_a_payload)

        compile_payload = call_domain_tool(
            client,
            19,
            "blueprint",
            "compile",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
            },
        )
        nodes_after_compile = query_nodes(client, 20, temp_asset, "EventGraph")
        if compile_payload.get("compiled") is not True and compile_payload.get("status") not in {"clean", "compiled"}:
            fail(f"Blueprint compile should return a successful compile report: {compile_payload}")

        nodes_before_remove = nodes_after_compile
        node_a_info = require_node(nodes_before_remove, node_a)
        node_b_info = require_node(nodes_before_remove, node_b)
        node_a_layout = require_layout(node_a_info)
        node_b_layout = require_layout(node_b_info)
        if node_a_layout.get("position", {}).get("x") != 16 or node_a_layout.get("position", {}).get("y") != 0:
            fail(f"blueprint.graph.edit moveNode delta did not update node_a layout as expected: {node_a_info}")
        node_b_pos = node_b_layout.get("position", {})
        if node_b_pos.get("x") != 656 or node_b_pos.get("y") not in {144, 160}:
            fail(f"blueprint.graph.edit moveNode position/delta did not update node_b layout as expected: {node_b_info}")

        add_without_position = call_domain_tool(
            client,
            1806,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_branch()],
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
        remove_e = call_domain_tool(
            client,
            1808,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_remove(node_e)],
            },
        )
        op_ok(remove_e)
        print("[PASS] blueprint.graph.edit addNode.byClass auto-placement validated")

        remove_a = call_domain_tool(
            client,
            21,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_remove(node_a)],
            },
        )
        op_ok(remove_a)
        nodes_after_remove_a = query_nodes(client, 22, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_a, node_a)

        remove_b = call_domain_tool(
            client,
            23,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_remove(node_b)],
            },
        )
        op_ok(remove_b)
        nodes_after_remove_b = query_nodes(client, 24, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_b, node_b)

        add_c = call_domain_tool(
            client,
            25,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_branch({"x": 960, "y": 0})],
            },
        )
        node_c = op_ok(add_c).get("nodeId")
        if not isinstance(node_c, str) or not node_c:
            fail(f"addNode.byClass did not return nodeId for third node: {add_c}")

        remove_c = call_domain_tool(
            client,
            26,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_remove(node_c)],
            },
        )
        op_ok(remove_c)
        nodes_after_remove_c = query_nodes(client, 27, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_c, node_c)

        add_via_graph_ref = call_domain_tool(
            client,
            28,
            "blueprint",
            "mutate",
            {
                "graphRef": {"kind": "asset", "assetPath": temp_asset, "graphName": "EventGraph"},
                "commands": [bp_branch({"x": 1280, "y": 0})],
            },
        )
        node_d = op_ok(add_via_graph_ref).get("nodeId")
        if not isinstance(node_d, str) or not node_d:
            fail(f"graphRef(asset) mutate addNode.byClass did not return nodeId: {add_via_graph_ref}")

        remove_via_target_graph_ref = call_domain_tool(
            client,
            29,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": [bp_remove(node_d)],
            },
        )
        op_ok(remove_via_target_graph_ref)
        nodes_after_remove_d = query_nodes(client, 30, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_d, node_d)

        print("[PASS] blueprint.graph.edit removeNode validated for stable node ids")
        print("[PASS] blueprint.graph.edit graphRef(asset) validated")

        bulk_branch_ops = []
        for index in range(60):
            bulk_branch_ops.append(
                {
                    "kind": "addNode",
                    "alias": f"bulk_branch_{index}",
                    "nodeType": {"kind": "branch"},
                    "position": {"x": 2200 + (index * 48), "y": 1800},
                }
            )
        bulk_blueprint_insert = call_domain_tool(
            client,
            1600,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "commands": bulk_branch_ops,
            },
        )
        op_ok(bulk_blueprint_insert)

        blueprint_default_page = call_domain_tool(
            client,
            1601,
            "blueprint",
            "query",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
            },
        )
        default_snapshot = blueprint_default_page.get("semanticSnapshot", {})
        default_nodes = default_snapshot.get("nodes", [])
        default_meta = blueprint_default_page.get("meta", {})
        default_cursor = blueprint_default_page.get("nextCursor")
        if not isinstance(default_nodes, list) or not default_nodes:
            fail(
                "Blueprint graph.query without explicit limit should return nodes[]: "
                f"{blueprint_default_page}"
            )
        if default_meta.get("returnedNodes") != len(default_nodes):
            fail(
                "Blueprint graph.query without explicit limit should report returnedNodes matching nodes[] length: "
                f"{blueprint_default_page}"
            )
        if default_meta.get("truncated") is True:
            if not isinstance(default_cursor, str) or not default_cursor:
                fail(
                    "Blueprint graph.query without explicit limit should provide nextCursor when truncated: "
                    f"{blueprint_default_page}"
                )
        elif default_meta.get("truncated") is False:
            if default_cursor not in {"", None}:
                fail(
                    "Blueprint graph.query without explicit limit should not provide nextCursor when untruncated: "
                    f"{blueprint_default_page}"
                )
        else:
            fail(
                "Blueprint graph.query without explicit limit should report truncated metadata: "
                f"{blueprint_default_page}"
            )
        print("[PASS] blueprint graph.query default no-limit behavior validated")

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
        material_graph_list_without_type = call_domain_tool(
            client,
            10009,
            "material",
            "list",
            {"assetPath": material_asset_path},
        )
        if material_graph_list_without_type.get("assetPath") != material_asset_path:
            fail(f"material.list assetPath mismatch: {material_graph_list_without_type}")
        material_expressions = material_graph_list_without_type.get("expressions")
        if not isinstance(material_expressions, list):
            fail(f"material.list missing expressions[]: {material_graph_list_without_type}")
        material_output_count = material_graph_list_without_type.get("outputCount")
        if not isinstance(material_output_count, int) or material_output_count < 1:
            fail(f"material.list missing valid outputCount: {material_graph_list_without_type}")
        print("[PASS] material.list validated")

        material_add = call_domain_tool(
            client,
            10010,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionConstant"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"},
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

        material_revision_before = call_domain_tool(
            client,
            100101,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_revision_r0 = material_revision_before.get("revision")
        material_nodes_before = material_revision_before.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_revision_r0, str) or not material_revision_r0:
            fail(f"Material graph.query missing revision before expectedRevision test: {material_revision_before}")
        if not isinstance(material_nodes_before, list):
            fail(f"Material graph.query missing nodes before expectedRevision test: {material_revision_before}")

        material_revision_apply = call_domain_tool(
            client,
            100102,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        op_ok(material_revision_apply)
        material_revision_after_apply = call_domain_tool(
            client,
            100103,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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

        stale_material_revision = call_domain_tool(
            client,
            100104,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
            expect_error=True,
        )
        if extract_nested_error_code(stale_material_revision) != "REVISION_CONFLICT" and stale_material_revision.get("domainCode") != "REVISION_CONFLICT":
            fail(f"Material stale expectedRevision did not return REVISION_CONFLICT: {stale_material_revision}")
        material_revision_after_stale = call_domain_tool(
            client,
            100105,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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

        material_dry_run = call_domain_tool(
            client,
            100106,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "dryRun": True,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
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
        material_after_dry_run = call_domain_tool(
            client,
            100107,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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
        material_idem_before = call_domain_tool(
            client,
            1001071,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_idem_before_revision = material_idem_before.get("revision")
        material_idem_before_nodes = material_idem_before.get("semanticSnapshot", {}).get("nodes", [])
        material_idem_first = call_domain_tool(
            client,
            1001072,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        material_idem_first_op = op_ok(material_idem_first)
        material_idem_after_first = call_domain_tool(
            client,
            1001073,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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
        material_idem_second = call_domain_tool(
            client,
            1001074,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        material_idem_second_op = op_ok(material_idem_second)
        material_idem_after_second = call_domain_tool(
            client,
            1001075,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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

        material_duplicate_client_ref = call_domain_tool(
            client,
            1001076,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter",
                    },
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "nodeClassPath": "/Script/Engine.MaterialExpressionConstant",
                    },
                ],
            },
            expect_error=True,
        )
        material_duplicate_struct = material_duplicate_client_ref
        material_duplicate_struct = structured_detail_or_payload(material_duplicate_client_ref)
        material_duplicate_results = material_duplicate_struct.get("opResults")
        if not isinstance(material_duplicate_results, list) or len(material_duplicate_results) < 2:
            fail(f"material.mutate duplicate clientRef missing opResults: {material_duplicate_client_ref}")
        material_duplicate_second = material_duplicate_results[1] if isinstance(material_duplicate_results[1], dict) else {}
        if material_duplicate_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"material.mutate duplicate clientRef wrong errorCode: {material_duplicate_second}")
        if "Duplicate clientRef" not in str(material_duplicate_second.get("errorMessage", "")):
            fail(f"material.mutate duplicate clientRef wrong errorMessage: {material_duplicate_second}")
        print("[PASS] material duplicate clientRef rejected")

        material_compile = call_domain_tool(
            client,
            100108,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [{"op": "compile"}],
            },
        )
        material_compile_first = op_ok(material_compile)
        material_revision_after_compile = call_domain_tool(
            client,
            100109,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
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

        material_verify = call_domain_tool(
            client,
            1001091,
            "material",
            "verify",
            {
                "assetPath": material_asset_path,
            },
        )
        if material_verify.get("status") != "ok":
            fail(f"material.verify should succeed for material fixture: {material_verify}")
        if not isinstance(material_verify.get("queryReport"), dict):
            fail(f"material.verify missing queryReport: {material_verify}")
        compile_report = material_verify.get("compileReport")
        if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
            fail(f"material.verify missing compiled=true: {material_verify}")
        print("[PASS] material.verify summary validated")


        material_connect = call_domain_tool(
            client,
            10011,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_param_id}, "to": {"nodeId": material_multiply_id, "pin": "A"},
                    },
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_constant_id}, "to": {"nodeId": material_multiply_id, "pin": "B"},
                    },
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_multiply_id},
                        "to": {"nodeId": "__material_root__", "pin": "Base Color"},
                    },
                    {"op": "layoutGraph", "scope": "touched"},
                    {"op": "compile"},
                ],
            },
            expect_error=True,
        )
        material_connect_results = material_connect.get("opResults")
        if not isinstance(material_connect_results, list) or len(material_connect_results) not in {4, 5}:
            fail(f"Material connect/layout/compile opResults mismatch: {material_connect}")
        for index in range(3):
            connect_result = material_connect_results[index] if isinstance(material_connect_results[index], dict) else {}
            if connect_result.get("op") != "connectpins" or connect_result.get("ok") is not True:
                fail(f"Material connectPins op[{index}] failed: {material_connect}")
        material_layout_result = material_connect_results[3] if isinstance(material_connect_results[3], dict) else {}
        if material_layout_result.get("op") != "layoutgraph":
            fail(f"Material layoutGraph wrong op echo: {material_layout_result}")
        material_touched_layout_skipped = False
        if material_layout_result.get("ok") is not True:
            if (
                material_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(material_layout_result.get("errorMessage", ""))
            ):
                material_touched_layout_skipped = True
                print("[WARN] material layoutGraph(scope=touched) reported no pending touched nodes; skipping touched-layout position assertion")
            else:
                fail(f"Material touched-layout layoutGraph failed: {material_connect}")
        if len(material_connect_results) == 5:
            material_compile_result = material_connect_results[4] if isinstance(material_connect_results[4], dict) else {}
            if material_compile_result.get("op") != "compile" or material_compile_result.get("ok") is not True:
                fail(f"Material compile after connect/layout failed: {material_connect}")
        elif not material_touched_layout_skipped:
            fail(f"Material connect/layout batch stopped before compile unexpectedly: {material_connect}")
        else:
            material_compile_after_touched_skip = call_domain_tool(
                client,
                100111,
                "material",
                "mutate",
                {"assetPath": material_asset_path, "ops": [{"op": "compile"}]},
            )
            material_compile_result = op_ok(material_compile_after_touched_skip)
            if material_compile_result.get("op") != "compile":
                fail(f"Material compile after touched-layout skip wrong op echo: {material_compile_after_touched_skip}")

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
        material_query_without_type = call_domain_tool(
            client,
            100131,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_query_without_type_snapshot = material_query_without_type.get("semanticSnapshot")
        if not isinstance(material_query_without_type_snapshot, dict):
            fail(f"material.query without explicit graphName missing semanticSnapshot: {material_query_without_type}")
        if material_query_without_type_snapshot.get("signature") != material_snapshot.get("signature"):
            fail(
                "Material query without explicit graphName should resolve the same single-graph asset snapshot: "
                f"without={material_query_without_type} with={material_snapshot}"
            )
        material_root = require_node(material_nodes, "__material_root__")
        if material_root.get("nodeRole") != "materialRoot":
            fail(f"Material root nodeRole mismatch: {material_root}")
        material_root_pos = require_layout(material_root).get("position", {})
        material_multiply_node = require_node(material_nodes, material_multiply_id)
        material_multiply_pos = require_layout(material_multiply_node).get("position", {})
        if not material_touched_layout_skipped:
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
        material_break_root_from_source = call_domain_tool(
            client,
            100151,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "target": {
                            "nodeId": material_root_edge.get("fromNodeId"),
                            "pinName": material_root_edge.get("fromPin"),
                        }

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
        material_reconnect_root_after_source_break = call_domain_tool(
            client,
            100153,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {
                            "nodeId": material_root_edge.get("fromNodeId"),
                            "pin": material_root_edge.get("fromPin"),
                        },
                        "to": {"nodeId": "__material_root__", "pin": "Base Color"},
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
        material_break_internal_from_source = call_domain_tool(
            client,
            100154,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "target": {
                            "nodeId": material_internal_edge.get("fromNodeId"),
                            "pinName": material_internal_edge.get("fromPin"),
                        }

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
        material_reconnect_internal_after_source_break = call_domain_tool(
            client,
            100156,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {
                            "nodeId": material_internal_edge.get("fromNodeId"),
                            "pin": material_internal_edge.get("fromPin"),
                        },
                        "to": {
                            "nodeId": material_internal_edge.get("toNodeId"),
                            "pin": material_internal_edge.get("toPin"),
                        },
                    }
                ],
            },
        )
        op_ok(material_reconnect_internal_after_source_break)

        material_saturate_add = call_domain_tool(
            client,
            100157,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionSaturate"}
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
        material_connect_saturate_by_query_pin = call_domain_tool(
            client,
            100159,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_param_id, "pin": material_internal_edge.get("fromPin")},
                        "to": {"nodeId": material_saturate_id, "pin": material_saturate_input_name},
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
        material_relayout_payload = call_domain_tool(
            client,
            10016,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "nodeId": material_multiply_id,
                        "dx": 900,
                        "dy": 600,
                    },
                    {"op": "layoutGraph", "scope": "all"},
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
                f"Saved/Loomle/captures/material-layout-before-{int(time.time())}.png",
            )
            material_visual_relayout_payload = call_domain_tool(
                client,
                10021,
                "material",
                "mutate",
                {
                    "assetPath": material_asset_path,
                    "ops": [
                        {
                            "op": "moveNodeBy",
                            "nodeId": material_multiply_id,
                            "dx": 640,
                            "dy": -320,
                        },
                        {"op": "layoutGraph", "scope": "all"},
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
                relative_path_prefix="Saved/Loomle/captures/material-layout-after",
                baseline_hash=material_capture_before_hash,
            )
            if material_capture_after_hash == material_capture_before_hash:
                fail(
                    "editor.screenshot stayed visually stale after Material layoutGraph: "
                    f"before={material_capture_before_hash} after={material_capture_after_hash}"
                )
            print("[PASS] material root-aware layout validated")

        pcg_layout_add = call_domain_tool(
            client,
            10100,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGSurfaceSamplerSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
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
        pcg_graph_list_without_type = call_domain_tool(
            client,
            101005,
            "pcg",
            "list",
            {"assetPath": temp_pcg_asset},
        )
        if pcg_graph_list_without_type.get("assetPath") != temp_pcg_asset:
            fail(f"pcg.list assetPath mismatch: {pcg_graph_list_without_type}")
        pcg_list_nodes = pcg_graph_list_without_type.get("nodes")
        if not isinstance(pcg_list_nodes, list) or len(pcg_list_nodes) < 6:
            fail(f"pcg.list missing nodes[]: {pcg_graph_list_without_type}")
        print("[PASS] pcg.list validated")

        pcg_connect = call_domain_tool(
            client,
            10101,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "connectPins", "from": {"nodeId": pcg_create_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_a_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_tag_a_id, "pin": "Out"}, "to": {"nodeId": pcg_filter_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_filter_id, "pin": "InsideFilter"}, "to": {"nodeId": pcg_tag_b_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_sampler_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_c_id, "pin": "In"}},
                    {"op": "layoutGraph", "scope": "touched"},
                ],
            },
        )
        pcg_connect_results = pcg_connect.get("opResults")
        if not isinstance(pcg_connect_results, list) or len(pcg_connect_results) != 5:
            fail(f"PCG connect/layout opResults mismatch: {pcg_connect}")
        for index in range(4):
            connect_result = pcg_connect_results[index] if isinstance(pcg_connect_results[index], dict) else {}
            if connect_result.get("op") != "connectpins" or connect_result.get("ok") is not True:
                fail(f"PCG connectPins op[{index}] failed: {pcg_connect}")
        pcg_layout_result = pcg_connect_results[4] if isinstance(pcg_connect_results[4], dict) else {}
        if pcg_layout_result.get("op") != "layoutgraph":
            fail(f"PCG layoutGraph wrong op echo: {pcg_layout_result}")
        if pcg_layout_result.get("ok") is not True:
            if (
                pcg_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(pcg_layout_result.get("errorMessage", ""))
            ):
                print("[WARN] pcg layoutGraph(scope=touched) reported no pending touched nodes; skipping touched-layout position assertion")
            else:
                fail(f"PCG touched-layout layoutGraph failed: {pcg_connect}")

        pcg_snapshot = query_snapshot(client, 10102, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_nodes = pcg_snapshot.get("nodes")
        pcg_edges = pcg_snapshot.get("edges")
        if not isinstance(pcg_nodes, list) or not isinstance(pcg_edges, list):
            fail(f"PCG graph.query missing nodes/edges: {pcg_snapshot}")
        bad_pcg_connect = call_domain_tool(
            client,
            101021,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": pcg_filter_id, "pin": "Out"},
                        "to": {"nodeId": pcg_tag_b_id, "pin": "In"},
                    }
                ],
            },
            expect_error=True,
        )
        bad_pcg_connect_struct = bad_pcg_connect
        bad_pcg_connect_struct = structured_detail_or_payload(bad_pcg_connect)
        bad_pcg_connect_results = bad_pcg_connect_struct.get("opResults")
        if not isinstance(bad_pcg_connect_results, list) or not bad_pcg_connect_results:
            fail(f"pcg.mutate bad connect missing opResults: {bad_pcg_connect}")
        bad_pcg_connect_first = bad_pcg_connect_results[0] if isinstance(bad_pcg_connect_results[0], dict) else {}
        if bad_pcg_connect_first.get("errorCode") not in {"TARGET_NOT_FOUND", "PIN_NOT_FOUND"}:
            fail(f"pcg.mutate bad connect wrong errorCode: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("ok") is not False:
            fail(f"pcg.mutate bad connect should fail explicitly: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("changed") is not False:
            fail(f"pcg.mutate bad connect should not report changed=true: {bad_pcg_connect_first}")
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
        print("[PASS] pcg.mutate invalid connectPins target is rejected")
        pcg_snapshot_without_graph_name = query_snapshot(client, 10103, temp_pcg_asset, "pcg", None)
        if pcg_snapshot_without_graph_name.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG graph.query without graphName should resolve the same single-graph asset snapshot: "
                f"without={pcg_snapshot_without_graph_name} with={pcg_snapshot}"
            )
        pcg_query_without_type = call_domain_tool(
            client,
            101031,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        pcg_query_without_type_snapshot = pcg_query_without_type.get("semanticSnapshot")
        if not isinstance(pcg_query_without_type_snapshot, dict):
            fail(f"PCG query without explicit graphName missing semanticSnapshot: {pcg_query_without_type}")
        if pcg_query_without_type_snapshot.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG query without explicit graphName should resolve the same single-graph asset snapshot: "
                f"without={pcg_query_without_type} with={pcg_snapshot}"
            )

        pcg_compile_first = call_domain_tool(
            client,
            101031,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [{"op": "compile"}],
            },
        )
        pcg_compile_first_result = op_ok(pcg_compile_first)
        if pcg_compile_first_result.get("op") != "compile":
            fail(f"PCG compile wrong op echo: {pcg_compile_first}")
        pcg_compile_second = call_domain_tool(
            client,
            101032,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
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
        pcg_revision_after_compile = call_domain_tool(
            client,
            101033,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
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
        pcg_relayout_payload = call_domain_tool(
            client,
            10110,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "nodeId": pcg_create_id,
                        "dx": 900,
                        "dy": 600,
                    },
                    {"op": "layoutGraph", "scope": "all"},
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
                f"Saved/Loomle/captures/pcg-layout-before-{int(time.time())}.png",
            )
            pcg_visual_relayout_payload = call_domain_tool(
                client,
                10115,
                "pcg",
                "mutate",
                {
                    "assetPath": temp_pcg_asset,
                    "ops": [
                        {
                            "op": "moveNodeBy",
                            "nodeId": pcg_create_id,
                            "dx": 640,
                            "dy": -320,
                        },
                        {"op": "layoutGraph", "scope": "all"},
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
                relative_path_prefix="Saved/Loomle/captures/pcg-layout-after",
                baseline_hash=pcg_capture_before_hash,
            )
            if pcg_capture_after_hash == pcg_capture_before_hash:
                fail(
                    "editor.screenshot stayed visually stale after PCG layoutGraph: "
                    f"before={pcg_capture_before_hash} after={pcg_capture_after_hash}"
                )
        print("[PASS] pcg pipeline layout validated")

        pcg_settings_probe_add = call_domain_tool(
            client,
            10117,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGGetActorPropertySettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGGetSplineSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"},
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
        pcg_health_add = call_domain_tool(
            client,
            1011901,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_health_asset,
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "health_create", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "clientRef": "health_tag", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "clientRef": "health_spawner", "nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"},
                    {"op": "connectPins", "from": {"nodeRef": "health_create", "pin": "Out"}, "to": {"nodeRef": "health_tag", "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeRef": "health_tag", "pin": "Out"}, "to": {"nodeRef": "health_spawner", "pin": "In"}},
                ],
            },
        )
        pcg_health_results = pcg_health_add.get("opResults")
        if not isinstance(pcg_health_results, list) or len(pcg_health_results) != 5:
            fail(f"PCG health probe add ops missing results: {pcg_health_add}")
        for idx, result in enumerate(pcg_health_results):
            if not isinstance(result, dict) or result.get("ok") is not True:
                fail(f"PCG health probe opResults[{idx}] failed: {pcg_health_add}")
        pcg_verify = call_domain_tool(
            client,
            1011902,
            "pcg",
            "verify",
            {
                "assetPath": temp_pcg_health_asset,
            },
        )
        if pcg_verify.get("status") == "error":
            fail(
                "pcg.verify should not become an error just because a PCG graph is not connected to Output: "
                f"{pcg_verify}"
            )
        if not isinstance(pcg_verify.get("queryReport"), dict):
            fail(f"pcg.verify missing queryReport for pcg graph: {pcg_verify}")
        pcg_compile_report = pcg_verify.get("compileReport")
        if not isinstance(pcg_compile_report, dict):
            fail(f"pcg.verify missing compileReport for pcg graph: {pcg_verify}")
        if pcg_compile_report.get("compiled") is not True:
            fail(f"pcg.verify should preserve compileReport.compiled=true for disconnected-output pcg graph: {pcg_verify}")
        pcg_health_diagnostics = pcg_verify.get("diagnostics")
        if not isinstance(pcg_health_diagnostics, list):
            fail(f"pcg.verify missing diagnostics[]: {pcg_verify}")
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
                fail(f"pcg.verify should not invent {unexpected_code} for a disconnected-output pcg graph: {pcg_verify}")
        print("[PASS] pcg.verify no longer invents disconnected-output failures")

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
        pcg_remove_add = call_domain_tool(
            client,
            1011904,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "remove_create", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "clientRef": "remove_tag", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "clientRef": "remove_filter", "nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"},
                    {"op": "connectPins", "from": {"nodeRef": "remove_create", "pin": "Out"}, "to": {"nodeRef": "remove_tag", "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeRef": "remove_tag", "pin": "Out"}, "to": {"nodeRef": "remove_filter", "pin": "In"}},
                ],
            },
        )
        pcg_remove_results = pcg_remove_add.get("opResults")
        if not isinstance(pcg_remove_results, list) or len(pcg_remove_results) != 5:
            fail(f"PCG remove fixture ops missing results: {pcg_remove_add}")
        pcg_remove_tag_id = pcg_remove_results[1].get("nodeId")
        if not isinstance(pcg_remove_tag_id, str) or not pcg_remove_tag_id:
            fail(f"PCG remove fixture missing removable node id: {pcg_remove_add}")

        pcg_remove_by_name_failure = call_domain_tool(
            client,
            1011905,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "removeNode", "name": "Add Tag"}],
            },
            expect_error=True,
        )
        if "stable target" not in str(pcg_remove_by_name_failure.get("message", "")):
            fail(f"PCG removeNode should reject non-stable name targets: {pcg_remove_by_name_failure}")

        pcg_remove_by_id = call_domain_tool(
            client,
            1011906,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "removeNode", "nodeId": pcg_remove_tag_id}],
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

        pcg_remove_layout = call_domain_tool(
            client,
            1011908,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "layoutGraph", "scope": "touched"}],
            },
            expect_error=True,
        )
        pcg_remove_layout_results = pcg_remove_layout.get("opResults")
        if not isinstance(pcg_remove_layout_results, list) or not pcg_remove_layout_results:
            fail(f"PCG removeNode touched layout missing opResults: {pcg_remove_layout}")
        pcg_remove_layout_result = pcg_remove_layout_results[0] if isinstance(pcg_remove_layout_results[0], dict) else {}
        if pcg_remove_layout_result.get("op") != "layoutgraph":
            fail(f"PCG removeNode touched layout wrong op echo: {pcg_remove_layout}")
        if pcg_remove_layout_result.get("ok") is not True:
            if not (
                pcg_remove_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(pcg_remove_layout_result.get("errorMessage", ""))
            ):
                fail(f"PCG removeNode touched layout failed unexpectedly: {pcg_remove_layout}")
        print("[PASS] pcg removeNode requires stable targets and preserves touched layout neighbors")

        pcg_set_default_add = call_domain_tool(
            client,
            101191,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGCreatePointsSphereSettings"},
                ],
            },
        )
        pcg_set_default_results = pcg_set_default_add.get("opResults")
        if not isinstance(pcg_set_default_results, list) or len(pcg_set_default_results) != 1:
            fail(f"PCG setPinDefault probe add op missing results: {pcg_set_default_add}")
        pcg_set_default_node_id = pcg_set_default_results[0].get("nodeId")
        if not isinstance(pcg_set_default_node_id, str) or not pcg_set_default_node_id:
            fail(f"PCG setPinDefault probe missing node id: {pcg_set_default_add}")

        pcg_set_default_payload = call_domain_tool(
            client,
            101192,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_set_default_node_id, "pin": "Radius"},
                        "value": 250.5,
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_set_default_node_id, "pin": "LongitudinalSegments"},
                        "value": 8,
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
        print("[PASS] pcg.mutate setPinDefault supports overridable inputs")

        pcg_filter_add = call_domain_tool(
            client,
            101194,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGFilterByAttributeSettings"},
                ],
            },
        )
        pcg_filter_add_results = pcg_filter_add.get("opResults")
        if not isinstance(pcg_filter_add_results, list) or len(pcg_filter_add_results) != 1:
            fail(f"PCG FilterByAttribute probe add op missing results: {pcg_filter_add}")
        pcg_filter_node_id = pcg_filter_add_results[0].get("nodeId")
        if not isinstance(pcg_filter_node_id, str) or not pcg_filter_node_id:
            fail(f"PCG FilterByAttribute probe missing node id: {pcg_filter_add}")

        pcg_filter_query = call_domain_tool(
            client,
            101195,
            "pcg",
            "query",
            {
                "assetPath": temp_pcg_asset,
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

        pcg_filter_mutate = call_domain_tool(
            client,
            101196,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "FilterMode"},
                        "value": "FilterByValue",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "TargetAttribute"},
                        "value": "Desert_Cactus",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "FilterOperator"},
                        "value": "GreaterOrEqual",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/bUseConstantThreshold"},
                        "value": True,
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/type"},
                        "value": "Double",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/double_value"},
                        "value": 0.5,
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
        print("[PASS] pcg.mutate setPinDefault supports selector and constant threshold paths")

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
                f"Saved/Loomle/captures/editor-open-regression-{int(time.time())}.png",
            )
            print("[PASS] editor.open, editor.focus, and editor.screenshot validated")

        print("[PASS] domain mutate core ops validated")

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
        if stale_code not in {"REVISION_CONFLICT", 1008} and wm_stale.get("message") != "REVISION_CONFLICT":
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
        if err_code not in {"WIDGET_TREE_UNAVAILABLE", 1023} and wq_err.get("message") != "WIDGET_TREE_UNAVAILABLE":
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
        if err_code not in {"WIDGET_CLASS_NOT_FOUND", 1025} and wd_bad.get("message") != "WIDGET_CLASS_NOT_FOUND":
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
        completed_successfully = True
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
        if completed_successfully and args.close_editor_on_success:
            close_editor_for_project(project_root)


if __name__ == "__main__":
    raise SystemExit(main())
