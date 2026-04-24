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
    make_temp_asset_path,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tests" / "tools"))
from domain_test_helpers import blank_surface_matrix, compact_json, flatten_graph_mutate_ops, wait_for_bridge_ready  # noqa: E402


class MaterialWorkflowSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


WORKFLOW_CASES = [
    {
        "id": "root_sink_then_layout",
        "payloadFixture": "tests/fixtures/workflows/material/root-sink-then-layout.json",
        "families": ["expression", "parameter"],
        "expectedNodes": ["scalar_a", "scalar_b", "multiply_ab"],
        "expectedEdges": [
            ("scalar_a", "", "multiply_ab", "A"),
            ("scalar_b", "", "multiply_ab", "B"),
            ("multiply_ab", "", "__material_root__", "Base Color"),
        ],
        "queryPins": [
            ("multiply_ab", ["A", "B"]),
        ],
        "rootPins": ["Base Color"],
    },
    {
        "id": "insert_multiply_before_base_color_root",
        "payloadFixture": "tests/fixtures/workflows/material/insert-multiply-before-base-color-root.json",
        "families": ["expression", "parameter", "texture"],
        "expectedNodes": ["albedo_tex", "tint_scalar", "multiply_tint"],
        "expectedEdges": [
            ("albedo_tex", "", "multiply_tint", "A"),
            ("tint_scalar", "", "multiply_tint", "B"),
            ("multiply_tint", "", "__material_root__", "Base Color"),
        ],
        "absentEdges": [
            ("albedo_tex", "", "__material_root__", "Base Color"),
        ],
        "queryPins": [
            ("multiply_tint", ["A", "B"]),
        ],
        "rootPins": ["Base Color"],
    },
    {
        "id": "insert_one_minus_before_multiply_b",
        "payloadFixture": "tests/fixtures/workflows/material/insert-one-minus-before-multiply-b.json",
        "families": ["expression", "parameter", "texture"],
        "expectedNodes": ["albedo_tex", "tint_scalar", "multiply_tint", "invert_tint"],
        "expectedEdges": [
            ("albedo_tex", "", "multiply_tint", "A"),
            ("tint_scalar", "", "invert_tint", "Input"),
            ("invert_tint", "", "multiply_tint", "B"),
            ("multiply_tint", "", "__material_root__", "Base Color"),
        ],
        "absentEdges": [
            ("tint_scalar", "", "multiply_tint", "B"),
        ],
        "queryPins": [
            ("invert_tint", ["Input"]),
            ("multiply_tint", ["A", "B"]),
        ],
        "rootPins": ["Base Color"],
    },
    {
        "id": "replace_saturate_with_one_minus",
        "payloadFixture": "tests/fixtures/workflows/material/replace-saturate-with-one-minus.json",
        "families": ["expression", "parameter"],
        "expectedNodes": ["roughness_scalar", "replacement_invert"],
        "absentNodes": ["old_saturate"],
        "expectedEdges": [
            ("roughness_scalar", "", "replacement_invert", "Input"),
            ("replacement_invert", "", "__material_root__", "Roughness"),
        ],
        "absentEdges": [
            ("roughness_scalar", "", "old_saturate", "Input"),
            ("old_saturate", "", "__material_root__", "Roughness"),
        ],
        "queryPins": [
            ("replacement_invert", ["Input"]),
        ],
        "rootPins": ["Roughness"],
    },
    {
        "id": "replace_multiply_with_lerp",
        "payloadFixture": "tests/fixtures/workflows/material/replace-multiply-with-lerp.json",
        "families": ["expression", "parameter"],
        "expectedNodes": ["color_a", "color_b", "alpha_control", "replacement_lerp"],
        "absentNodes": ["old_multiply"],
        "expectedEdges": [
            ("color_a", "", "replacement_lerp", "A"),
            ("color_b", "", "replacement_lerp", "B"),
            ("alpha_control", "", "replacement_lerp", "Alpha"),
            ("replacement_lerp", "", "__material_root__", "Base Color"),
        ],
        "absentEdges": [
            ("color_a", "", "old_multiply", "A"),
            ("color_b", "", "old_multiply", "B"),
            ("old_multiply", "", "__material_root__", "Base Color"),
        ],
        "queryPins": [
            ("replacement_lerp", ["A", "B", "Alpha"]),
        ],
        "rootPins": ["Base Color"],
    },
]


def load_case_payload(case: dict[str, Any]) -> dict[str, Any]:
    path = REPO_ROOT / str(case["payloadFixture"])
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise MaterialWorkflowSuiteError("case_definition_gap", f"workflow payload is not an object: {path}")
    return payload


def list_cases_payload() -> dict[str, Any]:
    family_counter: Counter[str] = Counter()
    for case in WORKFLOW_CASES:
        for family in case.get("families", []):
            family_counter[family] += 1
    return {
        "suite": "workflow_truth",
        "graphType": "material",
        "summary": {
            "totalCases": len(WORKFLOW_CASES),
            "families": sorted(family_counter),
            "payloadFixtureBackedCases": sum(1 for case in WORKFLOW_CASES if case.get("payloadFixture")),
        },
        "cases": [
            {
                "id": case["id"],
                "payloadFixture": case["payloadFixture"],
                "families": case.get("families", []),
                "expectedNodes": len(case.get("expectedNodes", [])),
                "expectedEdges": len(case.get("expectedEdges", [])),
            }
            for case in WORKFLOW_CASES
        ],
    }


def create_material_fixture(client: McpStdioClient, request_id_base: int, *, asset_path: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base,
        code=(
            "import json\n"
            "import unreal\n"
            f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
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
    parsed = parse_execute_json(payload)
    if not isinstance(parsed.get("assetPath"), str) or not parsed["assetPath"]:
        raise MaterialWorkflowSuiteError("fixture_gap", f"material fixture missing assetPath: {compact_json(payload)}")
    return parsed


def cleanup_material_fixture(client: McpStdioClient, request_id_base: int, *, asset_path: str) -> None:
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


def query_material_snapshot(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "material.query",
        {"assetPath": asset_path, "includeConnections": True},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise MaterialWorkflowSuiteError("query_gap", f"material.query missing semanticSnapshot: {compact_json(payload)}")
    nodes = snapshot.get("nodes")
    edges = snapshot.get("edges")
    if not isinstance(nodes, list) or not isinstance(edges, list):
        raise MaterialWorkflowSuiteError("query_gap", f"material.query missing nodes/edges: {compact_json(payload)}")
    return snapshot


def verify_material_graph(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "material.verify",
        {"assetPath": asset_path},
    )
    if payload.get("status") == "error":
        raise MaterialWorkflowSuiteError("verify_gap", f"material.verify returned error: {compact_json(payload)}")
    compile_report = payload.get("compileReport")
    if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
        raise MaterialWorkflowSuiteError("verify_gap", f"material.verify missing compiled=true: {compact_json(payload)}")
    diagnostics = payload.get("diagnostics")
    if not isinstance(diagnostics, list):
        raise MaterialWorkflowSuiteError("verify_gap", f"material.verify missing diagnostics[]: {compact_json(payload)}")
    return {
        "status": payload.get("status"),
        "compiled": True,
        "diagnosticCount": len(diagnostics),
    }


def build_client_ref_map(payload: dict[str, Any], mutate_result: dict[str, Any]) -> dict[str, str]:
    ref_map: dict[str, str] = {"__material_root__": "__material_root__"}
    ops = payload.get("ops")
    op_results = mutate_result.get("opResults")
    if not isinstance(ops, list) or not isinstance(op_results, list) or len(ops) != len(op_results):
        raise MaterialWorkflowSuiteError(
            "mutate_result_gap",
            f"material workflow mutate result missing aligned ops/opResults: {compact_json(mutate_result)}",
        )
    for op, result in zip(ops, op_results):
        if not isinstance(op, dict) or not isinstance(result, dict):
            continue
        if op.get("op") != "addNode.byClass":
            continue
        client_ref = op.get("clientRef")
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


def has_edge(snapshot: dict[str, Any], from_node_id: str, from_pin: str, to_node_id: str, to_pin: str) -> bool:
    edges = snapshot.get("edges")
    if not isinstance(edges, list):
        return False
    return any(
        isinstance(edge, dict)
        and edge.get("fromNodeId") == from_node_id
        and (from_pin == "" or edge.get("fromPin") == from_pin)
        and edge.get("toNodeId") == to_node_id
        and edge.get("toPin") == to_pin
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
    details: dict[str, Any] = {
        "nodeChecks": {"present": [], "absent": []},
        "edgeChecks": {"present": [], "absent": []},
    }

    for ref in case.get("expectedNodes", []):
        node_id = ref_map.get(ref)
        if not isinstance(node_id, str) or find_node(snapshot, node_id) is None:
            raise MaterialWorkflowSuiteError("structure_gap", f"workflow missing expected node {ref}")
        details["nodeChecks"]["present"].append(ref)

    for ref in case.get("absentNodes", []):
        node_id = ref_map.get(ref)
        if not isinstance(node_id, str):
            continue
        if find_node(snapshot, node_id) is not None:
            raise MaterialWorkflowSuiteError("structure_gap", f"workflow expected removed node {ref} to be absent")
        details["nodeChecks"]["absent"].append(ref)

    for from_ref, from_pin, to_ref, to_pin in case.get("expectedEdges", []):
        from_node_id = ref_map.get(from_ref)
        to_node_id = ref_map.get(to_ref)
        if not isinstance(from_node_id, str) or not isinstance(to_node_id, str):
            raise MaterialWorkflowSuiteError(
                "mutate_result_gap",
                f"workflow missing clientRef mapping for edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}",
            )
        if not has_edge(snapshot, from_node_id, from_pin, to_node_id, to_pin):
            raise MaterialWorkflowSuiteError("structure_gap", f"workflow missing edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}")
        details["edgeChecks"]["present"].append({"from": from_ref, "fromPin": from_pin, "to": to_ref, "toPin": to_pin})

    for from_ref, from_pin, to_ref, to_pin in case.get("absentEdges", []):
        from_node_id = ref_map.get(from_ref)
        to_node_id = ref_map.get(to_ref)
        if not isinstance(from_node_id, str) or not isinstance(to_node_id, str):
            continue
        if has_edge(snapshot, from_node_id, from_pin, to_node_id, to_pin):
            raise MaterialWorkflowSuiteError(
                "structure_gap",
                f"workflow preserved forbidden edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}",
            )
        details["edgeChecks"]["absent"].append({"from": from_ref, "fromPin": from_pin, "to": to_ref, "toPin": to_pin})

    return details


def audit_workflow_query_truth(case: dict[str, Any], snapshot: dict[str, Any], ref_map: dict[str, str]) -> dict[str, Any]:
    root_node = find_node(snapshot, "__material_root__")
    if not isinstance(root_node, dict):
        raise MaterialWorkflowSuiteError("query_truth_gap", "material workflow missing __material_root__")
    if root_node.get("nodeRole") != "materialRoot":
        raise MaterialWorkflowSuiteError("query_truth_gap", f"material root nodeRole mismatch: {root_node}")

    root_pin_names = node_pin_names(root_node)
    for root_pin in case.get("rootPins", []):
        if root_pin not in root_pin_names:
            raise MaterialWorkflowSuiteError("query_truth_gap", f"material root missing pin {root_pin}")

    node_pin_checks: list[dict[str, Any]] = []
    for node_ref, expected_pins in case.get("queryPins", []):
        node_id = ref_map.get(node_ref)
        if not isinstance(node_id, str):
            raise MaterialWorkflowSuiteError("mutate_result_gap", f"workflow missing clientRef mapping for query pins {node_ref}")
        node = find_node(snapshot, node_id)
        if not isinstance(node, dict):
            raise MaterialWorkflowSuiteError("structure_gap", f"workflow node missing for query pins {node_ref}")
        actual_pins = node_pin_names(node)
        missing = [pin for pin in expected_pins if pin not in actual_pins]
        if missing:
            raise MaterialWorkflowSuiteError(
                "query_truth_gap",
                f"workflow query pins missing for {node_ref}: expected={expected_pins!r} actual={actual_pins!r}",
            )
        node_pin_checks.append({"nodeRef": node_ref, "pins": expected_pins, "surfacedPins": actual_pins})

    return {
        "rootNodeRole": root_node.get("nodeRole"),
        "rootPins": root_pin_names,
        "nodePinChecks": node_pin_checks,
    }


def run_workflow_case(client: McpStdioClient, *, request_id_base: int, case_index: int, case: dict[str, Any]) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "payloadFixture": case.get("payloadFixture"),
        "families": case.get("families", []),
        "status": "fail",
    }
    surface_matrix = blank_surface_matrix()
    asset_path = make_temp_asset_path(f"/Game/Codex/M_WorkflowTruth_{case['id']}")
    try:
        create_material_fixture(client, request_id_base, asset_path=asset_path)
        payload = load_case_payload(case)
        payload["assetPath"] = asset_path
        mutate_args = flatten_graph_mutate_ops(payload)
        mutate_result = call_tool(client, request_id_base + 10, "material.mutate", mutate_args)
        surface_matrix["mutate"] = "pass"
        ref_map = build_client_ref_map(mutate_args, mutate_result)
        snapshot = query_material_snapshot(client, request_id_base + 20, asset_path)
        structure_details = assert_workflow_structure(case, snapshot, ref_map)
        surface_matrix["queryStructure"] = "pass"
        query_truth_details = audit_workflow_query_truth(case, snapshot, ref_map)
        surface_matrix["queryTruth"] = "pass"
        verify_details = verify_material_graph(client, request_id_base + 30, asset_path)
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
    except MaterialWorkflowSuiteError as exc:
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
        else:
            surface_matrix["mutate"] = "fail"
        result["failureKind"] = exc.kind
        result["message"] = str(exc)
        result["details"] = {"surfaceMatrix": surface_matrix}
        return result
    finally:
        try:
            cleanup_material_fixture(client, request_id_base + 90, asset_path=asset_path)
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
    family_rows: dict[str, dict[str, Any]] = {}
    for result in results:
        for family in result.get("families", []):
            family_counter[family] += 1
            row = family_rows.setdefault(
                family,
                {"family": family, "totalCases": 0, "passed": 0, "failed": 0, "failureKinds": Counter()},
            )
            row["totalCases"] += 1
            if result.get("status") == "pass":
                row["passed"] += 1
            else:
                row["failed"] += 1
                if isinstance(result.get("failureKind"), str):
                    row["failureKinds"][result["failureKind"]] += 1
        if result.get("status") == "fail" and isinstance(result.get("failureKind"), str):
            failure_kinds[result["failureKind"]] += 1
        matrix = (result.get("details") or {}).get("surfaceMatrix", {})
        if isinstance(matrix, dict):
            for key in surface_matrix:
                value = matrix.get(key)
                if isinstance(value, str):
                    surface_matrix[key][value] += 1
    normalized_family_rows = []
    for family in sorted(family_rows):
        row = dict(family_rows[family])
        row["failureKinds"] = dict(sorted(row["failureKinds"].items()))
        normalized_family_rows.append(row)
    return {
        "totalCases": len(results),
        "passed": sum(1 for result in results if result.get("status") == "pass"),
        "failed": sum(1 for result in results if result.get("status") == "fail"),
        "families": dict(sorted(family_counter.items())),
        "failureKinds": dict(sorted(failure_kinds.items())),
        "surfaceMatrix": {key: dict(sorted(counter.items())) for key, counter in surface_matrix.items()},
        "familySummary": normalized_family_rows,
    }


def build_client(project_root: Path, timeout_s: float) -> McpStdioClient:
    return McpStdioClient(
        project_root=project_root,
        server_binary=resolve_default_loomle_binary(project_root),
        timeout_s=timeout_s,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run first-pass Material workflow truth cases.")
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
            result = run_workflow_case(client, request_id_base=5000 + case_index * 100, case_index=case_index, case=case)
            results.append(result)
            status = result.get("status")
            if status == "pass":
                print(f"[PASS] {case['id']}")
            else:
                print(f"[FAIL] {case['id']}: {result.get('failureKind')} {result.get('message')}")
    finally:
        client.close()

    report = {
        "version": "1",
        "graphType": "material",
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
