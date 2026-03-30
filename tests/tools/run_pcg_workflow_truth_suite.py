#!/usr/bin/env python3
import argparse
import contextlib
import io
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_tool,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tests" / "tools"))
from run_pcg_graph_test_plan import (  # noqa: E402
    blank_surface_matrix,
    cleanup_pcg_fixture,
    compact_json,
    create_pcg_fixture,
    query_pcg_snapshot,
    wait_for_bridge_ready,
)


class WorkflowSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


WORKFLOW_CASES = [
    {
        "id": "actor_data_tag_route",
        "example": "workspace/Loomle/pcg/examples/actor-data-tag-route.json",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source", "route", "meta"],
        "expectedNodes": ["actor_data", "filter_by_tag", "tag_matched_branch"],
        "expectedEdges": [
            ("actor_data", "Out", "filter_by_tag", "In"),
            ("filter_by_tag", "InsideFilter", "tag_matched_branch", "In"),
        ],
        "queryDefaults": [
            ("filter_by_tag", "SelectedTags", "Gameplay.Spawnable"),
            ("tag_matched_branch", "TagsToAdd", "Gameplay.Routed"),
        ],
    },
    {
        "id": "surface_sample_to_static_mesh",
        "example": "workspace/Loomle/pcg/examples/surface-sample-to-static-mesh.json",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source", "sample", "spawn"],
        "expectedNodes": ["actor_surface", "surface_sampler", "static_mesh_spawner"],
        "expectedEdges": [
            ("actor_surface", "Out", "surface_sampler", "Surface"),
            ("surface_sampler", "Out", "static_mesh_spawner", "In"),
        ],
        "queryDefaults": [
            ("surface_sampler", "PointsPerSquaredMeter", 0.2),
        ],
    },
    {
        "id": "project_surface_from_actor_data",
        "example": "workspace/Loomle/pcg/examples/project-surface-from-actor-data.json",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source", "transform", "meta"],
        "expectedNodes": ["source_points", "projection_target", "project_surface", "tag_projected"],
        "expectedEdges": [
            ("source_points", "Out", "project_surface", "In"),
            ("projection_target", "Out", "project_surface", "Projection Target"),
            ("project_surface", "Out", "tag_projected", "In"),
        ],
        "queryDefaults": [
            ("tag_projected", "TagsToAdd", "Gameplay.Projected"),
        ],
    },
    {
        "id": "insert_density_filter_before_static_mesh",
        "example": "workspace/Loomle/pcg/examples/insert-density-filter-before-static-mesh.json",
        "fixture": "pcg_graph",
        "families": ["create", "filter", "spawn"],
        "expectedNodes": ["create_points", "density_filter", "static_mesh_spawner"],
        "expectedEdges": [
            ("create_points", "Out", "density_filter", "In"),
            ("density_filter", "Out", "static_mesh_spawner", "In"),
        ],
        "absentEdges": [
            ("create_points", "Out", "static_mesh_spawner", "In"),
        ],
        "queryDefaults": [
            ("density_filter", "LowerBound", 0.2),
            ("density_filter", "UpperBound", 0.8),
        ],
    },
    {
        "id": "replace_tag_route_with_attribute_route",
        "example": "workspace/Loomle/pcg/examples/replace-tag-route-with-attribute-route.json",
        "fixture": "pcg_graph",
        "families": ["create", "filter", "route", "meta"],
        "expectedNodes": ["create_points", "replacement_filter_by_attribute", "matched_branch", "unmatched_branch"],
        "absentNodes": ["old_filter_by_tag"],
        "expectedEdges": [
            ("create_points", "Out", "replacement_filter_by_attribute", "In"),
            ("replacement_filter_by_attribute", "InsideFilter", "matched_branch", "In"),
            ("replacement_filter_by_attribute", "OutsideFilter", "unmatched_branch", "In"),
        ],
        "queryDefaults": [
            ("replacement_filter_by_attribute", "FilterMode", "FilterByExistence"),
            ("replacement_filter_by_attribute", "Attribute", "Density"),
        ],
    },
    {
        "id": "transform_points_then_tag",
        "fixture": "pcg_graph",
        "families": ["create", "transform", "meta"],
        "payload": {
            "tool": "graph.mutate",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "transform_points",
                    "args": {"nodeClassPath": "/Script/PCG.PCGTransformPointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_transformed",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points", "pin": "Out"},
                        "to": {"nodeRef": "transform_points", "pin": "In"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "transform_points", "pin": "Out"},
                        "to": {"nodeRef": "tag_transformed", "pin": "In"},
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "transform_points", "pin": "bAbsoluteRotation"},
                        "value": True,
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "tag_transformed", "pin": "TagsToAdd"},
                        "value": "Gameplay.Transformed",
                    },
                },
                {"op": "layoutGraph", "args": {"scope": "touched"}},
            ],
        },
        "expectedNodes": ["create_points", "transform_points", "tag_transformed"],
        "expectedEdges": [
            ("create_points", "Out", "transform_points", "In"),
            ("transform_points", "Out", "tag_transformed", "In"),
        ],
        "queryDefaults": [
            ("transform_points", "bAbsoluteRotation", True),
            ("tag_transformed", "TagsToAdd", "Gameplay.Transformed"),
        ],
    },
    {
        "id": "branch_to_dual_tags",
        "fixture": "pcg_graph",
        "families": ["branch", "create", "meta"],
        "payload": {
            "tool": "graph.mutate",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "branch_node",
                    "args": {"nodeClassPath": "/Script/PCG.PCGBranchSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_output_a",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_output_b",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points", "pin": "Out"},
                        "to": {"nodeRef": "branch_node", "pin": "In"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "branch_node", "pin": "Output A"},
                        "to": {"nodeRef": "tag_output_a", "pin": "In"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "branch_node", "pin": "Output B"},
                        "to": {"nodeRef": "tag_output_b", "pin": "In"},
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "branch_node", "pin": "bOutputToB"},
                        "value": True,
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "tag_output_b", "pin": "TagsToAdd"},
                        "value": "Gameplay.BranchB",
                    },
                },
                {"op": "layoutGraph", "args": {"scope": "touched"}},
            ],
        },
        "expectedNodes": ["create_points", "branch_node", "tag_output_a", "tag_output_b"],
        "expectedEdges": [
            ("create_points", "Out", "branch_node", "In"),
            ("branch_node", "Output A", "tag_output_a", "In"),
            ("branch_node", "Output B", "tag_output_b", "In"),
        ],
        "queryDefaults": [
            ("branch_node", "bOutputToB", True),
            ("tag_output_b", "TagsToAdd", "Gameplay.BranchB"),
        ],
    },
    {
        "id": "boolean_select_between_two_sources",
        "fixture": "pcg_graph",
        "families": ["create", "meta", "select"],
        "payload": {
            "tool": "graph.mutate",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points_a",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points_b",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "select_node",
                    "args": {"nodeClassPath": "/Script/PCG.PCGBooleanSelectSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_selected",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points_a", "pin": "Out"},
                        "to": {"nodeRef": "select_node", "pin": "Input A"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points_b", "pin": "Out"},
                        "to": {"nodeRef": "select_node", "pin": "Input B"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "select_node", "pin": "Out"},
                        "to": {"nodeRef": "tag_selected", "pin": "In"},
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "select_node", "pin": "bUseInputB"},
                        "value": True,
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "tag_selected", "pin": "TagsToAdd"},
                        "value": "Gameplay.Selected",
                    },
                },
                {"op": "layoutGraph", "args": {"scope": "touched"}},
            ],
        },
        "expectedNodes": ["create_points_a", "create_points_b", "select_node", "tag_selected"],
        "expectedEdges": [
            ("create_points_a", "Out", "select_node", "Input A"),
            ("create_points_b", "Out", "select_node", "Input B"),
            ("select_node", "Out", "tag_selected", "In"),
        ],
        "queryDefaults": [
            ("select_node", "bUseInputB", True),
            ("tag_selected", "TagsToAdd", "Gameplay.Selected"),
        ],
    },
    {
        "id": "predicate_get_index_then_tag",
        "fixture": "pcg_graph",
        "families": ["create", "meta", "predicate"],
        "payload": {
            "tool": "graph.mutate",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "get_index",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAttributeGetFromIndexSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_indexed",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points", "pin": "Out"},
                        "to": {"nodeRef": "get_index", "pin": "In"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "get_index", "pin": "Out"},
                        "to": {"nodeRef": "tag_indexed", "pin": "In"},
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "get_index", "pin": "Index"},
                        "value": 2,
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "tag_indexed", "pin": "TagsToAdd"},
                        "value": "Gameplay.Predicate",
                    },
                },
                {"op": "layoutGraph", "args": {"scope": "touched"}},
            ],
        },
        "expectedNodes": ["create_points", "get_index", "tag_indexed"],
        "expectedEdges": [
            ("create_points", "Out", "get_index", "In"),
            ("get_index", "Out", "tag_indexed", "In"),
        ],
        "queryDefaults": [
            ("get_index", "Index", 2),
            ("tag_indexed", "TagsToAdd", "Gameplay.Predicate"),
        ],
    },
    {
        "id": "subgraph_depth_after_subgraph",
        "fixture": "pcg_graph",
        "families": ["create", "meta", "struct"],
        "payload": {
            "tool": "graph.mutate",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "create_points",
                    "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "subgraph_node",
                    "args": {"nodeClassPath": "/Script/PCG.PCGSubgraphSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "depth_node",
                    "args": {"nodeClassPath": "/Script/PCG.PCGGetSubgraphDepthSettings"},
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tag_depth",
                    "args": {"nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points", "pin": "Out"},
                        "to": {"nodeRef": "subgraph_node", "pin": "In"},
                    },
                },
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "subgraph_node", "pin": "Out"},
                        "to": {"nodeRef": "tag_depth", "pin": "In"},
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "depth_node", "pin": "DistanceRelativeToUpstreamGraph"},
                        "value": 2,
                    },
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "tag_depth", "pin": "TagsToAdd"},
                        "value": "Gameplay.StructDepth",
                    },
                },
                {"op": "layoutGraph", "args": {"scope": "touched"}},
            ],
        },
        "expectedNodes": ["create_points", "subgraph_node", "depth_node", "tag_depth"],
        "expectedEdges": [
            ("create_points", "Out", "subgraph_node", "In"),
            ("subgraph_node", "Out", "tag_depth", "In"),
        ],
        "queryDefaults": [
            ("depth_node", "DistanceRelativeToUpstreamGraph", 2),
            ("tag_depth", "TagsToAdd", "Gameplay.StructDepth"),
        ],
    },
]


def load_case_payload(case: dict[str, Any]) -> dict[str, Any]:
    inline_payload = case.get("payload")
    if isinstance(inline_payload, dict):
        return json.loads(json.dumps(inline_payload))
    example_path = REPO_ROOT / case["example"]
    return json.loads(example_path.read_text(encoding="utf-8"))


def list_cases_payload() -> dict[str, Any]:
    families = sorted(
        {
            family
            for case in WORKFLOW_CASES
            for family in case.get("families", [])
            if isinstance(family, str) and family
        }
    )
    return {
        "version": "1",
        "graphType": "pcg",
        "summary": {
            "totalCases": len(WORKFLOW_CASES),
            "worldContextCases": sum(1 for case in WORKFLOW_CASES if case["fixture"] == "pcg_graph_with_world_actor"),
            "families": families,
        },
        "cases": [
            {
                "id": case["id"],
                "example": case.get("example"),
                "fixture": case["fixture"],
                "families": case.get("families", []),
                "expectedNodes": len(case.get("expectedNodes", [])),
                "expectedEdges": len(case.get("expectedEdges", [])),
                "queryDefaults": len(case.get("queryDefaults", [])),
            }
            for case in WORKFLOW_CASES
        ],
    }


def build_client_ref_map(payload: dict[str, Any], mutate_result: dict[str, Any]) -> dict[str, str]:
    ref_map: dict[str, str] = {}
    ops = payload.get("ops")
    op_results = mutate_result.get("opResults")
    if not isinstance(ops, list) or not isinstance(op_results, list) or len(ops) != len(op_results):
        raise WorkflowSuiteError(
            "mutate_result_gap",
            f"workflow mutate result missing aligned ops/opResults: {compact_json(mutate_result)}",
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
        and edge.get("fromPin") == from_pin
        and edge.get("toNodeId") == to_node_id
        and edge.get("toPin") == to_pin
        for edge in edges
    )


def node_pin_surface_values(node: dict[str, Any], pin_name: str) -> list[Any]:
    pins = node.get("pins")
    if not isinstance(pins, list):
        return []
    for pin in pins:
        if not isinstance(pin, dict) or pin.get("name") != pin_name:
            continue
        default_obj = pin.get("default")
        values: list[Any] = []
        for value in (
            pin.get("defaultValue"),
            pin.get("defaultText"),
            default_obj.get("value") if isinstance(default_obj, dict) else None,
            default_obj.get("text") if isinstance(default_obj, dict) else None,
        ):
            if value in (None, ""):
                continue
            if value not in values:
                values.append(value)
        return values
    return []


def normalize_surface_value(value: Any) -> Any:
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"true", "false"}:
            return lowered == "true"
        try:
            if "." in lowered:
                return float(lowered)
            return int(lowered)
        except Exception:
            return value
    return value


def assert_workflow_structure(case: dict[str, Any], snapshot: dict[str, Any], ref_map: dict[str, str]) -> dict[str, Any]:
    details: dict[str, Any] = {
        "nodeChecks": {"present": [], "absent": []},
        "edgeChecks": {"present": [], "absent": []},
    }

    for ref in case.get("expectedNodes", []):
        node_id = ref_map.get(ref)
        if not isinstance(node_id, str) or find_node(snapshot, node_id) is None:
            raise WorkflowSuiteError("structure_gap", f"workflow missing expected node {ref}")
        details["nodeChecks"]["present"].append(ref)

    for ref in case.get("absentNodes", []):
        node_id = ref_map.get(ref)
        if not isinstance(node_id, str):
            continue
        if find_node(snapshot, node_id) is not None:
            raise WorkflowSuiteError("structure_gap", f"workflow expected removed node {ref} to be absent")
        details["nodeChecks"]["absent"].append(ref)

    for from_ref, from_pin, to_ref, to_pin in case.get("expectedEdges", []):
        from_node_id = ref_map.get(from_ref)
        to_node_id = ref_map.get(to_ref)
        if not isinstance(from_node_id, str) or not isinstance(to_node_id, str):
            raise WorkflowSuiteError(
                "mutate_result_gap",
                f"workflow missing clientRef mapping for edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}",
            )
        if not has_edge(snapshot, from_node_id, from_pin, to_node_id, to_pin):
            raise WorkflowSuiteError("structure_gap", f"workflow missing edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}")
        details["edgeChecks"]["present"].append(
            {"from": from_ref, "fromPin": from_pin, "to": to_ref, "toPin": to_pin}
        )

    for from_ref, from_pin, to_ref, to_pin in case.get("absentEdges", []):
        from_node_id = ref_map.get(from_ref)
        to_node_id = ref_map.get(to_ref)
        if not isinstance(from_node_id, str) or not isinstance(to_node_id, str):
            continue
        if has_edge(snapshot, from_node_id, from_pin, to_node_id, to_pin):
            raise WorkflowSuiteError(
                "structure_gap",
                f"workflow preserved forbidden edge {from_ref}:{from_pin} -> {to_ref}:{to_pin}",
            )
        details["edgeChecks"]["absent"].append(
            {"from": from_ref, "fromPin": from_pin, "to": to_ref, "toPin": to_pin}
        )

    return details


def audit_workflow_query_truth(case: dict[str, Any], snapshot: dict[str, Any], ref_map: dict[str, str]) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []
    for node_ref, pin_name, expected in case.get("queryDefaults", []):
        node_id = ref_map.get(node_ref)
        if not isinstance(node_id, str):
            raise WorkflowSuiteError(
                "mutate_result_gap",
                f"workflow missing clientRef mapping for query default {node_ref}.{pin_name}",
            )
        node = find_node(snapshot, node_id)
        if not isinstance(node, dict):
            raise WorkflowSuiteError("structure_gap", f"workflow node missing for query default {node_ref}.{pin_name}")
        surface_values = node_pin_surface_values(node, pin_name)
        if not surface_values:
            raise WorkflowSuiteError(
                "query_truth_unsurfaced",
                f"workflow query truth missing surfaced default for {node_ref}.{pin_name}",
            )
        expected_cmp = normalize_surface_value(expected)
        if not any(normalize_surface_value(value) == expected_cmp for value in surface_values):
            raise WorkflowSuiteError(
                "query_truth_mismatch",
                f"workflow query truth mismatch for {node_ref}.{pin_name}: expected={expected!r} surfaced={surface_values!r}"
            )
        checks.append(
            {"nodeRef": node_ref, "pin": pin_name, "expected": expected, "surfacedValues": surface_values}
        )
    return checks


def verify_graph(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "graph.verify",
        {"assetPath": asset_path, "graphName": "PCGGraph", "graphType": "pcg"},
    )
    if payload.get("status") == "error":
        raise WorkflowSuiteError("verify_gap", f"workflow graph.verify returned error: {compact_json(payload)}")
    compile_report = payload.get("compileReport")
    if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
        raise WorkflowSuiteError("verify_gap", f"workflow graph.verify missing compiled=true: {compact_json(payload)}")
    diagnostics = payload.get("diagnostics")
    if not isinstance(diagnostics, list):
        raise WorkflowSuiteError("verify_gap", f"workflow graph.verify missing diagnostics[]: {compact_json(payload)}")
    return {
        "status": payload.get("status"),
        "compiled": True,
        "diagnosticCount": len(diagnostics),
    }


def run_workflow_case(
    client: McpStdioClient,
    *,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "example": case.get("example"),
        "fixture": case["fixture"],
        "families": case.get("families", []),
        "status": "fail",
    }
    surface_matrix = blank_surface_matrix()
    asset_path = f"/Game/Codex/PCGWorkflowTruth/{case['id']}_{case_index}"
    actor_path: str | None = None
    try:
        fixture_info = create_pcg_fixture(
            client,
            request_id_base,
            asset_path=asset_path,
            fixture_id=case["fixture"],
            actor_offset=float(case_index * 100),
        )
        actor_path = fixture_info.get("actorPath") if isinstance(fixture_info.get("actorPath"), str) else None

        payload = load_case_payload(case)
        payload["assetPath"] = asset_path
        payload["graphName"] = "PCGGraph"
        mutate_result = call_tool(client, request_id_base + 10, "graph.mutate", payload)
        surface_matrix["mutate"] = "pass"
        ref_map = build_client_ref_map(payload, mutate_result)
        snapshot = query_pcg_snapshot(client, request_id_base + 20, asset_path)
        structure_details = assert_workflow_structure(case, snapshot, ref_map)
        surface_matrix["queryStructure"] = "pass"
        verify_details = verify_graph(client, request_id_base + 30, asset_path)
        surface_matrix["verify"] = "pass"
        surface_matrix["diagnostics"] = "pass"
        query_truth_checks = audit_workflow_query_truth(case, snapshot, ref_map)
        surface_matrix["queryTruth"] = "pass" if query_truth_checks else "not_run"
        result["status"] = "pass"
        result["details"] = {
            "nodeCount": len(snapshot.get("nodes", [])) if isinstance(snapshot.get("nodes"), list) else None,
            "edgeCount": len(snapshot.get("edges", [])) if isinstance(snapshot.get("edges"), list) else None,
            "structure": {
                **structure_details,
                "queryTruthChecks": query_truth_checks,
            },
            "verify": verify_details,
            "surfaceMatrix": surface_matrix,
        }
        return result
    except WorkflowSuiteError as exc:
        if exc.kind == "query_truth_unsurfaced" or exc.kind == "query_truth_mismatch":
            surface_matrix["queryStructure"] = "pass"
            surface_matrix["queryTruth"] = "fail"
        elif exc.kind == "verify_gap":
            surface_matrix["queryStructure"] = "pass"
            surface_matrix["verify"] = "fail"
            surface_matrix["diagnostics"] = "fail"
        elif exc.kind == "structure_gap":
            surface_matrix["queryStructure"] = "fail"
        elif exc.kind == "mutate_result_gap":
            surface_matrix["mutate"] = "fail"
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        result["details"] = {"surfaceMatrix": surface_matrix}
        return result
    except Exception as exc:
        result["details"] = {"surfaceMatrix": surface_matrix}
        result["failureKind"] = "runner_error"
        result["reason"] = str(exc)
        return result
    finally:
        try:
            cleanup_pcg_fixture(client, request_id_base + 900, asset_path=asset_path, actor_path=actor_path)
        except BaseException:
            pass


def execute_case_with_fresh_client(
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=timeout_s)
    transcript = io.StringIO()
    try:
        with contextlib.redirect_stdout(transcript):
            _ = client.request(1, "initialize", {})
            wait_for_bridge_ready(client)
            result = run_workflow_case(
                client,
                request_id_base=request_id_base,
                case_index=case_index,
                case=case,
            )
    except Exception as exc:
        result = {
            "caseId": case["id"],
            "example": case.get("example"),
            "fixture": case["fixture"],
            "families": case.get("families", []),
            "status": "fail",
            "failureKind": "runner_error",
            "reason": str(exc),
        }
    finally:
        client.close()

    log_text = transcript.getvalue().strip()
    if log_text:
        result["logs"] = log_text.splitlines()[-10:]
    return result


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    status_counter = Counter(result["status"] for result in results)
    kind_counter = Counter(
        result["failureKind"]
        for result in results
        if result.get("status") == "fail" and isinstance(result.get("failureKind"), str)
    )
    reason_counter = Counter(
        result["reason"] for result in results if result.get("status") == "fail" and isinstance(result.get("reason"), str)
    )
    surface_totals: dict[str, Counter[str]] = {
        surface: Counter()
        for surface in ("mutate", "queryStructure", "queryTruth", "engineTruth", "verify", "diagnostics")
    }
    family_rows: dict[str, dict[str, Any]] = {}
    for result in results:
        details = result.get("details")
        if isinstance(details, dict):
            surface_matrix = details.get("surfaceMatrix")
            if isinstance(surface_matrix, dict):
                for surface, counter in surface_totals.items():
                    value = surface_matrix.get(surface)
                    if isinstance(value, str) and value:
                        counter[value] += 1
        for family in result.get("families", []):
            if not isinstance(family, str) or not family:
                continue
            row = family_rows.setdefault(
                family,
                {"family": family, "totalCases": 0, "passed": 0, "failed": 0, "failureKinds": Counter()},
            )
            row["totalCases"] += 1
            if result.get("status") == "pass":
                row["passed"] += 1
            elif result.get("status") == "fail":
                row["failed"] += 1
                if isinstance(result.get("failureKind"), str):
                    row["failureKinds"][result["failureKind"]] += 1

    return {
        "totalCases": len(results),
        "passed": status_counter.get("pass", 0),
        "failed": status_counter.get("fail", 0),
        "failureKinds": dict(sorted(kind_counter.items())),
        "failureReasons": dict(sorted(reason_counter.items())),
        "surfaceMatrix": {
            surface: dict(sorted(surface_counter.items()))
            for surface, surface_counter in surface_totals.items()
            if surface_counter
        },
        "familySummary": [
            {
                "family": row["family"],
                "totalCases": row["totalCases"],
                "passed": row["passed"],
                "failed": row["failed"],
                "failureKinds": dict(sorted(row["failureKinds"].items())),
            }
            for row in sorted(family_rows.values(), key=lambda item: item["family"])
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PCG workflow truth regressions against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the project-local loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--list-cases", action="store_true", help="Print the workflow case registry and exit")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)

    cases = WORKFLOW_CASES[: args.max_cases] if args.max_cases > 0 else WORKFLOW_CASES
    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=80000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "workflow_truth",
        "summary": build_summary(results),
        "results": results,
    }
    text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        output_path = Path(args.output).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
