#!/usr/bin/env python3
import argparse
import contextlib
import io
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tools"))
from run_blueprint_workflow_truth_suite import (  # noqa: E402
    cleanup_blueprint_fixture,
    create_blueprint_fixture,
    query_blueprint_snapshot,
    safe_call_tool,
)
from run_pcg_graph_test_plan import blank_surface_matrix, compact_json, wait_for_bridge_ready  # noqa: E402
from run_pcg_selector_truth_suite import _assert_expected_subset  # noqa: E402


class BlueprintEmbeddedTemplateSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


EMBEDDED_TEMPLATE_CASES = [
    {
        "id": "timeline_embedded_template_surface",
        "className": "UK2Node_Timeline",
        "displayName": "Timeline",
        "families": ["utility"],
        "profile": "context_recipe_required",
        "recipe": "blueprint_timeline_graph",
        "fixture": "blueprint_timeline_graph",
        "querySurfaceKind": "embedded_template",
        "nodeClassPath": "/Script/BlueprintGraph.K2Node_Timeline",
        "summary": "Timeline should surface Blueprint-owned timeline template truth through embeddedTemplate/effectiveSettings.",
        "setupArgs": {},
        "expectedQuery": {
            "surfaceKind": "embedded_template",
            "templateKind": "timeline",
            "timelineName": "Timeline",
            "variableName": "Timeline",
            "length": 5,
            "lengthMode": "TL_TimelineLength",
            "autoPlay": False,
            "loop": False,
            "replicated": False,
            "ignoreTimeDilation": False,
            "trackSummary": {
                "floatTrackCount": 0,
                "vectorTrackCount": 0,
                "linearColorTrackCount": 0,
                "eventTrackCount": 0,
                "tracks": [],
            },
        },
        "requiredStringFields": ["templateName", "templatePath", "updateFunctionName", "finishedFunctionName", "timelineGuid"],
    },
    {
        "id": "add_component_embedded_template_surface",
        "className": "UK2Node_AddComponent",
        "displayName": "Add Component",
        "families": ["utility"],
        "profile": "context_recipe_required",
        "recipe": "blueprint_component_template_context",
        "fixture": "blueprint_component_template_context",
        "querySurfaceKind": "embedded_template",
        "nodeClassPath": "/Script/BlueprintGraph.K2Node_AddComponent",
        "summary": "AddComponent should surface Blueprint-owned component template truth through embeddedTemplate/effectiveSettings.",
        "setupArgs": {
            "componentClassPath": "/Script/Engine.StaticMeshComponent",
            "componentName": "ProbeMesh",
        },
        "expectedQuery": {
            "surfaceKind": "embedded_template",
            "templateKind": "component",
            "templateResolved": True,
            "componentClassPath": "/Script/Engine.StaticMeshComponent",
            "templateTypeClassPath": "/Script/Engine.StaticMeshComponent",
            "attachPolicy": "auto_root",
            "manualAttachmentDefault": "false",
            "relativeTransformSummary": {
                "location": {"x": 0, "y": 0, "z": 0},
                "rotation": {"pitch": 0, "yaw": 0, "roll": -0.0},
                "scale": {"x": 1, "y": 1, "z": 1},
            },
            "templatePropertySummary": {
                "overrideCount": 0,
                "overrides": [],
                "truncated": False,
            },
        },
        "requiredStringFields": ["templateName", "templateObjectPath"],
    },
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "blueprint",
        "suite": "embedded_template",
        "summary": {
            "totalCases": len(EMBEDDED_TEMPLATE_CASES),
            "presenceShapeCases": len(EMBEDDED_TEMPLATE_CASES),
            "families": sorted(
                {
                    family
                    for case in EMBEDDED_TEMPLATE_CASES
                    for family in case.get("families", [])
                    if isinstance(family, str) and family
                }
            ),
            "querySurfaceKinds": sorted(
                {
                    case["querySurfaceKind"]
                    for case in EMBEDDED_TEMPLATE_CASES
                    if isinstance(case.get("querySurfaceKind"), str) and case["querySurfaceKind"]
                }
            ),
            "recipes": sorted(
                {
                    case["recipe"]
                    for case in EMBEDDED_TEMPLATE_CASES
                    if isinstance(case.get("recipe"), str) and case["recipe"]
                }
            ),
        },
        "cases": [
            {
                "id": case["id"],
                "className": case["className"],
                "displayName": case["displayName"],
                "families": case.get("families", []),
                "profile": case["profile"],
                "recipe": case["recipe"],
                "fixture": case["fixture"],
                "querySurfaceKind": case["querySurfaceKind"],
                "requiredStringFields": case["requiredStringFields"],
                "summary": case["summary"],
            }
            for case in EMBEDDED_TEMPLATE_CASES
        ],
    }


def _find_node(snapshot: dict[str, Any], node_class_path: str) -> dict[str, Any]:
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        raise BlueprintEmbeddedTemplateSuiteError("query_gap", "blueprint graph.query missing nodes[]")
    for node in nodes:
        if isinstance(node, dict) and node.get("nodeClassPath") == node_class_path:
            return node
    raise BlueprintEmbeddedTemplateSuiteError(
        "query_gap",
        f"blueprint graph.query missing node {node_class_path}: {compact_json(snapshot)}",
    )


def _assert_required_string_fields(surface: dict[str, Any], fields: list[str]) -> None:
    for field_name in fields:
        value = surface.get(field_name)
        if not isinstance(value, str) or not value:
            raise BlueprintEmbeddedTemplateSuiteError(
                "embedded_template_shape_gap",
                f"embedded-template field {field_name} missing or empty: {compact_json(surface)}",
            )


def _assert_surface(case: dict[str, Any], surface: dict[str, Any], *, label: str) -> None:
    _assert_expected_subset(surface, case["expectedQuery"], kind="embedded_template_shape_gap", prefix=label)
    _assert_required_string_fields(surface, case["requiredStringFields"])


def _run_case(client: McpStdioClient, *, request_id_base: int, case: dict[str, Any]) -> dict[str, Any]:
    asset_path = f"/Game/LoomleTests/Blueprint/{case['id']}"
    surface_matrix = blank_surface_matrix()
    try:
        create_blueprint_fixture(client, request_id_base, asset_path=asset_path)
        payload = safe_call_tool(
            client,
            request_id_base + 10,
            "graph.mutate",
            {
                "assetPath": asset_path,
                "graphName": "EventGraph",
                "graphType": "blueprint",
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "embedded_template_node",
                        "args": {
                            "nodeClassPath": case["nodeClassPath"],
                            "position": {"x": 320, "y": 0},
                            **case.get("setupArgs", {}),
                        },
                    }
                ],
            },
        )
        op_results = payload.get("opResults")
        if not isinstance(op_results, list) or not op_results or not isinstance(op_results[0], dict) or op_results[0].get("ok") is not True:
            raise BlueprintEmbeddedTemplateSuiteError(
                "mutate_gap",
                f"embedded-template addNode opResults missing success: {compact_json(payload)}",
            )
        surface_matrix["mutate"] = "pass"

        snapshot = query_blueprint_snapshot(client, request_id_base + 20, asset_path)
        node = _find_node(snapshot, case["nodeClassPath"])
        surface_matrix["queryStructure"] = "pass"

        embedded_template = node.get("embeddedTemplate")
        if not isinstance(embedded_template, dict):
            raise BlueprintEmbeddedTemplateSuiteError(
                "embedded_template_unsurfaced",
                f"graph.query missing embeddedTemplate surface: {compact_json(node)}",
            )
        effective_settings = node.get("effectiveSettings")
        if not isinstance(effective_settings, dict):
            raise BlueprintEmbeddedTemplateSuiteError(
                "embedded_template_unsurfaced",
                f"graph.query missing effectiveSettings mirror for embedded template node: {compact_json(node)}",
            )

        _assert_surface(case, embedded_template, label="embeddedTemplate")
        _assert_surface(case, effective_settings, label="effectiveSettings")
        if embedded_template != effective_settings:
            raise BlueprintEmbeddedTemplateSuiteError(
                "embedded_template_surface_mismatch",
                f"embeddedTemplate and effectiveSettings diverged: embeddedTemplate={compact_json(embedded_template)} effectiveSettings={compact_json(effective_settings)}",
            )
        surface_matrix["queryTruth"] = "pass"

        return {
            "status": "pass",
            "surfaceMatrix": surface_matrix,
            "details": {
                "querySurfaceKind": case["querySurfaceKind"],
                "embeddedTemplate": embedded_template,
                "effectiveSettings": effective_settings,
            },
        }
    finally:
        cleanup_blueprint_fixture(client, request_id_base + 900, asset_path=asset_path)


def execute_suite(*, project_root: Path, loomle_binary: Path, timeout_s: float) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=timeout_s)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            wait_for_bridge_ready(client)
        for index, case in enumerate(EMBEDDED_TEMPLATE_CASES):
            try:
                case_result = _run_case(client, request_id_base=410000 + (index * 1000), case=case)
            except BlueprintEmbeddedTemplateSuiteError as exc:
                case_result = {
                    "status": "fail",
                    "failureKind": exc.kind,
                    "reason": str(exc),
                    "surfaceMatrix": blank_surface_matrix(),
                }
            results.append(
                {
                    "id": case["id"],
                    "className": case["className"],
                    "displayName": case["displayName"],
                    "families": case.get("families", []),
                    "recipe": case["recipe"],
                    "fixture": case["fixture"],
                    "querySurfaceKind": case["querySurfaceKind"],
                    **case_result,
                }
            )
    finally:
        client.close()

    passing = sum(1 for result in results if result["status"] == "pass")
    failing = len(results) - passing
    return {
        "version": "1",
        "graphType": "blueprint",
        "suite": "embedded_template",
        "summary": {
            "totalCases": len(EMBEDDED_TEMPLATE_CASES),
            "passingCases": passing,
            "failingCases": failing,
            "presenceShapeCases": len(EMBEDDED_TEMPLATE_CASES),
            "families": sorted(
                {
                    family
                    for case in EMBEDDED_TEMPLATE_CASES
                    for family in case.get("families", [])
                    if isinstance(family, str) and family
                }
            ),
            "querySurfaceKinds": sorted(
                {
                    case["querySurfaceKind"]
                    for case in EMBEDDED_TEMPLATE_CASES
                    if isinstance(case.get("querySurfaceKind"), str) and case["querySurfaceKind"]
                }
            ),
            "recipes": sorted(
                {
                    case["recipe"]
                    for case in EMBEDDED_TEMPLATE_CASES
                    if isinstance(case.get("recipe"), str) and case["recipe"]
                }
            ),
        },
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Blueprint embedded-template query surfaces.")
    parser.add_argument("--project-root")
    parser.add_argument("--loomle-binary")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--output")
    parser.add_argument("--list-cases", action="store_true")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, None)
    loomle_binary = Path(args.loomle_binary) if args.loomle_binary else resolve_default_loomle_binary(project_root)
    report = execute_suite(project_root=project_root, loomle_binary=loomle_binary, timeout_s=args.timeout)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["summary"]["failingCases"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
