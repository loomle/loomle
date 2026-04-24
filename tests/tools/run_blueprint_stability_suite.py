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
from domain_test_helpers import (  # noqa: E402
    blank_surface_matrix,
    blueprint_edit_args_from_legacy_payload,
    compact_json,
    wait_for_bridge_ready,
)
from run_blueprint_workflow_truth_suite import (  # noqa: E402
    WORKFLOW_CASES,
    assert_workflow_structure,
    audit_workflow_query_truth,
    build_client_ref_map,
    cleanup_blueprint_fixture,
    create_blueprint_fixture,
    load_case_payload,
    query_blueprint_snapshot,
    rewrite_live_node_ids,
    run_workflow_case as run_blueprint_workflow_case,
    verify_blueprint_graph,
)


class BlueprintStabilitySuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


STABILITY_CASES = [
    {
        "id": "query_snapshot_repeatability_roundtrip",
        "fixture": "blueprint_event_graph",
        "families": ["branch", "function_call"],
        "summary": "Repeated Blueprint graph inspect snapshots should stay stable after a simple local branch edit.",
    },
    {
        "id": "verify_repeatability_workflow",
        "fixture": "blueprint_event_graph",
        "families": ["branch", "function_call", "utility"],
        "summary": "Repeated Blueprint validate calls should stay stable on a workflow graph.",
        "workflowCaseId": "replace_branch_with_sequence",
    },
    {
        "id": "workflow_repeatability_fresh_session",
        "fixture": "blueprint_event_graph",
        "families": ["function_call", "struct", "utility"],
        "summary": "A Blueprint workflow should produce the same status and surface matrix across fresh sessions.",
        "workflowCaseId": "replace_delay_with_do_once",
    },
]

SURFACES = ("mutate", "queryStructure", "queryTruth", "engineTruth", "verify", "diagnostics")


def list_cases_payload() -> dict[str, Any]:
    families = sorted(
        {
            family
            for case in STABILITY_CASES
            for family in case.get("families", [])
            if isinstance(family, str) and family
        }
    )
    return {
        "version": "1",
        "graphType": "blueprint",
        "suite": "stability",
        "summary": {
            "totalCases": len(STABILITY_CASES),
            "freshSessionCases": sum(1 for case in STABILITY_CASES if case["id"] == "workflow_repeatability_fresh_session"),
            "families": families,
        },
        "cases": [
            {
                "id": case["id"],
                "fixture": case["fixture"],
                "families": case.get("families", []),
                "summary": case["summary"],
                "workflowCaseId": case.get("workflowCaseId"),
            }
            for case in STABILITY_CASES
        ],
    }


def normalize_pin(pin: dict[str, Any]) -> dict[str, Any]:
    default_obj = pin.get("default")
    return {
        "name": pin.get("name"),
        "direction": pin.get("direction"),
        "defaultValue": pin.get("defaultValue"),
        "defaultText": pin.get("defaultText"),
        "default": {
            "value": default_obj.get("value") if isinstance(default_obj, dict) else None,
            "text": default_obj.get("text") if isinstance(default_obj, dict) else None,
        },
    }


def normalize_node_snapshot(node: dict[str, Any]) -> dict[str, Any]:
    pins = node.get("pins")
    normalized_pins = []
    if isinstance(pins, list):
        normalized_pins = [normalize_pin(pin) for pin in pins if isinstance(pin, dict)]
        normalized_pins.sort(key=lambda pin: str(pin.get("name") or ""))
    return {
        "id": node.get("id"),
        "title": node.get("title"),
        "nodeClassPath": node.get("nodeClassPath"),
        "pins": normalized_pins,
    }


def find_node(snapshot: dict[str, Any], node_id: str) -> dict[str, Any] | None:
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        return None
    for node in nodes:
        if isinstance(node, dict) and node.get("id") == node_id:
            return node
    return None


def execute_query_snapshot_repeatability_roundtrip(
    client: McpStdioClient, request_id_base: int, asset_path: str
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    payload = load_case_payload(next(case for case in WORKFLOW_CASES if case["id"] == "branch_local_subgraph"))
    payload["assetPath"] = asset_path
    payload["graphName"] = "EventGraph"
    edit_payload = blueprint_edit_args_from_legacy_payload(payload)
    mutate_result = call_tool(client, request_id_base + 1, "blueprint.graph.edit", edit_payload)
    op_results = mutate_result.get("opResults")
    if not isinstance(op_results, list) or len(op_results) < 1:
        raise BlueprintStabilitySuiteError("runner_error", f"missing blueprint setup opResults: {compact_json(mutate_result)}")
    ref_map = build_client_ref_map(edit_payload, mutate_result)
    branch_id = ref_map.get("branch_main")
    if not isinstance(branch_id, str) or not branch_id:
        raise BlueprintStabilitySuiteError("runner_error", f"missing branch node id: {compact_json(mutate_result)}")
    surface_matrix["mutate"] = "pass"

    first = query_blueprint_snapshot(client, request_id_base + 10, asset_path)
    second = query_blueprint_snapshot(client, request_id_base + 11, asset_path)
    first_node = find_node(first, branch_id)
    second_node = find_node(second, branch_id)
    if not isinstance(first_node, dict) or not isinstance(second_node, dict):
        raise BlueprintStabilitySuiteError("query_repeatability_gap", "query snapshot missing target Blueprint node on repeated reads")

    normalized_first = normalize_node_snapshot(first_node)
    normalized_second = normalize_node_snapshot(second_node)
    if normalized_first != normalized_second:
        raise BlueprintStabilitySuiteError(
            "query_repeatability_gap",
            f"blueprint query snapshot changed between repeated reads: first={compact_json(normalized_first)} second={compact_json(normalized_second)}",
        )
    surface_matrix["queryStructure"] = "pass"
    surface_matrix["queryTruth"] = "pass"

    return {
        "surfaceMatrix": surface_matrix,
        "nodeId": branch_id,
        "queryNode": normalized_first,
    }


def execute_verify_repeatability_workflow(
    client: McpStdioClient, request_id_base: int, asset_path: str, workflow_case: dict[str, Any]
) -> dict[str, Any]:
    surface_matrix = blank_surface_matrix()
    payload = load_case_payload(workflow_case)
    payload["assetPath"] = asset_path
    payload["graphName"] = "EventGraph"
    initial_snapshot = query_blueprint_snapshot(client, request_id_base, asset_path)
    payload = rewrite_live_node_ids(payload, initial_snapshot)
    edit_payload = blueprint_edit_args_from_legacy_payload(payload)
    mutate_result = call_tool(client, request_id_base + 1, "blueprint.graph.edit", edit_payload)
    surface_matrix["mutate"] = "pass"

    ref_map = build_client_ref_map(edit_payload, mutate_result)
    snapshot = query_blueprint_snapshot(client, request_id_base + 2, asset_path)
    _ = assert_workflow_structure(workflow_case, snapshot, ref_map)
    surface_matrix["queryStructure"] = "pass"
    _ = audit_workflow_query_truth(workflow_case, snapshot, ref_map)
    surface_matrix["queryTruth"] = "pass"

    first_verify = verify_blueprint_graph(client, request_id_base + 3, asset_path)
    second_verify = verify_blueprint_graph(client, request_id_base + 4, asset_path)
    if first_verify != second_verify:
        raise BlueprintStabilitySuiteError(
            "verify_repeatability_gap",
            f"blueprint verify changed between repeated calls: first={compact_json(first_verify)} second={compact_json(second_verify)}",
        )
    surface_matrix["verify"] = "pass"
    surface_matrix["diagnostics"] = "pass"
    return {"surfaceMatrix": surface_matrix, "verify": first_verify}


def execute_workflow_repeatability_fresh_session(
    *, project_root: Path, loomle_binary: Path, timeout_s: float, workflow_case: dict[str, Any]
) -> dict[str, Any]:
    first = execute_workflow_case_with_fresh_client(
        project_root=project_root,
        loomle_binary=loomle_binary,
        timeout_s=timeout_s,
        request_id_base=370000,
        case_index=1,
        case=workflow_case,
    )
    second = execute_workflow_case_with_fresh_client(
        project_root=project_root,
        loomle_binary=loomle_binary,
        timeout_s=timeout_s,
        request_id_base=371000,
        case_index=2,
        case=workflow_case,
    )

    first_status = first.get("status")
    second_status = second.get("status")
    if first_status != second_status:
        raise BlueprintStabilitySuiteError(
            "fresh_session_repeatability_gap",
            f"blueprint workflow status changed across fresh sessions: first={compact_json(first)} second={compact_json(second)}",
        )

    first_failure_kind = first.get("failureKind")
    second_failure_kind = second.get("failureKind")
    if first_failure_kind != second_failure_kind:
        raise BlueprintStabilitySuiteError(
            "fresh_session_repeatability_gap",
            f"blueprint workflow failureKind changed across fresh sessions: first={compact_json(first)} second={compact_json(second)}",
        )

    first_surface = first.get("details", {}).get("surfaceMatrix") if isinstance(first.get("details"), dict) else None
    second_surface = second.get("details", {}).get("surfaceMatrix") if isinstance(second.get("details"), dict) else None
    if first_surface != second_surface:
        raise BlueprintStabilitySuiteError(
            "fresh_session_repeatability_gap",
            f"blueprint workflow surface matrix changed across fresh sessions: first={compact_json(first_surface)} second={compact_json(second_surface)}",
        )

    return {
        "surfaceMatrix": first_surface or blank_surface_matrix(),
        "observedStatus": first_status,
        "observedFailureKind": first_failure_kind,
        "firstReason": first.get("message") or first.get("reason"),
        "secondReason": second.get("message") or second.get("reason"),
    }


def run_case(
    client: McpStdioClient | None,
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "fixture": case["fixture"],
        "families": case.get("families", []),
        "status": "fail",
    }

    if case["id"] == "workflow_repeatability_fresh_session":
        workflow_case = next(workflow for workflow in WORKFLOW_CASES if workflow["id"] == case["workflowCaseId"])
        try:
            result["details"] = execute_workflow_repeatability_fresh_session(
                project_root=project_root,
                loomle_binary=loomle_binary,
                timeout_s=timeout_s,
                workflow_case=workflow_case,
            )
            result["status"] = "pass"
            return result
        except BlueprintStabilitySuiteError as exc:
            result["failureKind"] = exc.kind
            result["reason"] = str(exc)
            return result

    if client is None:
        result["failureKind"] = "runner_error"
        result["reason"] = "stability case requires active client"
        return result

    asset_path = f"/Game/Codex/BlueprintStability/{case['id']}_{case_index}"
    try:
        _ = create_blueprint_fixture(client, request_id_base, asset_path=asset_path)

        if case["id"] == "query_snapshot_repeatability_roundtrip":
            details = execute_query_snapshot_repeatability_roundtrip(client, request_id_base + 100, asset_path)
        elif case["id"] == "verify_repeatability_workflow":
            workflow_case = next(workflow for workflow in WORKFLOW_CASES if workflow["id"] == case["workflowCaseId"])
            details = execute_verify_repeatability_workflow(client, request_id_base + 100, asset_path, workflow_case)
        else:
            raise BlueprintStabilitySuiteError("runner_error", f"unsupported blueprint stability case {case['id']}")

        result["status"] = "pass"
        result["details"] = details
        return result
    except BlueprintStabilitySuiteError as exc:
        details = result.setdefault("details", {})
        if not isinstance(details, dict):
            details = {}
            result["details"] = details
        if "surfaceMatrix" not in details:
            details["surfaceMatrix"] = blank_surface_matrix()
        surface_matrix = details.get("surfaceMatrix")
        if not isinstance(surface_matrix, dict):
            surface_matrix = blank_surface_matrix()
            details["surfaceMatrix"] = surface_matrix
        if exc.kind == "query_repeatability_gap":
            surface_matrix["queryStructure"] = "fail"
            surface_matrix["queryTruth"] = "fail"
        elif exc.kind == "verify_repeatability_gap":
            surface_matrix["verify"] = "fail"
            surface_matrix["diagnostics"] = "fail"
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        return result
    except Exception as exc:
        result["details"] = {"surfaceMatrix": blank_surface_matrix()}
        result["failureKind"] = "runner_error"
        result["reason"] = str(exc)
        return result
    finally:
        try:
            cleanup_blueprint_fixture(client, request_id_base + 900, asset_path=asset_path)
        except BaseException:
            pass


def execute_workflow_case_with_fresh_client(
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
            result = run_blueprint_workflow_case(client, request_id_base=request_id_base, case=case)
    except SystemExit as exc:
        result = {
            "caseId": case["id"],
            "families": case.get("families", []),
            "status": "fail",
            "failureKind": "runner_system_exit",
            "reason": f"system_exit:{exc.code}",
        }
    except Exception as exc:
        result = {
            "caseId": case["id"],
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
    surface_totals: dict[str, Counter[str]] = {surface: Counter() for surface in SURFACES}
    family_rows: dict[str, dict[str, Any]] = {}

    for result in results:
        details = result.get("details")
        if isinstance(details, dict):
            surface_matrix = details.get("surfaceMatrix")
            if isinstance(surface_matrix, dict):
                for surface in SURFACES:
                    value = surface_matrix.get(surface)
                    if isinstance(value, str) and value:
                        surface_totals[surface][value] += 1

        for family in result.get("families", []):
            if not isinstance(family, str) or not family:
                continue
            row = family_rows.setdefault(
                family,
                {
                    "family": family,
                    "totalCases": 0,
                    "passed": 0,
                    "failed": 0,
                    "failureKinds": Counter(),
                    "surfaceMatrix": {surface: Counter() for surface in SURFACES},
                },
            )
            row["totalCases"] += 1
            if result.get("status") == "pass":
                row["passed"] += 1
            elif result.get("status") == "fail":
                row["failed"] += 1
                if isinstance(result.get("failureKind"), str):
                    row["failureKinds"][result["failureKind"]] += 1
            if isinstance(details, dict):
                surface_matrix = details.get("surfaceMatrix")
                if isinstance(surface_matrix, dict):
                    for surface in SURFACES:
                        value = surface_matrix.get(surface)
                        if isinstance(value, str) and value:
                            row["surfaceMatrix"][surface][value] += 1

    return {
        "totalCases": len(results),
        "passed": status_counter.get("pass", 0),
        "failed": status_counter.get("fail", 0),
        "failureKinds": dict(sorted(kind_counter.items())),
        "failureReasons": dict(sorted(reason_counter.items())),
        "surfaceMatrix": {
            surface: dict(sorted(counter.items()))
            for surface, counter in surface_totals.items()
            if counter
        },
        "familySummary": [
            {
                "family": row["family"],
                "totalCases": row["totalCases"],
                "passed": row["passed"],
                "failed": row["failed"],
                "failureKinds": dict(sorted(row["failureKinds"].items())),
                "surfaceMatrix": {
                    surface: dict(sorted(counter.items()))
                    for surface, counter in row["surfaceMatrix"].items()
                    if counter
                },
            }
            for row in sorted(family_rows.values(), key=lambda value: value["family"])
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Blueprint stability and repeatability tests against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--list-cases", action="store_true", help="Print the stability case registry and exit")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)

    cases = STABILITY_CASES[: args.max_cases] if args.max_cases > 0 else STABILITY_CASES
    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        if case["id"] == "workflow_repeatability_fresh_session":
            result = run_case(
                None,
                project_root=project_root,
                loomle_binary=loomle_binary,
                timeout_s=args.timeout,
                request_id_base=380000 + index * 1000,
                case_index=index,
                case=case,
            )
        else:
            client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=args.timeout)
            transcript = io.StringIO()
            try:
                with contextlib.redirect_stdout(transcript):
                    _ = client.request(1, "initialize", {})
                    wait_for_bridge_ready(client)
                    result = run_case(
                        client,
                        project_root=project_root,
                        loomle_binary=loomle_binary,
                        timeout_s=args.timeout,
                        request_id_base=380000 + index * 1000,
                        case_index=index,
                        case=case,
                    )
            except SystemExit as exc:
                result = {
                    "caseId": case["id"],
                    "fixture": case["fixture"],
                    "families": case.get("families", []),
                    "status": "fail",
                    "failureKind": "runner_system_exit",
                    "reason": f"system_exit:{exc.code}",
                }
            except Exception as exc:
                result = {
                    "caseId": case["id"],
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

        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "blueprint",
        "suite": "stability",
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
