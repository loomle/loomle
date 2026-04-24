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
    call_execute_exec_with_retry,
    call_tool,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tests" / "tools"))
from domain_test_helpers import blank_surface_matrix, compact_json, wait_for_bridge_ready  # noqa: E402
from run_material_workflow_truth_suite import query_material_snapshot, verify_material_graph  # noqa: E402


class MaterialChildGraphRefSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


CHILD_GRAPH_REF_CASES = [
    {
        "id": "material_function_call_child_graph_ref_traversal",
        "fixture": "material_graph",
        "families": ["expression"],
        "nodeClassPath": "/Script/Engine.MaterialExpressionMaterialFunctionCall",
        "summary": "MaterialFunctionCall should surface childGraphRef and keep the function graph queryable as a graph-native second hop.",
    }
]


def list_cases_payload() -> dict[str, Any]:
    return {
        "version": "1",
        "graphType": "material",
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


def create_material_function_fixture(
    client: McpStdioClient, request_id_base: int, *, material_asset_path: str, function_asset_path: str
) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base,
        code=(
            "import json\n"
            "import unreal\n"
            f"mat_asset={json.dumps(material_asset_path, ensure_ascii=False)}\n"
            f"fn_asset={json.dumps(function_asset_path, ensure_ascii=False)}\n"
            "for asset in [mat_asset, fn_asset]:\n"
            "  if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
            "    deleted = unreal.EditorAssetLibrary.delete_asset(asset)\n"
            "    if not deleted:\n"
            "      raise RuntimeError(f'failed to delete material child-graph fixture asset: {asset}')\n"
            "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
            "mat_pkg, mat_name = mat_asset.rsplit('/', 1)\n"
            "fn_pkg, fn_name = fn_asset.rsplit('/', 1)\n"
            "mat = asset_tools.create_asset(mat_name, mat_pkg, unreal.Material, unreal.MaterialFactoryNew())\n"
            "fn = asset_tools.create_asset(fn_name, fn_pkg, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())\n"
            "if mat is None or fn is None:\n"
            "  raise RuntimeError('failed to create material child-graph fixture assets')\n"
            "lib = unreal.MaterialEditingLibrary\n"
            "call = lib.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -400, 0)\n"
            "call.set_editor_property('material_function', fn)\n"
            "lib.recompile_material(mat)\n"
            "unreal.EditorAssetLibrary.save_loaded_asset(mat)\n"
            "unreal.EditorAssetLibrary.save_loaded_asset(fn)\n"
            "print(json.dumps({'materialAssetPath': mat_asset, 'functionAssetPath': fn_asset, 'expressionName': call.get_name()}, ensure_ascii=False))\n"
        ),
    )
    parsed = parse_execute_json(payload)
    if not isinstance(parsed.get("materialAssetPath"), str) or not isinstance(parsed.get("functionAssetPath"), str):
        raise MaterialChildGraphRefSuiteError("fixture_gap", f"material childGraphRef fixture missing asset paths: {compact_json(payload)}")
    return parsed


def cleanup_material_function_fixture(
    client: McpStdioClient, request_id_base: int, *, material_asset_path: str, function_asset_path: str
) -> None:
    _ = client.request(
        request_id_base,
        "tools/call",
        {
            "name": "execute",
            "arguments": {
                "mode": "exec",
                "code": (
                    "import unreal\n"
                    f"assets = [{json.dumps(material_asset_path, ensure_ascii=False)}, {json.dumps(function_asset_path, ensure_ascii=False)}]\n"
                    "for asset in assets:\n"
                    "  if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                    "    unreal.EditorAssetLibrary.delete_asset(asset)\n"
                ),
            },
        },
    )


def _query_graph_by_ref(client: McpStdioClient, request_id: int, *, graph_ref: dict[str, Any]) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "material.query",
        {"graph": graph_ref, "includeConnections": True},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise MaterialChildGraphRefSuiteError("child_graph_ref_unqueryable", f"material.query(graph) missing semanticSnapshot: {compact_json(payload)}")
    return snapshot


def _query_graph_by_name(client: McpStdioClient, request_id: int, *, asset_path: str, graph_name: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "material.query",
        {"assetPath": asset_path, "graphName": graph_name, "includeConnections": True},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise MaterialChildGraphRefSuiteError("child_graph_ref_unqueryable", f"material.query(graphName) missing semanticSnapshot: {compact_json(payload)}")
    return snapshot


def _find_function_call_node(snapshot: dict[str, Any], *, function_asset_path: str) -> dict[str, Any] | None:
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        return None
    for node in nodes:
        if not isinstance(node, dict):
            continue
        if node.get("nodeClassPath") != "/Script/Engine.MaterialExpressionMaterialFunctionCall":
            continue
        child_graph_ref = node.get("childGraphRef")
        if isinstance(child_graph_ref, dict) and child_graph_ref.get("assetPath") == function_asset_path:
            return node
    return None


def execute_case_with_fresh_client(
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    material_asset_path = f"/Game/Codex/MaterialChildGraphRef/{case['id']}_{case_index}/ProbeMaterial"
    function_asset_path = f"/Game/Codex/MaterialChildGraphRef/{case['id']}_{case_index}/ProbeFunction"
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

        create_material_function_fixture(
            client,
            request_id_base + 10,
            material_asset_path=material_asset_path,
            function_asset_path=function_asset_path,
        )
        result["details"]["surfaceMatrix"]["mutate"] = "pass"

        root_snapshot = query_material_snapshot(client, request_id_base + 20, material_asset_path)
        result["details"]["surfaceMatrix"]["queryStructure"] = "pass"
        node = _find_function_call_node(root_snapshot, function_asset_path=function_asset_path)
        if not isinstance(node, dict):
            raise MaterialChildGraphRefSuiteError(
                "child_graph_ref_node_missing",
                f"material function call node missing from root snapshot: {compact_json(root_snapshot)}",
            )

        verify = verify_material_graph(client, request_id_base + 30, material_asset_path)
        result["details"]["verify"] = verify
        result["details"]["surfaceMatrix"]["verify"] = "pass"
        result["details"]["surfaceMatrix"]["diagnostics"] = "pass"

        child_graph_ref = node.get("childGraphRef")
        if not isinstance(child_graph_ref, dict):
            raise MaterialChildGraphRefSuiteError("child_graph_ref_unsurfaced", f"material.query missing childGraphRef: {compact_json(node)}")
        if child_graph_ref.get("assetPath") != function_asset_path:
            raise MaterialChildGraphRefSuiteError(
                "child_graph_ref_second_hop_mismatch",
                f"material childGraphRef points at the wrong asset: {compact_json(node)}",
            )

        by_ref_snapshot = _query_graph_by_ref(client, request_id_base + 50, graph_ref=child_graph_ref)
        graph_name = str(child_graph_ref.get("graphName") or "MaterialGraph")
        by_name_snapshot = _query_graph_by_name(client, request_id_base + 60, asset_path=function_asset_path, graph_name=graph_name)
        by_ref_signature = by_ref_snapshot.get("signature")
        by_name_signature = by_name_snapshot.get("signature")
        if not isinstance(by_ref_signature, str) or not by_ref_signature:
            raise MaterialChildGraphRefSuiteError(
                "child_graph_ref_unqueryable",
                f"child graph query-by-ref missing signature: {compact_json(by_ref_snapshot)}",
            )
        if not isinstance(by_name_signature, str) or not by_name_signature:
            raise MaterialChildGraphRefSuiteError(
                "child_graph_ref_unqueryable",
                f"child graph query-by-name missing signature: {compact_json(by_name_snapshot)}",
            )
        if by_ref_signature != by_name_signature:
            raise MaterialChildGraphRefSuiteError(
                "child_graph_ref_second_hop_mismatch",
                f"material child graph query surfaces disagree: byRef={compact_json(by_ref_snapshot)} byName={compact_json(by_name_snapshot)}",
            )

        result["details"]["surfaceMatrix"]["queryTruth"] = "pass"
        result["details"]["query"] = {
            "childGraphRef": child_graph_ref,
            "graphName": graph_name,
            "secondHop": {
                "signature": by_ref_signature,
                "nodeCount": len(by_ref_snapshot.get("nodes", [])) if isinstance(by_ref_snapshot.get("nodes"), list) else 0,
            },
        }
        result["status"] = "pass"
    except MaterialChildGraphRefSuiteError as exc:
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        if exc.kind in {
            "child_graph_ref_unsurfaced",
            "child_graph_ref_second_hop_missing",
            "child_graph_ref_second_hop_mismatch",
            "child_graph_ref_unqueryable",
        }:
            result["details"]["surfaceMatrix"]["queryTruth"] = "fail"
        elif exc.kind in {"child_graph_ref_node_missing"}:
            result["details"]["surfaceMatrix"]["queryStructure"] = "fail"
    finally:
        client.close()
    return result


def run_suite(project_root: Path, loomle_binary: Path, timeout_s: float) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    failure_kinds: Counter[str] = Counter()
    family_counter: Counter[str] = Counter()
    for index, case in enumerate(CHILD_GRAPH_REF_CASES):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=timeout_s,
            request_id_base=850000 + index * 100,
            case_index=index,
            case=case,
        )
        results.append(result)
        if result.get("status") != "pass" and isinstance(result.get("failureKind"), str):
            failure_kinds[result["failureKind"]] += 1
        for family in result.get("families", []):
            if isinstance(family, str) and family:
                family_counter[family] += 1
    passed = sum(1 for result in results if result.get("status") == "pass")
    failed = len(results) - passed
    return {
        "version": "1",
        "graphType": "material",
        "suite": "child_graph_ref",
        "summary": {
            "totalCases": len(results),
            "passed": passed,
            "failed": failed,
            "families": sorted(family_counter),
            "failureKinds": dict(sorted(failure_kinds.items())),
        },
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Material childGraphRef traversal regressions against the current LOOMLE bridge.")
    parser.add_argument("--project-root")
    parser.add_argument("--loomle-binary")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--output")
    parser.add_argument("--list-cases", action="store_true")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, None)
    loomle_binary = Path(args.loomle_binary).expanduser() if args.loomle_binary else resolve_default_loomle_binary(project_root)
    report = run_suite(project_root, loomle_binary, args.timeout)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
