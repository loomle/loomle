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
    add_node,
    blank_surface_matrix,
    cleanup_pcg_fixture,
    compact_json,
    create_pcg_fixture,
    query_pcg_snapshot,
    set_pin_default,
    wait_for_bridge_ready,
)
from run_pcg_workflow_truth_suite import find_node, verify_graph  # noqa: E402


class ChildGraphRefSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


CHILD_GRAPH_REF_CASES = [
    {
        "id": "subgraph_child_graph_ref_traversal",
        "fixture": "pcg_graph",
        "families": ["struct"],
        "nodeClassPath": "/Script/PCG.PCGSubgraphSettings",
        "summary": "Subgraph should surface childGraphRef and keep the second hop queryable as a graph-native traversal.",
    },
    {
        "id": "loop_child_graph_ref_traversal",
        "fixture": "pcg_graph",
        "families": ["struct"],
        "nodeClassPath": "/Script/PCG.PCGLoopSettings",
        "summary": "Loop should surface childGraphRef and keep the second hop queryable as a graph-native traversal.",
    },
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "pcg",
        "suite": "child_graph_ref",
        "summary": {
            "totalCases": len(CHILD_GRAPH_REF_CASES),
            "families": sorted(
                {
                    family
                    for case in CHILD_GRAPH_REF_CASES
                    for family in case.get("families", [])
                    if isinstance(family, str) and family
                }
            ),
            "querySurfaceKinds": ["child_graph_ref"],
        },
        "cases": [
            {
                "id": case["id"],
                "fixture": case["fixture"],
                "families": case.get("families", []),
                "nodeClassPath": case["nodeClassPath"],
                "querySurfaceKind": "child_graph_ref",
                "summary": case["summary"],
            }
            for case in CHILD_GRAPH_REF_CASES
        ],
    }


def _query_pcg_by_ref(client: McpStdioClient, request_id: int, *, graph_ref: dict[str, Any]) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "pcg.query",
        {"graph": graph_ref, "includeConnections": True},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise ChildGraphRefSuiteError("child_graph_ref_unqueryable", f"pcg.query(graph) missing semanticSnapshot: {compact_json(payload)}")
    return snapshot


def _query_pcg_asset(client: McpStdioClient, request_id: int, *, asset_path: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "pcg.query",
        {"assetPath": asset_path, "includeConnections": True},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise ChildGraphRefSuiteError("child_graph_ref_unqueryable", f"pcg.query(assetPath) missing semanticSnapshot: {compact_json(payload)}")
    return snapshot


def execute_case_with_fresh_client(
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    parent_asset_path = f"/Game/Codex/PCGChildGraphRef/{case['id']}_{case_index}/ParentGraph"
    child_asset_path = f"/Game/Codex/PCGChildGraphRef/{case['id']}_{case_index}/ChildGraph"
    result: dict[str, Any] = {
        "caseId": case["id"],
        "fixture": case["fixture"],
        "families": case.get("families", []),
        "querySurfaceKind": "child_graph_ref",
        "status": "fail",
        "details": {"surfaceMatrix": blank_surface_matrix()},
        "logs": [],
    }
    client = McpStdioClient(project_root, loomle_binary, timeout_s)
    try:
        wait_log = io.StringIO()
        with contextlib.redirect_stdout(wait_log):
            wait_for_bridge_ready(client)
        result["logs"] = [line for line in wait_log.getvalue().splitlines() if line]

        create_pcg_fixture(
            client,
            request_id_base + 10,
            asset_path=parent_asset_path,
            fixture_id=case["fixture"],
            actor_offset=float(case_index * 300),
        )
        create_pcg_fixture(
            client,
            request_id_base + 20,
            asset_path=child_asset_path,
            fixture_id="pcg_graph",
            actor_offset=float(case_index * 300 + 100),
        )

        node_id = add_node(
            client,
            request_id_base + 30,
            asset_path=parent_asset_path,
            node_class_path=case["nodeClassPath"],
        )
        result["details"]["surfaceMatrix"]["mutate"] = "pass"

        set_pin_default(
            client,
            request_id_base + 40,
            asset_path=parent_asset_path,
            node_id=node_id,
            pin="SubgraphOverride",
            value=child_asset_path,
        )
        result["details"]["surfaceMatrix"]["mutate"] = "pass"

        root_snapshot = query_pcg_snapshot(client, request_id_base + 50, parent_asset_path)
        result["details"]["surfaceMatrix"]["queryStructure"] = "pass"
        node = find_node(root_snapshot, node_id)
        if not isinstance(node, dict):
            raise ChildGraphRefSuiteError("child_graph_ref_node_missing", f"node missing from root snapshot: {compact_json(root_snapshot)}")

        verify = verify_graph(client, request_id_base + 60, parent_asset_path)
        result["details"]["verify"] = verify
        result["details"]["surfaceMatrix"]["verify"] = "pass"
        result["details"]["surfaceMatrix"]["diagnostics"] = "pass"

        child_graph_ref = node.get("childGraphRef")
        if not isinstance(child_graph_ref, dict):
            raise ChildGraphRefSuiteError("child_graph_ref_unsurfaced", f"pcg.query missing childGraphRef: {compact_json(node)}")

        child_ref_asset_path = child_graph_ref.get("assetPath")
        if child_ref_asset_path != child_asset_path:
            raise ChildGraphRefSuiteError(
                "child_graph_ref_second_hop_mismatch",
                f"childGraphRef should point at the configured child asset: expected={child_asset_path} actual={compact_json(child_graph_ref)}",
            )

        by_ref_snapshot = _query_pcg_by_ref(client, request_id_base + 70, graph_ref=child_graph_ref)
        by_asset_snapshot = _query_pcg_asset(client, request_id_base + 80, asset_path=child_asset_path)

        by_ref_signature = by_ref_snapshot.get("signature")
        by_asset_signature = by_asset_snapshot.get("signature")
        if not isinstance(by_ref_signature, str) or not by_ref_signature:
            raise ChildGraphRefSuiteError("child_graph_ref_unqueryable", f"child graph query-by-ref missing signature: {compact_json(by_ref_snapshot)}")
        if not isinstance(by_asset_signature, str) or not by_asset_signature:
            raise ChildGraphRefSuiteError("child_graph_ref_unqueryable", f"child graph query-by-asset missing signature: {compact_json(by_asset_snapshot)}")
        if by_ref_signature != by_asset_signature:
            raise ChildGraphRefSuiteError(
                "child_graph_ref_second_hop_mismatch",
                f"child graph query surfaces disagree: byRef={compact_json(by_ref_snapshot)} byAsset={compact_json(by_asset_snapshot)}",
            )

        result["details"]["surfaceMatrix"]["queryTruth"] = "pass"
        result["details"]["query"] = {
            "childGraphRef": child_graph_ref,
            "secondHop": {
                "signature": by_ref_signature,
                "nodeCount": len(by_ref_snapshot.get("nodes", [])) if isinstance(by_ref_snapshot.get("nodes"), list) else 0,
            },
        }
        result["status"] = "pass"
    except ChildGraphRefSuiteError as exc:
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        if exc.kind in {
            "child_graph_ref_unsurfaced",
            "child_graph_ref_second_hop_missing",
            "child_graph_ref_second_hop_mismatch",
            "child_graph_ref_unqueryable",
        }:
            result["details"]["surfaceMatrix"]["queryTruth"] = "fail"
    except BaseException as exc:
        result["failureKind"] = "runner_error"
        result["reason"] = str(exc)
    finally:
        try:
            cleanup_pcg_fixture(client, request_id_base + 100, asset_path=parent_asset_path, actor_path=None)
        except Exception:
            pass
        try:
            cleanup_pcg_fixture(client, request_id_base + 110, asset_path=child_asset_path, actor_path=None)
        except Exception:
            pass
        client.close()
    return result


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    failure_kinds = Counter()
    family_buckets: dict[str, dict[str, Any]] = {}
    passed = 0
    failed = 0
    for result in results:
        if result.get("status") == "pass":
            passed += 1
        else:
            failed += 1
            failure_kind = result.get("failureKind")
            if isinstance(failure_kind, str) and failure_kind:
                failure_kinds[failure_kind] += 1
        for family in result.get("families", []):
            if not isinstance(family, str) or not family:
                continue
            bucket = family_buckets.setdefault(family, {"family": family, "totalCases": 0, "passed": 0, "failed": 0, "failureKinds": Counter()})
            bucket["totalCases"] += 1
            if result.get("status") == "pass":
                bucket["passed"] += 1
            else:
                bucket["failed"] += 1
                failure_kind = result.get("failureKind")
                if isinstance(failure_kind, str) and failure_kind:
                    bucket["failureKinds"][failure_kind] += 1

    family_summary = []
    for family in sorted(family_buckets):
        bucket = family_buckets[family]
        family_summary.append(
            {
                "family": bucket["family"],
                "totalCases": bucket["totalCases"],
                "passed": bucket["passed"],
                "failed": bucket["failed"],
                "failureKinds": dict(sorted(bucket["failureKinds"].items())),
            }
        )

    return {
        "totalCases": len(results),
        "passed": passed,
        "failed": failed,
        "families": sorted(
            {
                family
                for result in results
                for family in result.get("families", [])
                if isinstance(family, str) and family
            }
        ),
        "failureKinds": dict(sorted(failure_kinds.items())),
        "familySummary": family_summary,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PCG childGraphRef traversal regressions against the current LOOMLE bridge.")
    parser.add_argument("--project-root")
    parser.add_argument("--loomle-bin")
    parser.add_argument("--dev-config", action="store_true")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--output")
    parser.add_argument("--list-cases", action="store_true")
    parser.add_argument("--max-cases", type=int, default=0)
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)
    cases = CHILD_GRAPH_REF_CASES[: args.max_cases] if args.max_cases > 0 else CHILD_GRAPH_REF_CASES

    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=101000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "child_graph_ref",
        "summary": build_summary(results),
        "results": results,
    }
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
