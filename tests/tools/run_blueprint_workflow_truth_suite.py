#!/usr/bin/env python3
import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_execute_exec_with_retry,
    call_tool,
    is_tool_error_payload,
    parse_tool_payload,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tests" / "tools"))
from domain_test_helpers import (  # noqa: E402
    blank_surface_matrix,
    blueprint_edit_args_from_legacy_payload,
    compact_json,
    wait_for_bridge_ready,
)


class BlueprintWorkflowSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


WORKFLOW_CASES = [
    {
        "id": "branch_local_subgraph",
        "example": "workspace/Loomle/blueprint/examples/executable/branch-local-subgraph.json",
        "families": ["branch", "function_call"],
        "expectedNodes": ["branch_main", "true_print", "false_print"],
        "expectedEdges": [
            ("branch_main", "Then", "true_print", "execute"),
            ("branch_main", "Else", "false_print", "execute"),
        ],
        "queryPins": {
            "branch_main": ["Condition", "Then", "Else"],
            "true_print": ["execute", "InString"],
            "false_print": ["execute", "InString"],
        },
    },
    {
        "id": "delay_local_chain",
        "example": "workspace/Loomle/blueprint/examples/executable/delay-local-chain.json",
        "families": ["function_call", "utility"],
        "expectedNodes": ["delay_main", "print_after_delay"],
        "expectedEdges": [
            ("delay_main", "then", "print_after_delay", "execute"),
        ],
        "queryPins": {
            "delay_main": ["execute", "then", "Duration"],
            "print_after_delay": ["execute", "InString"],
        },
    },
    {
        "id": "sequence_local_fanout",
        "example": "workspace/Loomle/blueprint/examples/executable/sequence-local-fanout.json",
        "families": ["function_call", "utility"],
        "expectedNodes": ["sequence_main", "print_first", "print_second"],
        "expectedEdges": [
            ("sequence_main", "Then_0", "print_first", "execute"),
            ("sequence_main", "Then_1", "print_second", "execute"),
        ],
        "queryPins": {
            "sequence_main": ["Then_0", "Then_1"],
            "print_first": ["execute", "InString"],
            "print_second": ["execute", "InString"],
        },
    },
    {
        "id": "replace_branch_with_sequence",
        "example": "workspace/Loomle/blueprint/examples/illustrative/replace-branch-with-sequence.json",
        "families": ["branch", "function_call", "utility"],
        "expectedNodes": ["replacement_sequence", "true_print", "false_print", "EventBeginPlay"],
        "absentNodes": ["old_branch"],
        "expectedEdges": [
            ("EventBeginPlay", "Then", "replacement_sequence", "execute"),
            ("replacement_sequence", "Then_0", "true_print", "execute"),
            ("replacement_sequence", "Then_1", "false_print", "execute"),
        ],
        "queryPins": {
            "EventBeginPlay": ["Then"],
            "replacement_sequence": ["execute", "Then_0", "Then_1"],
            "true_print": ["execute"],
            "false_print": ["execute"],
        },
    },
    {
        "id": "replace_delay_with_do_once",
        "example": "workspace/Loomle/blueprint/examples/illustrative/replace-delay-with-do-once.json",
        "families": ["function_call", "struct", "utility"],
        "expectedNodes": ["replacement_gate", "terminal_print", "EventBeginPlay"],
        "absentNodes": ["old_delay"],
        "expectedEdges": [
            ("EventBeginPlay", "Then", "replacement_gate", "Execute"),
            ("replacement_gate", "Completed", "terminal_print", "execute"),
        ],
        "queryPins": {
            "EventBeginPlay": ["Then"],
            "replacement_gate": ["Execute", "Completed"],
            "terminal_print": ["execute", "InString"],
        },
    },
]


def load_case_payload(case: dict[str, Any]) -> dict[str, Any]:
    path = REPO_ROOT / str(case["example"])
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise BlueprintWorkflowSuiteError("case_definition_gap", f"workflow payload is not an object: {path}")
    return payload


def list_cases_payload() -> dict[str, Any]:
    family_counter: Counter[str] = Counter()
    for case in WORKFLOW_CASES:
        for family in case.get("families", []):
            family_counter[family] += 1
    return {
        "version": "1",
        "suite": "workflow_truth",
        "graphType": "blueprint",
        "summary": {
            "totalCases": len(WORKFLOW_CASES),
            "families": sorted(family_counter),
            "exampleBackedCases": sum(1 for case in WORKFLOW_CASES if case.get("example")),
        },
        "cases": [
            {
                "id": case["id"],
                "example": case["example"],
                "families": case.get("families", []),
                "expectedNodes": len(case.get("expectedNodes", [])),
                "expectedEdges": len(case.get("expectedEdges", [])),
            }
            for case in WORKFLOW_CASES
        ],
    }


def create_blueprint_fixture(client: McpStdioClient, request_id_base: int, *, asset_path: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base,
        code=(
            "import json\n"
            "import unreal\n"
            f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
            "pkg_path, asset_name = asset.rsplit('/', 1)\n"
            "deleted_existing = False\n"
            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
            "  deleted_existing = unreal.EditorAssetLibrary.delete_asset(asset)\n"
            "  if not deleted_existing:\n"
            "    raise RuntimeError(f'failed to delete existing blueprint fixture: {asset}')\n"
            "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
            "factory = unreal.BlueprintFactory()\n"
            "factory.set_editor_property('ParentClass', unreal.Actor)\n"
            "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
            "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
            "print(json.dumps({'assetPath': asset, 'created': bp is not None, 'exists': exists, 'deletedExisting': deleted_existing}, ensure_ascii=False))\n"
        ),
    )
    parsed = parse_execute_json(payload)
    if not isinstance(parsed.get("assetPath"), str) or parsed.get("exists") is not True:
        raise BlueprintWorkflowSuiteError("fixture_gap", f"blueprint fixture creation missing assetPath/exists: {compact_json(payload)}")
    return parsed


def cleanup_blueprint_fixture(client: McpStdioClient, request_id_base: int, *, asset_path: str) -> None:
    _ = call_tool(
        client,
        request_id_base,
        "execute",
        {
            "mode": "exec",
            "code": (
                "import unreal\n"
                f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
                "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                "  unreal.EditorAssetLibrary.delete_asset(asset)\n"
            ),
        },
    )


def safe_call_tool(client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    payload = parse_tool_payload(response, f"tools/call.{name}")
    if is_tool_error_payload(payload):
        raise BlueprintWorkflowSuiteError("tool_error", f"{name} failed payload={compact_json(payload)}")
    return payload


def query_blueprint_snapshot(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = safe_call_tool(
        client,
        request_id,
        "blueprint.graph.inspect",
        {"assetPath": asset_path, "graphName": "EventGraph", "limit": 200},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise BlueprintWorkflowSuiteError("query_gap", f"blueprint.graph.inspect missing semanticSnapshot: {compact_json(payload)}")
    nodes = snapshot.get("nodes")
    edges = snapshot.get("edges")
    if not isinstance(nodes, list) or not isinstance(edges, list):
        raise BlueprintWorkflowSuiteError("query_gap", f"blueprint.graph.inspect missing nodes/edges: {compact_json(payload)}")
    return snapshot


def find_event_begin_play_node_id(snapshot: dict[str, Any]) -> str:
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        raise BlueprintWorkflowSuiteError("query_gap", "blueprint.graph.inspect missing nodes[] while resolving EventBeginPlay")
    for node in nodes:
        if not isinstance(node, dict):
            continue
        if node.get("title") == "Event BeginPlay" and isinstance(node.get("id"), str) and node["id"]:
            return node["id"]
    raise BlueprintWorkflowSuiteError("query_gap", "blueprint Event BeginPlay node not found in fixture graph")


def rewrite_live_node_ids(payload: dict[str, Any], snapshot: dict[str, Any]) -> dict[str, Any]:
    live_begin_play = find_event_begin_play_node_id(snapshot)

    def rewrite_value(value: Any) -> Any:
        if isinstance(value, dict):
            rewritten: dict[str, Any] = {}
            for key, child in value.items():
                if key == "nodeId" and child == "EventBeginPlay":
                    rewritten[key] = live_begin_play
                else:
                    rewritten[key] = rewrite_value(child)
            return rewritten
        if isinstance(value, list):
            return [rewrite_value(item) for item in value]
        return value

    return rewrite_value(payload)


def verify_blueprint_graph(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = safe_call_tool(
        client,
        request_id,
        "blueprint.validate",
        {"assetPath": asset_path, "graphName": "EventGraph", "limit": 200},
    )
    if payload.get("status") not in {"ok", "warn"}:
        raise BlueprintWorkflowSuiteError("verify_gap", f"blueprint.validate returned error: {compact_json(payload)}")
    compile_report = payload.get("compileReport")
    if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
        raise BlueprintWorkflowSuiteError("verify_gap", f"blueprint.validate missing compiled=true: {compact_json(payload)}")
    diagnostics = payload.get("diagnostics")
    if not isinstance(diagnostics, list):
        raise BlueprintWorkflowSuiteError("verify_gap", f"blueprint.validate missing diagnostics[]: {compact_json(payload)}")
    return {
        "status": payload.get("status"),
        "compiled": True,
        "diagnosticCount": len(diagnostics),
    }


def build_client_ref_map(payload: dict[str, Any], mutate_result: dict[str, Any]) -> dict[str, str]:
    ref_map: dict[str, str] = {}
    commands = payload.get("commands")
    op_results = mutate_result.get("opResults")
    if not isinstance(commands, list) or not isinstance(op_results, list) or len(commands) != len(op_results):
        raise BlueprintWorkflowSuiteError(
            "mutate_result_gap",
            f"blueprint workflow mutate result missing aligned commands/opResults: {compact_json(mutate_result)}",
        )
    for command, result in zip(commands, op_results):
        if not isinstance(command, dict) or not isinstance(result, dict):
            continue
        if command.get("kind") != "addNode":
            continue
        client_ref = command.get("alias")
        node_id = result.get("nodeId")
        if isinstance(client_ref, str) and client_ref and isinstance(node_id, str) and node_id:
            ref_map[client_ref] = node_id
    return ref_map


def find_node(snapshot: dict[str, Any], node_id: str) -> dict[str, Any] | None:
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        return None
    for node in nodes:
        if isinstance(node, dict) and node.get("id") == node_id:
            return node
    return None


def resolve_node_id(snapshot: dict[str, Any], ref_map: dict[str, str], ref: str) -> str | None:
    mapped = ref_map.get(ref)
    if isinstance(mapped, str) and mapped:
        return mapped
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        return None
    ref_lower = ref.lower()
    for node in nodes:
        if not isinstance(node, dict):
            continue
        node_id = node.get("id")
        title = node.get("title")
        name = node.get("name")
        if isinstance(node_id, str) and node_id == ref:
            return node_id
        if isinstance(title, str) and title.lower().replace(" ", "") == ref_lower.lower().replace(" ", ""):
            return node_id if isinstance(node_id, str) else None
        if ref == "EventBeginPlay" and isinstance(title, str) and title == "Event BeginPlay":
            return node_id if isinstance(node_id, str) else None
        if isinstance(name, str) and name == ref:
            return node_id if isinstance(node_id, str) else None
    return None


def has_edge(snapshot: dict[str, Any], from_node_id: str, from_pin: str, to_node_id: str, to_pin: str) -> bool:
    edges = snapshot.get("edges")
    if not isinstance(edges, list):
        return False
    return any(
        isinstance(edge, dict)
        and edge.get("fromNodeId") == from_node_id
        and str(edge.get("fromPin", "")).lower() == from_pin.lower()
        and edge.get("toNodeId") == to_node_id
        and str(edge.get("toPin", "")).lower() == to_pin.lower()
        for edge in edges
    )


def node_pin_names(node: dict[str, Any]) -> list[str]:
    pins = node.get("pins")
    if not isinstance(pins, list):
        return []
    names: list[str] = []
    for pin in pins:
        if not isinstance(pin, dict):
            continue
        name = pin.get("name")
        if isinstance(name, str) and name not in names:
            names.append(name)
    return names


def assert_workflow_structure(case: dict[str, Any], snapshot: dict[str, Any], ref_map: dict[str, str]) -> dict[str, Any]:
    details: dict[str, Any] = {"nodeChecks": {"present": [], "absent": []}, "edgeChecks": {"present": []}}

    for ref in case.get("expectedNodes", []):
        node_id = resolve_node_id(snapshot, ref_map, ref)
        if not isinstance(node_id, str) or find_node(snapshot, node_id) is None:
            raise BlueprintWorkflowSuiteError("structure_gap", f"workflow missing expected node {ref}")
        details["nodeChecks"]["present"].append(ref)

    for ref in case.get("absentNodes", []):
        node_id = resolve_node_id(snapshot, ref_map, ref)
        if not isinstance(node_id, str):
            continue
        if find_node(snapshot, node_id) is not None:
            raise BlueprintWorkflowSuiteError("structure_gap", f"workflow expected removed node {ref} to be absent")
        details["nodeChecks"]["absent"].append(ref)

    for from_ref, from_pin, to_ref, to_pin in case.get("expectedEdges", []):
        from_node_id = resolve_node_id(snapshot, ref_map, from_ref)
        to_node_id = resolve_node_id(snapshot, ref_map, to_ref)
        if not isinstance(from_node_id, str) or not isinstance(to_node_id, str):
            raise BlueprintWorkflowSuiteError(
                "mutate_result_gap",
                f"workflow missing clientRef mapping for edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}",
            )
        if not has_edge(snapshot, from_node_id, from_pin, to_node_id, to_pin):
            raise BlueprintWorkflowSuiteError("structure_gap", f"workflow missing edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}")
        details["edgeChecks"]["present"].append({"from": from_ref, "fromPin": from_pin, "to": to_ref, "toPin": to_pin})

    return details


def audit_workflow_query_truth(case: dict[str, Any], snapshot: dict[str, Any], ref_map: dict[str, str]) -> dict[str, Any]:
    node_pin_checks: list[dict[str, Any]] = []
    query_pins = case.get("queryPins", {})
    if not isinstance(query_pins, dict):
        raise BlueprintWorkflowSuiteError("case_definition_gap", f"workflow queryPins invalid: {case}")
    for node_ref, expected_pins in query_pins.items():
        node_id = resolve_node_id(snapshot, ref_map, node_ref)
        if not isinstance(node_id, str):
            raise BlueprintWorkflowSuiteError("mutate_result_gap", f"workflow missing clientRef mapping for query pins {node_ref}")
        node = find_node(snapshot, node_id)
        if not isinstance(node, dict):
            raise BlueprintWorkflowSuiteError("structure_gap", f"workflow node missing for query pins {node_ref}")
        actual_pins = node_pin_names(node)
        actual_lower = {pin.lower() for pin in actual_pins}
        missing = [pin for pin in expected_pins if pin.lower() not in actual_lower]
        if missing:
            raise BlueprintWorkflowSuiteError(
                "query_truth_gap",
                f"workflow query pins missing for {node_ref}: expected={expected_pins!r} actual={actual_pins!r}",
            )
        node_pin_checks.append({"nodeRef": node_ref, "pins": expected_pins, "surfacedPins": actual_pins})
    return {"nodePinChecks": node_pin_checks}


def run_workflow_case(client: McpStdioClient, *, request_id_base: int, case: dict[str, Any]) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "example": case.get("example"),
        "families": case.get("families", []),
        "status": "fail",
    }
    surface_matrix = blank_surface_matrix()
    asset_path = f"/Game/Codex/BP_WorkflowTruth_{case['id']}_{request_id_base}"
    try:
        create_blueprint_fixture(client, request_id_base, asset_path=asset_path)
        initial_snapshot = query_blueprint_snapshot(client, request_id_base + 5, asset_path)
        payload = rewrite_live_node_ids(load_case_payload(case), initial_snapshot)
        payload["assetPath"] = asset_path
        payload["graphName"] = "EventGraph"
        edit_payload = blueprint_edit_args_from_legacy_payload(payload)
        mutate_result = safe_call_tool(client, request_id_base + 10, "blueprint.graph.edit", edit_payload)
        surface_matrix["mutate"] = "pass"
        ref_map = build_client_ref_map(edit_payload, mutate_result)
        snapshot = query_blueprint_snapshot(client, request_id_base + 20, asset_path)
        structure_details = assert_workflow_structure(case, snapshot, ref_map)
        surface_matrix["queryStructure"] = "pass"
        query_truth_details = audit_workflow_query_truth(case, snapshot, ref_map)
        surface_matrix["queryTruth"] = "pass"
        verify_details = verify_blueprint_graph(client, request_id_base + 30, asset_path)
        surface_matrix["verify"] = "pass"
        surface_matrix["diagnostics"] = "pass"
        result["status"] = "pass"
        result["details"] = {
            "nodeCount": len(snapshot.get("nodes", [])) if isinstance(snapshot.get("nodes"), list) else None,
            "edgeCount": len(snapshot.get("edges", [])) if isinstance(snapshot.get("edges"), list) else None,
            "structure": structure_details,
            "queryTruth": query_truth_details,
            "verify": verify_details,
            "surfaceMatrix": surface_matrix,
        }
        return result
    except BlueprintWorkflowSuiteError as exc:
        if exc.kind == "structure_gap":
            surface_matrix["queryStructure"] = "fail"
        elif exc.kind == "query_truth_gap":
            surface_matrix["queryStructure"] = "pass"
            surface_matrix["queryTruth"] = "fail"
        elif exc.kind == "verify_gap":
            surface_matrix["queryStructure"] = "pass"
            surface_matrix["queryTruth"] = "pass"
            surface_matrix["verify"] = "fail"
            surface_matrix["diagnostics"] = "fail"
        elif exc.kind == "mutate_result_gap":
            surface_matrix["mutate"] = "fail"
        elif exc.kind == "tool_error":
            surface_matrix["mutate"] = "fail"
        else:
            surface_matrix["mutate"] = "fail"
        result["failureKind"] = exc.kind
        result["message"] = str(exc)
        result["details"] = {"surfaceMatrix": surface_matrix}
        return result
    finally:
        try:
            cleanup_blueprint_fixture(client, request_id_base + 90, asset_path=asset_path)
        except Exception:
            pass


def summarize_results(results: list[dict[str, Any]]) -> dict[str, Any]:
    family_counter: Counter[str] = Counter()
    failure_kinds: Counter[str] = Counter()
    surface_matrix: dict[str, Counter[str]] = {
        "mutate": Counter(),
        "queryStructure": Counter(),
        "queryTruth": Counter(),
        "verify": Counter(),
        "diagnostics": Counter(),
    }
    for result in results:
        for family in result.get("families", []):
            family_counter[family] += 1
        if result.get("status") == "fail" and isinstance(result.get("failureKind"), str):
            failure_kinds[result["failureKind"]] += 1
        matrix = (result.get("details") or {}).get("surfaceMatrix", {})
        if isinstance(matrix, dict):
            for key in surface_matrix:
                value = matrix.get(key)
                if isinstance(value, str):
                    surface_matrix[key][value] += 1
    return {
        "totalCases": len(results),
        "passed": sum(1 for result in results if result.get("status") == "pass"),
        "failed": sum(1 for result in results if result.get("status") == "fail"),
        "families": dict(sorted(family_counter.items())),
        "failureKinds": dict(sorted(failure_kinds.items())),
        "surfaceMatrix": {key: dict(sorted(counter.items())) for key, counter in surface_matrix.items()},
    }


def build_client(project_root: Path, timeout_s: float) -> McpStdioClient:
    return McpStdioClient(
        project_root=project_root,
        server_binary=resolve_default_loomle_binary(project_root),
        timeout_s=timeout_s,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run first-pass Blueprint workflow truth cases.")
    parser.add_argument("--project-root", default="", help="Path to Unreal project root (defaults via local config search).")
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds.")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--list-cases", action="store_true")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, "")
    results: list[dict[str, Any]] = []
    client = build_client(project_root, args.timeout)
    try:
        wait_for_bridge_ready(client)
        for case_index, case in enumerate(WORKFLOW_CASES, start=1):
            result = run_workflow_case(client, request_id_base=5000 + case_index * 100, case=case)
            results.append(result)
            if result.get("status") == "pass":
                print(f"[PASS] {case['id']}")
            else:
                print(f"[FAIL] {case['id']}: {result.get('failureKind')} {result.get('message')}")
    finally:
        client.close()

    report = {
        "version": "1",
        "graphType": "blueprint",
        "suite": "workflow_truth",
        "summary": summarize_results(results),
        "results": results,
    }
    text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
