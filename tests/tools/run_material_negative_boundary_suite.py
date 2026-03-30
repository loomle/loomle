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
from run_material_workflow_truth_suite import cleanup_material_fixture, create_material_fixture  # noqa: E402
from run_pcg_graph_test_plan import blank_surface_matrix, compact_json, wait_for_bridge_ready  # noqa: E402


class MaterialNegativeSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str, *, details: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.kind = kind
        self.details = details or {}


NEGATIVE_CASES = [
    {
        "id": "stale_expected_revision_conflict",
        "fixture": "material_graph",
        "operation": "addNode.byClass",
        "families": ["expression", "parameter"],
        "summary": "stale expectedRevision should reject a second material mutate after revision advancement",
    },
    {
        "id": "duplicate_client_ref_rejected",
        "fixture": "material_graph",
        "operation": "addNode.byClass",
        "families": ["constant", "parameter"],
        "summary": "duplicate clientRef values should be rejected on material graph mutates",
    },
    {
        "id": "set_pin_default_unsupported",
        "fixture": "material_graph",
        "operation": "setPinDefault",
        "families": ["parameter"],
        "summary": "setPinDefault should surface as unsupported on material graphs",
    },
    {
        "id": "connect_pins_bad_output_pin",
        "fixture": "material_graph",
        "operation": "connectPins",
        "families": ["expression", "parameter"],
        "summary": "connectPins should reject missing material source output pins",
    },
    {
        "id": "disconnect_pins_bad_output_pin",
        "fixture": "material_graph",
        "operation": "disconnectPins",
        "families": ["expression", "parameter"],
        "summary": "disconnectPins should reject missing material source output pins",
    },
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "material",
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


def call_tool_allow_error(
    client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]
) -> tuple[dict[str, Any], bool]:
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
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [{"op": "addNode.byClass", "args": {"nodeClassPath": node_class_path}}],
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        raise MaterialNegativeSuiteError("runner_error", f"missing addNode opResults: {compact_json(payload)}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    node_id = first.get("nodeId")
    if not first.get("ok") or not isinstance(node_id, str) or not node_id:
        raise MaterialNegativeSuiteError("runner_error", f"addNode failed: {compact_json(payload)}")
    return node_id


def expect_error_contains(payload: dict[str, Any], needle: str, *, kind: str) -> None:
    haystack = " ".join([str(payload.get("message", "")), str(payload.get("detail", ""))])
    if needle not in haystack:
        raise MaterialNegativeSuiteError(kind, f"expected error to contain {needle!r}: {compact_json(payload)}")


def query_material_revision_and_node_count(client: McpStdioClient, request_id: int, asset_path: str) -> tuple[str, int]:
    payload = call_tool(
        client,
        request_id,
        "graph.query",
        {"assetPath": asset_path, "graphName": "MaterialGraph", "graphType": "material", "limit": 200},
    )
    revision = payload.get("revision")
    snapshot = payload.get("semanticSnapshot")
    nodes = snapshot.get("nodes") if isinstance(snapshot, dict) else None
    if not isinstance(revision, str) or not revision:
        raise MaterialNegativeSuiteError("query_gap", f"material query missing revision: {compact_json(payload)}")
    if not isinstance(nodes, list):
        raise MaterialNegativeSuiteError("query_gap", f"material query missing nodes[]: {compact_json(payload)}")
    return revision, len(nodes)


def run_stale_expected_revision_conflict(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    revision_r0, node_count_r0 = query_material_revision_and_node_count(client, request_id_base + 1, asset_path)
    apply_payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "expectedRevision": revision_r0,
            "ops": [{"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}],
        },
    )
    first_results = apply_payload.get("opResults")
    if not isinstance(first_results, list) or not first_results or not isinstance(first_results[0], dict) or not first_results[0].get("ok"):
        raise MaterialNegativeSuiteError("runner_error", f"control mutate failed: {compact_json(apply_payload)}")
    revision_r1, node_count_r1 = query_material_revision_and_node_count(client, request_id_base + 3, asset_path)
    if revision_r1 == revision_r0 or node_count_r1 != node_count_r0 + 1:
        raise MaterialNegativeSuiteError(
            "runner_error",
            f"control mutate did not advance material graph state: before={(revision_r0, node_count_r0)} after={(revision_r1, node_count_r1)}",
        )

    payload = call_tool(
        client,
        request_id_base + 4,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "expectedRevision": revision_r0,
            "ops": [{"op": "addNode.byClass", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}}],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    if payload.get("domainCode") != "REVISION_CONFLICT":
        raise MaterialNegativeSuiteError(
            "contract_surface_gap",
            f"expected REVISION_CONFLICT for stale expectedRevision: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    revision_after, node_count_after = query_material_revision_and_node_count(client, request_id_base + 5, asset_path)
    if revision_after != revision_r1 or node_count_after != node_count_r1:
        surface_matrix["queryStructure"] = "fail"
        raise MaterialNegativeSuiteError(
            "query_surface_gap",
            f"stale expectedRevision changed graph state: before={(revision_r1, node_count_r1)} after={(revision_after, node_count_after)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    surface_matrix["queryStructure"] = "pass"
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_duplicate_client_ref_rejected(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    payload = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
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
    surface_matrix["mutate"] = "pass"
    op_results = extract_error_op_results(payload)
    if len(op_results) < 2:
        raise MaterialNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef missing opResults: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    second = op_results[1]
    if second.get("errorCode") != "INVALID_ARGUMENT":
        surface_matrix["diagnostics"] = "fail"
        raise MaterialNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef wrong errorCode: {compact_json(second)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": second},
        )
    if "Duplicate clientRef" not in str(second.get("errorMessage", "")):
        surface_matrix["diagnostics"] = "fail"
        raise MaterialNegativeSuiteError(
            "diagnostic_surface_gap",
            f"duplicate clientRef wrong errorMessage: {compact_json(second)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload, "opResult": second},
        )
    surface_matrix["diagnostics"] = "pass"
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_set_pin_default_unsupported(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    node_id = add_node_by_class(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/Engine.MaterialExpressionScalarParameter",
    )
    payload = call_tool(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [
                {
                    "op": "setPinDefault",
                    "args": {"target": {"nodeId": node_id, "pin": "DefaultValue"}, "value": 0.5},
                }
            ],
        },
        expect_error=True,
    )
    surface_matrix["mutate"] = "pass"
    detail = payload.get("detail")
    parsed_detail = None
    if isinstance(detail, str) and detail.strip():
        try:
            parsed = json.loads(detail)
        except Exception:
            parsed = None
        if isinstance(parsed, dict):
            parsed_detail = parsed
    op_results = extract_error_op_results(payload)
    if parsed_detail is not None and parsed_detail.get("code") != "UNSUPPORTED_OP":
        raise MaterialNegativeSuiteError(
            "contract_surface_gap",
            f"setPinDefault unsupported detail missing UNSUPPORTED_OP code: {compact_json(parsed_detail)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    if not op_results or op_results[0].get("errorCode") != "UNSUPPORTED_OP":
        raise MaterialNegativeSuiteError(
            "contract_surface_gap",
            f"setPinDefault unsupported opResults missing UNSUPPORTED_OP: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    expect_error_contains(payload, "Unsupported op for material: setpindefault", kind="contract_surface_gap")
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_connect_pins_bad_output_pin(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    setup = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [
                {"op": "addNode.byClass", "clientRef": "scalar_a", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}},
                {"op": "addNode.byClass", "clientRef": "multiply_ab", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"}},
            ],
        },
    )
    op_results = setup.get("opResults")
    if not isinstance(op_results, list) or len(op_results) < 2:
        raise MaterialNegativeSuiteError("runner_error", f"missing material setup opResults: {compact_json(setup)}")
    source_id = op_results[0].get("nodeId") if isinstance(op_results[0], dict) else None
    target_id = op_results[1].get("nodeId") if isinstance(op_results[1], dict) else None
    if not isinstance(source_id, str) or not isinstance(target_id, str):
        raise MaterialNegativeSuiteError("runner_error", f"missing material setup node ids: {compact_json(setup)}")
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeId": source_id, "pin": "DefinitelyMissingOutput"},
                        "to": {"nodeId": target_id, "pin": "A"},
                    },
                }
            ],
        },
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise MaterialNegativeSuiteError(
            "contract_surface_gap",
            f"connectPins accepted missing material output pin: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    if "pin" not in (str(payload.get("message", "")) + " " + str(payload.get("detail", ""))).lower():
        surface_matrix["diagnostics"] = "fail"
        raise MaterialNegativeSuiteError(
            "diagnostic_surface_gap",
            f"connectPins missing pin-oriented diagnostics: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    surface_matrix["diagnostics"] = "pass"
    return {
        "surfaceMatrix": surface_matrix,
        "errorPayload": payload,
    }


def run_disconnect_pins_bad_output_pin(client: McpStdioClient, request_id_base: int, asset_path: str) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    setup = call_tool(
        client,
        request_id_base + 1,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [
                {"op": "addNode.byClass", "clientRef": "scalar_a", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}},
                {"op": "addNode.byClass", "clientRef": "multiply_ab", "args": {"nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"}},
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeRef": "scalar_a", "pin": ""},
                        "to": {"nodeRef": "multiply_ab", "pin": "A"},
                    },
                },
            ],
        },
    )
    op_results = setup.get("opResults")
    if not isinstance(op_results, list) or len(op_results) < 2:
        raise MaterialNegativeSuiteError("runner_error", f"missing material setup opResults: {compact_json(setup)}")
    source_id = op_results[0].get("nodeId") if isinstance(op_results[0], dict) else None
    target_id = op_results[1].get("nodeId") if isinstance(op_results[1], dict) else None
    if not isinstance(source_id, str) or not isinstance(target_id, str):
        raise MaterialNegativeSuiteError("runner_error", f"missing material setup node ids: {compact_json(setup)}")
    payload, has_error = call_tool_allow_error(
        client,
        request_id_base + 2,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphName": "MaterialGraph",
            "graphType": "material",
            "ops": [
                {
                    "op": "disconnectPins",
                    "args": {
                        "from": {"nodeId": source_id, "pin": "DefinitelyMissingOutput"},
                        "to": {"nodeId": target_id, "pin": "A"},
                    },
                }
            ],
        },
    )
    surface_matrix["mutate"] = "pass"
    if not has_error:
        surface_matrix["mutate"] = "fail"
        raise MaterialNegativeSuiteError(
            "contract_surface_gap",
            f"disconnectPins accepted missing material output pin: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "unexpectedPayload": payload},
        )
    if "pin" not in (str(payload.get("message", "")) + " " + str(payload.get("detail", ""))).lower():
        surface_matrix["diagnostics"] = "fail"
        raise MaterialNegativeSuiteError(
            "diagnostic_surface_gap",
            f"disconnectPins missing pin-oriented diagnostics: {compact_json(payload)}",
            details={"surfaceMatrix": surface_matrix, "errorPayload": payload},
        )
    surface_matrix["diagnostics"] = "pass"
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
    asset_path = f"/Game/Codex/MaterialNegativeBoundary/{case['id']}_{case_index}"
    try:
        _ = create_material_fixture(client, request_id_base, asset_path=asset_path)

        if case["id"] == "stale_expected_revision_conflict":
            details = run_stale_expected_revision_conflict(client, request_id_base + 100, asset_path)
        elif case["id"] == "duplicate_client_ref_rejected":
            details = run_duplicate_client_ref_rejected(client, request_id_base + 100, asset_path)
        elif case["id"] == "set_pin_default_unsupported":
            details = run_set_pin_default_unsupported(client, request_id_base + 100, asset_path)
        elif case["id"] == "connect_pins_bad_output_pin":
            details = run_connect_pins_bad_output_pin(client, request_id_base + 100, asset_path)
        elif case["id"] == "disconnect_pins_bad_output_pin":
            details = run_disconnect_pins_bad_output_pin(client, request_id_base + 100, asset_path)
        else:
            raise MaterialNegativeSuiteError("runner_error", f"unsupported case id: {case['id']}")

        result["status"] = "pass"
        result["details"] = details
        return result
    except SystemExit as exc:
        result["failureKind"] = "runner_system_exit"
        result["reason"] = f"system_exit:{exc.code}"
        return result
    except MaterialNegativeSuiteError as exc:
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
            cleanup_material_fixture(client, request_id_base + 900, asset_path=asset_path)
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
    parser = argparse.ArgumentParser(description="Run Material negative and boundary contract tests against the current LOOMLE bridge.")
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
            request_id_base=220000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "material",
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
