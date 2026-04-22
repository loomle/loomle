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
    is_tool_error_payload,
    parse_tool_payload,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tests" / "tools"))
from run_pcg_graph_test_plan import (  # noqa: E402
    blank_surface_matrix,
    cleanup_pcg_fixture,
    compact_json,
    create_pcg_fixture,
    wait_for_bridge_ready,
)


class NegativeSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str, *, details: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.kind = kind
        self.details = details or {}


NEGATIVE_CASES = [
    {
        "id": "set_pin_default_requires_target_apply",
        "fixture": "pcg_graph",
        "operation": "setPinDefault",
        "families": ["meta"],
        "summary": "setPinDefault should reject legacy args without target during normal apply",
    },
    {
        "id": "set_pin_default_requires_target_dry_run",
        "fixture": "pcg_graph",
        "operation": "setPinDefault",
        "families": ["meta"],
        "summary": "setPinDefault dryRun should enforce the same target contract as normal apply",
    },
    {
        "id": "set_pin_default_bad_pin_diagnostics",
        "fixture": "pcg_graph",
        "operation": "setPinDefault",
        "families": ["meta"],
        "summary": "bad PCG pin targets should return candidate pin diagnostics",
    },
    {
        "id": "remove_node_requires_stable_target",
        "fixture": "pcg_graph",
        "operation": "removeNode",
        "families": ["struct"],
        "summary": "removeNode should reject non-stable name targets on PCG graphs",
    },
    {
        "id": "set_pin_default_bad_nested_filter_path",
        "fixture": "pcg_graph",
        "operation": "setPinDefault",
        "families": ["filter"],
        "summary": "nested filter threshold targets should reject missing property-path segments",
    },
    {
        "id": "set_pin_default_missing_subgraph_asset",
        "fixture": "pcg_graph",
        "operation": "setPinDefault",
        "families": ["struct"],
        "summary": "SubgraphOverride should reject clearly missing subgraph asset references",
    },
    {
        "id": "connect_pins_bad_output_pin",
        "fixture": "pcg_graph",
        "operation": "connectPins",
        "families": ["branch", "create"],
        "summary": "connectPins should reject missing PCG output pins on structured branch graphs",
    },
    {
        "id": "disconnect_pins_bad_output_pin",
        "fixture": "pcg_graph",
        "operation": "disconnectPins",
        "families": ["branch", "create"],
        "summary": "disconnectPins should reject missing PCG output pins on structured branch graphs",
    },
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "pcg",
        "suite": "negative_boundary",
        "summary": {
            "totalCases": len(NEGATIVE_CASES),
            "operations": sorted(
                {
                    case["operation"]
                    for case in NEGATIVE_CASES
                    if isinstance(case.get("operation"), str) and case["operation"]
                }
            ),
        },
        "cases": [
            {
                "id": case["id"],
                "fixture": case["fixture"],
                "operation": case["operation"],
                "families": case.get("families", []),
                "summary": case["summary"],
            }
            for case in NEGATIVE_CASES
        ],
    }


def extract_error_op_results(payload: dict[str, Any]) -> list[dict[str, Any]]:
    op_results = payload.get("opResults")
    if isinstance(op_results, list):
        return [result for result in op_results if isinstance(result, dict)]

    detail = payload.get("detail")
    if isinstance(detail, str) and detail.strip():
        try:
            parsed = json.loads(detail)
        except Exception:
            return []
        parsed_results = parsed.get("opResults") if isinstance(parsed, dict) else None
        if isinstance(parsed_results, list):
            return [result for result in parsed_results if isinstance(result, dict)]
    return []


def call_tool_allow_error(client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]) -> tuple[dict[str, Any], bool]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    payload = parse_tool_payload(response, f"tools/call.{name}")
    return payload, is_tool_error_payload(payload)


def add_node_by_class(client: McpStdioClient, request_id: int, *, asset_path: str, node_class_path: str) -> str:
    payload = call_tool(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [{"op": "addNode.byClass", "args": {"nodeClassPath": node_class_path}}],
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        raise NegativeSuiteError("runner_error", f"missing addNode opResults: {compact_json(payload)}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    node_id = first.get("nodeId")
    if not first.get("ok") or not isinstance(node_id, str) or not node_id:
        raise NegativeSuiteError("runner_error", f"addNode failed: {compact_json(payload)}")
    return node_id


def expect_error_contains(payload: dict[str, Any], needle: str, *, kind: str) -> None:
    haystack = " ".join(
        [
            str(payload.get("message", "")),
            str(payload.get("detail", "")),
        ]
    )
    if needle not in haystack:
        raise NegativeSuiteError(kind, f"expected error to contain {needle!r}: {compact_json(payload)}")


def run_set_pin_default_requires_target_apply(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_node_by_class(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGCreatePointsSphereSettings",
    )
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "setPinDefault",
                    "args": {
                        "nodeId": node_id,
                        "pinName": "Radius",
                        "value": 250.5,
                    },
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    expect_error_contains(payload, "setPinDefault requires args.target.", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_set_pin_default_requires_target_dry_run(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_node_by_class(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGCreatePointsSphereSettings",
    )
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "dryRun": True,
            "ops": [
                {
                    "op": "setPinDefault",
                    "args": {
                        "nodeId": node_id,
                        "pinName": "Radius",
                        "value": 250.5,
                    },
                }
            ],
        },
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise NegativeSuiteError(
            "contract_surface_gap",
            f"dryRun accepted legacy setPinDefault args: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    expect_error_contains(payload, "setPinDefault requires args.target.", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_set_pin_default_bad_pin_diagnostics(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_node_by_class(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGCreatePointsSphereSettings",
    )
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeId": node_id, "pin": "DefinitelyMissingPin"},
                        "value": True,
                    },
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    op_results = extract_error_op_results(payload)
    if not op_results:
        raise NegativeSuiteError(
            "diagnostic_surface_gap",
            f"missing error opResults: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    first = op_results[0]
    details = first.get("details")
    if not isinstance(details, dict):
        surface_matrix["diagnostics"] = "fail"
        raise NegativeSuiteError(
            "diagnostic_surface_gap",
            f"missing details object: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    expected_target_forms = details.get("expectedTargetForms")
    candidate_pins = details.get("candidatePins")
    if not isinstance(expected_target_forms, list) or not expected_target_forms:
        surface_matrix["diagnostics"] = "fail"
        raise NegativeSuiteError(
            "diagnostic_surface_gap",
            f"missing expectedTargetForms: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    if not isinstance(candidate_pins, list) or not candidate_pins:
        surface_matrix["diagnostics"] = "fail"
        raise NegativeSuiteError(
            "diagnostic_surface_gap",
            f"missing candidatePins: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    if not any(isinstance(pin, dict) and pin.get("pinName") == "Radius" for pin in candidate_pins):
        surface_matrix["diagnostics"] = "fail"
        raise NegativeSuiteError(
            "diagnostic_surface_gap",
            f"candidatePins missing Radius: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    surface_matrix["diagnostics"] = "pass"
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
        "details": {
            "errorCode": first.get("errorCode"),
            "candidatePinCount": len(candidate_pins),
            "expectedTargetForms": expected_target_forms,
        },
    }


def run_remove_node_requires_stable_target(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    _ = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
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
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [{"op": "removeNode", "args": {"target": {"name": "Add Tag"}}}],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    expect_error_contains(payload, "stable target", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_set_pin_default_bad_nested_filter_path(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_node_by_class(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGAttributeFilteringRangeSettings",
    )
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeId": node_id, "pin": "MinThreshold/DefinitelyMissing"},
                        "value": True,
                    },
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    expect_error_contains(payload, "property path segment 'DefinitelyMissing' was not found", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_set_pin_default_missing_subgraph_asset(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "subgraph_node",
                    "args": {"nodeClassPath": "/Script/PCG.PCGSubgraphSettings"},
                },
                {
                    "op": "setPinDefault",
                    "args": {
                        "target": {"nodeRef": "subgraph_node", "pin": "SubgraphOverride"},
                        "value": "/Game/Definitely/Missing",
                    },
                },
            ],
        },
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise NegativeSuiteError(
            "contract_surface_gap",
            f"missing subgraph asset was accepted: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    expect_error_contains(payload, "Missing", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_connect_pins_bad_output_pin(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    setup = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {"op": "addNode.byClass", "clientRef": "create_points", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"}},
                {"op": "addNode.byClass", "clientRef": "branch_node", "args": {"nodeClassPath": "/Script/PCG.PCGBranchSettings"}},
            ],
        },
    )
    op_results = setup.get("opResults")
    if not isinstance(op_results, list) or len(op_results) < 2:
        raise NegativeSuiteError("runner_error", f"missing setup opResults: {compact_json(setup)}")
    create_id = op_results[0].get("nodeId") if isinstance(op_results[0], dict) else None
    branch_id = op_results[1].get("nodeId") if isinstance(op_results[1], dict) else None
    if not isinstance(create_id, str) or not isinstance(branch_id, str):
        raise NegativeSuiteError("runner_error", f"missing setup node ids: {compact_json(setup)}")
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeId": create_id, "pin": "DefinitelyMissingOutput"},
                        "to": {"nodeId": branch_id, "pin": "In"},
                    },
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    expect_error_contains(payload, "PCG output pin not found.", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_disconnect_pins_bad_output_pin(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    setup = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {"op": "addNode.byClass", "clientRef": "create_points", "args": {"nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"}},
                {"op": "addNode.byClass", "clientRef": "branch_node", "args": {"nodeClassPath": "/Script/PCG.PCGBranchSettings"}},
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "create_points", "pin": "Out"},
                        "to": {"nodeRef": "branch_node", "pin": "In"},
                    },
                },
            ],
        },
    )
    op_results = setup.get("opResults")
    if not isinstance(op_results, list) or len(op_results) < 2:
        raise NegativeSuiteError("runner_error", f"missing setup opResults: {compact_json(setup)}")
    create_id = op_results[0].get("nodeId") if isinstance(op_results[0], dict) else None
    branch_id = op_results[1].get("nodeId") if isinstance(op_results[1], dict) else None
    if not isinstance(create_id, str) or not isinstance(branch_id, str):
        raise NegativeSuiteError("runner_error", f"missing setup node ids: {compact_json(setup)}")
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "PCGGraph",
            "graphType": "pcg",
            "ops": [
                {
                    "op": "disconnectPins",
                    "args": {
                        "from": {"nodeId": create_id, "pin": "DefinitelyMissingOutput"},
                        "to": {"nodeId": branch_id, "pin": "In"},
                    },
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    expect_error_contains(payload, "PCG output pin not found.", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_negative_case(client: McpStdioClient, request_id_base: int, case: dict[str, Any], case_index: int) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "fixture": case["fixture"],
        "operation": case["operation"],
        "families": case.get("families", []),
        "status": "fail",
    }
    asset_path = f"/Game/Codex/PCGNegativeBoundary/{case['id']}_{case_index}"
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

        if case["id"] == "set_pin_default_requires_target_apply":
            details = run_set_pin_default_requires_target_apply(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_requires_target_dry_run":
            details = run_set_pin_default_requires_target_dry_run(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_bad_pin_diagnostics":
            details = run_set_pin_default_bad_pin_diagnostics(client, request_id_base + 100, asset_path)
        elif case["id"] == "remove_node_requires_stable_target":
            details = run_remove_node_requires_stable_target(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_bad_nested_filter_path":
            details = run_set_pin_default_bad_nested_filter_path(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_missing_subgraph_asset":
            details = run_set_pin_default_missing_subgraph_asset(client, request_id_base + 100, asset_path)
        elif case["id"] == "connect_pins_bad_output_pin":
            details = run_connect_pins_bad_output_pin(client, request_id_base + 100, asset_path)
        elif case["id"] == "disconnect_pins_bad_output_pin":
            details = run_disconnect_pins_bad_output_pin(client, request_id_base + 100, asset_path)
        else:
            raise NegativeSuiteError("runner_error", f"unsupported case id: {case['id']}")

        result["status"] = "pass"
        result["details"] = details
        return result
    except SystemExit as exc:
        result["failureKind"] = "runner_system_exit"
        result["reason"] = f"system_exit:{exc.code}"
        return result
    except NegativeSuiteError as exc:
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        if exc.details:
            result["details"] = exc.details
        return result
    except Exception as exc:
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
            result = run_negative_case(client, request_id_base, case, case_index)
    except SystemExit as exc:
        result = {
            "caseId": case["id"],
            "fixture": case["fixture"],
            "operation": case["operation"],
            "families": case.get("families", []),
            "status": "fail",
            "failureKind": "runner_system_exit",
            "reason": f"system_exit:{exc.code}",
        }
    except Exception as exc:
        result = {
            "caseId": case["id"],
            "fixture": case["fixture"],
            "operation": case["operation"],
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
    return {
        "totalCases": len(results),
        "passed": status_counter.get("pass", 0),
        "failed": status_counter.get("fail", 0),
        "failureKinds": dict(sorted(kind_counter.items())),
        "failureReasons": dict(sorted(reason_counter.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PCG negative and boundary contract tests against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--list-cases", action="store_true", help="Print the negative case registry and exit")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)

    cases = NEGATIVE_CASES[: args.max_cases] if args.max_cases > 0 else NEGATIVE_CASES
    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=120000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "negative_boundary",
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
