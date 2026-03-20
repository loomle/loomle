#!/usr/bin/env python3
import argparse
import contextlib
import io
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    is_tool_error_payload,
    parse_tool_payload,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tools"))
from run_blueprint_workflow_truth_suite import cleanup_blueprint_fixture, create_blueprint_fixture  # noqa: E402
from run_pcg_graph_test_plan import blank_surface_matrix, compact_json, wait_for_bridge_ready  # noqa: E402


class BlueprintNegativeSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str, *, details: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.kind = kind
        self.details = details or {}


NEGATIVE_CASES = [
    {
        "id": "stale_expected_revision_conflict",
        "fixture": "blueprint_event_graph",
        "operation": "addNode.byClass",
        "families": ["branch", "utility"],
        "summary": "stale expectedRevision should reject a second Blueprint mutate after revision advancement",
    },
    {
        "id": "duplicate_client_ref_rejected",
        "fixture": "blueprint_event_graph",
        "operation": "addNode.byClass",
        "families": ["branch", "utility"],
        "summary": "duplicate clientRef values should be rejected on Blueprint mutates",
    },
    {
        "id": "set_pin_default_bad_target_diagnostics",
        "fixture": "blueprint_event_graph",
        "operation": "setPinDefault",
        "families": ["branch", "variable"],
        "summary": "bad Blueprint setPinDefault targets should surface candidate pin diagnostics",
    },
    {
        "id": "partial_apply_unsupported_op",
        "fixture": "blueprint_event_graph",
        "operation": "batch_partial_apply",
        "families": ["branch", "struct", "utility"],
        "summary": "mixed Blueprint batches should report partialApplied when an earlier op commits before an unsupported op",
    },
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "blueprint",
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


def call_tool_allow_error(
    client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]
) -> tuple[dict[str, Any], bool]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    payload = parse_tool_payload(response, f"tools/call.{name}")
    return payload, is_tool_error_payload(payload)


def call_tool_ok(client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    payload, has_error = call_tool_allow_error(client, req_id, name, arguments)
    if has_error:
        raise BlueprintNegativeSuiteError("tool_error", f"{name} failed payload={compact_json(payload)}", details={"errorPayload": payload})
    return payload


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


def query_blueprint_revision_and_node_count(client: McpStdioClient, request_id: int, asset_path: str) -> tuple[str, int]:
    payload = call_tool_ok(
        client,
        request_id,
        "graph.query",
        {"assetPath": asset_path, "graphName": "EventGraph", "graphType": "blueprint", "limit": 200},
    )
    revision = payload.get("revision")
    snapshot = payload.get("semanticSnapshot")
    nodes = snapshot.get("nodes") if isinstance(snapshot, dict) else None
    if not isinstance(revision, str) or not revision:
        raise BlueprintNegativeSuiteError("query_gap", f"blueprint query missing revision: {compact_json(payload)}")
    if not isinstance(nodes, list):
        raise BlueprintNegativeSuiteError("query_gap", f"blueprint query missing nodes[]: {compact_json(payload)}")
    return revision, len(nodes)


def add_branch_node(client: McpStdioClient, request_id: int, *, asset_path: str, client_ref: str = "branch_node") -> str:
    payload = call_tool_ok(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "EventGraph",
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": client_ref,
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 640, "y": 0},
                    },
                }
            ],
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results or not isinstance(op_results[0], dict):
        raise BlueprintNegativeSuiteError("runner_error", f"blueprint addNode missing opResults: {compact_json(payload)}")
    node_id = op_results[0].get("nodeId")
    if not isinstance(node_id, str) or not node_id:
        raise BlueprintNegativeSuiteError("runner_error", f"blueprint addNode missing nodeId: {compact_json(payload)}")
    return node_id


def run_stale_expected_revision_conflict(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    revision_r0, node_count_r0 = query_blueprint_revision_and_node_count(client, request_id_base + 1, asset_path)
    apply_payload = call_tool_ok(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "EventGraph",
            "graphType": "blueprint",
            "expectedRevision": revision_r0,
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
    op_results = apply_payload.get("opResults")
    if not isinstance(op_results, list) or not op_results or not isinstance(op_results[0], dict) or not op_results[0].get("ok"):
        raise BlueprintNegativeSuiteError("runner_error", f"control mutate failed: {compact_json(apply_payload)}")
    revision_r1, node_count_r1 = query_blueprint_revision_and_node_count(client, request_id_base + 3, asset_path)
    if revision_r1 == revision_r0 or node_count_r1 != node_count_r0 + 1:
        raise BlueprintNegativeSuiteError(
            "runner_error",
            f"control mutate did not advance blueprint graph state: before={(revision_r0, node_count_r0)} after={(revision_r1, node_count_r1)}",
        )

    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 4,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "EventGraph",
            "graphType": "blueprint",
            "expectedRevision": revision_r0,
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
    surface_matrix["mutate"] = "pass"
    if not has_error or payload.get("domainCode") != "REVISION_CONFLICT":
        raise BlueprintNegativeSuiteError(
            "contract_surface_gap",
            f"expected REVISION_CONFLICT for stale expectedRevision: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    revision_after, node_count_after = query_blueprint_revision_and_node_count(client, request_id_base + 5, asset_path)
    if revision_after != revision_r1 or node_count_after != node_count_r1:
        surface_matrix["queryStructure"] = "fail"
        raise BlueprintNegativeSuiteError(
            "query_surface_gap",
            f"stale expectedRevision changed graph state: before={(revision_r1, node_count_r1)} after={(revision_after, node_count_after)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    surface_matrix["queryStructure"] = "pass"
    return {"surfaceMatrix": surface_matrix, "errorPayload": payload}


def run_duplicate_client_ref_rejected(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
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
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise BlueprintNegativeSuiteError(
            "contract_surface_gap",
            f"duplicate clientRef was accepted: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    op_results = extract_error_op_results(payload)
    if len(op_results) < 2:
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef missing opResults: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    second = op_results[1]
    if second.get("errorCode") != "INVALID_ARGUMENT":
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef wrong errorCode: {compact_json(second)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": second},
        )
    if "Duplicate clientRef" not in str(second.get("errorMessage", "")):
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef wrong errorMessage: {compact_json(second)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": second},
        )
    surface_matrix["diagnostics"] = "pass"
    return {"surfaceMatrix": surface_matrix, "errorPayload": payload}


def run_set_pin_default_bad_target_diagnostics(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_branch_node(client, request_id_base + 1, asset_path=asset_path)
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "EventGraph",
            "graphType": "blueprint",
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
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise BlueprintNegativeSuiteError(
            "contract_surface_gap",
            f"bad setPinDefault target was accepted: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    op_results = extract_error_op_results(payload)
    if not op_results:
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault missing opResults: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    first = op_results[0]
    if first.get("errorCode") not in {"TARGET_NOT_FOUND", "INVALID_ARGUMENT"}:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault wrong errorCode: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    details = first.get("details")
    if not isinstance(details, dict):
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault missing details object: {compact_json(first)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    expected_target_forms = details.get("expectedTargetForms")
    candidate_pins = details.get("candidatePins")
    if not isinstance(expected_target_forms, list) or not expected_target_forms:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault missing expectedTargetForms: {compact_json(details)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    if not isinstance(candidate_pins, list) or not candidate_pins:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault missing candidatePins: {compact_json(details)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    if not any(isinstance(pin, dict) and pin.get("pinName") == "Condition" for pin in candidate_pins):
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"bad setPinDefault candidatePins missing Condition: {compact_json(candidate_pins)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": first},
        )
    surface_matrix["diagnostics"] = "pass"
    return {"surfaceMatrix": surface_matrix, "errorPayload": payload}


def run_partial_apply_unsupported_op(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_branch_node(client, request_id_base + 1, asset_path=asset_path, client_ref="partial_apply_node")
    revision_before, node_count_before = query_blueprint_revision_and_node_count(client, request_id_base + 2, asset_path)
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 3,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "EventGraph",
            "graphType": "blueprint",
            "ops": [
                {"op": "removeNode", "args": {"target": {"nodeId": node_id}}},
                {"op": "addNode", "args": {"nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse"}},
            ],
        },
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise BlueprintNegativeSuiteError(
            "contract_surface_gap",
            f"partial-apply mixed batch unexpectedly succeeded: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    detail = payload.get("detail")
    try:
        parsed_detail = json.loads(detail) if isinstance(detail, str) and detail.strip() else None
    except Exception:
        parsed_detail = None
    if not isinstance(parsed_detail, dict):
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply missing structured detail: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    if parsed_detail.get("code") != "UNSUPPORTED_OP":
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply structured detail missing UNSUPPORTED_OP: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    if parsed_detail.get("applied") is not False or parsed_detail.get("partialApplied") is not True:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply metadata mismatch: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    op_results = parsed_detail.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != 2:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply missing opResults: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    second = op_results[1] if isinstance(op_results[1], dict) else {}
    if first.get("ok") is not True or first.get("changed") is not True:
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply first op metadata mismatch: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    if second.get("errorCode") != "UNSUPPORTED_OP":
        surface_matrix["diagnostics"] = "fail"
        raise BlueprintNegativeSuiteError(
            "diagnostic_surface_gap",
            f"partial-apply second op errorCode mismatch: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    revision_after, node_count_after = query_blueprint_revision_and_node_count(client, request_id_base + 4, asset_path)
    if revision_after == revision_before or node_count_after != node_count_before - 1:
        surface_matrix["queryStructure"] = "fail"
        raise BlueprintNegativeSuiteError(
            "query_surface_gap",
            f"partial-apply batch did not commit earlier removal: before={(revision_before, node_count_before)} after={(revision_after, node_count_after)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail},
        )
    surface_matrix["queryStructure"] = "pass"
    surface_matrix["diagnostics"] = "pass"
    return {"surfaceMatrix": surface_matrix, "errorPayload": payload, "detail": parsed_detail}


def run_negative_case(client: McpStdioClient, request_id_base: int, case: dict[str, Any], case_index: int) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "fixture": case["fixture"],
        "operation": case["operation"],
        "families": case.get("families", []),
        "status": "fail",
    }
    asset_path = f"/Game/Codex/BlueprintNegativeBoundary/{case['id']}_{case_index}"
    try:
        _ = create_blueprint_fixture(client, request_id_base, asset_path=asset_path)

        if case["id"] == "stale_expected_revision_conflict":
            details = run_stale_expected_revision_conflict(client, request_id_base + 100, asset_path)
        elif case["id"] == "duplicate_client_ref_rejected":
            details = run_duplicate_client_ref_rejected(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_bad_target_diagnostics":
            details = run_set_pin_default_bad_target_diagnostics(client, request_id_base + 100, asset_path)
        elif case["id"] == "partial_apply_unsupported_op":
            details = run_partial_apply_unsupported_op(client, request_id_base + 100, asset_path)
        else:
            raise BlueprintNegativeSuiteError("runner_error", f"unsupported case id: {case['id']}")

        result["status"] = "pass"
        result["details"] = details
        return result
    except SystemExit as exc:
        result["failureKind"] = "runner_system_exit"
        result["reason"] = f"system_exit:{exc.code}"
        return result
    except BlueprintNegativeSuiteError as exc:
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
            cleanup_blueprint_fixture(client, request_id_base + 900, asset_path=asset_path)
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
    parser = argparse.ArgumentParser(description="Run Blueprint negative and boundary contract tests against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the project-local loomle client")
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
            request_id_base=320000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "blueprint",
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
