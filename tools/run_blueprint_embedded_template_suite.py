#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
BLUEPRINT_NODE_DATABASE = REPO_ROOT / "workspace" / "Loomle" / "blueprint" / "catalogs" / "node-database.json"

EXPECTED_RECIPES = {
    "UK2Node_Timeline": "blueprint_timeline_graph",
    "UK2Node_AddComponent": "blueprint_component_template_context",
}


def load_blueprint_nodes() -> list[dict[str, Any]]:
    payload = json.loads(BLUEPRINT_NODE_DATABASE.read_text(encoding="utf-8"))
    nodes = payload.get("nodes")
    if not isinstance(nodes, list):
        raise ValueError(f"blueprint node database missing nodes[]: {BLUEPRINT_NODE_DATABASE}")
    return [node for node in nodes if isinstance(node, dict)]


def embedded_template_cases() -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    for node in load_blueprint_nodes():
        class_name = node.get("className")
        if class_name not in EXPECTED_RECIPES:
            continue
        testing = node.get("testing")
        if not isinstance(testing, dict):
            continue
        query_surface = testing.get("querySurface")
        cases.append(
            {
                "id": class_name,
                "className": class_name,
                "displayName": node.get("displayName"),
                "family": node.get("family"),
                "profile": testing.get("profile"),
                "recipe": testing.get("recipe"),
                "fallback": query_surface.get("fallback") if isinstance(query_surface, dict) else None,
                "querySurfaceKind": query_surface.get("kind") if isinstance(query_surface, dict) else None,
                "reason": testing.get("reason"),
            }
        )
    return cases


def list_cases_payload() -> dict[str, Any]:
    cases = embedded_template_cases()
    return {
        "version": "1",
        "graphType": "blueprint",
        "suite": "embedded_template",
        "summary": {
            "totalCases": len(cases),
            "families": sorted(
                {
                    case["family"]
                    for case in cases
                    if isinstance(case.get("family"), str) and case["family"]
                }
            ),
            "querySurfaceKinds": sorted(
                {
                    case["querySurfaceKind"]
                    for case in cases
                    if isinstance(case.get("querySurfaceKind"), str) and case["querySurfaceKind"]
                }
            ),
            "recipes": sorted(
                {
                    case["recipe"]
                    for case in cases
                    if isinstance(case.get("recipe"), str) and case["recipe"]
                }
            ),
        },
        "cases": cases,
    }


def execute_suite() -> dict[str, Any]:
    cases = embedded_template_cases()
    results: list[dict[str, Any]] = []
    for case in cases:
        status = "pass"
        failure_kind = None
        reason = None
        expected_recipe = EXPECTED_RECIPES.get(case["className"])
        if case.get("profile") != "context_recipe_required":
            status = "fail"
            failure_kind = "embedded_template_profile_mismatch"
            reason = "embedded-template nodes must use context_recipe_required coverage"
        elif case.get("recipe") != expected_recipe:
            status = "fail"
            failure_kind = "embedded_template_recipe_mismatch"
            reason = f"embedded-template node must use recipe {expected_recipe}"
        elif case.get("querySurfaceKind") != "residual_gap":
            status = "fail"
            failure_kind = "embedded_template_surface_mismatch"
            reason = "embedded-template nodes must currently declare residual_gap querySurface"
        elif case.get("fallback") != "execute":
            status = "fail"
            failure_kind = "embedded_template_fallback_mismatch"
            reason = "embedded-template residual gaps must declare execute fallback"
        elif not isinstance(case.get("reason"), str) or not case["reason"]:
            status = "fail"
            failure_kind = "embedded_template_reason_missing"
            reason = "embedded-template nodes must explain why richer query truth is still missing"

        result = {
            "className": case["className"],
            "displayName": case.get("displayName"),
            "family": case.get("family"),
            "status": status,
            "details": {
                "profile": case.get("profile"),
                "recipe": case.get("recipe"),
                "querySurfaceKind": case.get("querySurfaceKind"),
                "fallback": case.get("fallback"),
                "reason": case.get("reason"),
            },
        }
        if failure_kind:
            result["failureKind"] = failure_kind
        if reason:
            result["reason"] = reason
        results.append(result)

    passing = sum(1 for result in results if result["status"] == "pass")
    failing = len(results) - passing
    return {
        "version": "1",
        "graphType": "blueprint",
        "suite": "embedded_template",
        "summary": {
            "totalCases": len(cases),
            "passingCases": passing,
            "failingCases": failing,
            "families": sorted(
                {
                    case["family"]
                    for case in cases
                    if isinstance(case.get("family"), str) and case["family"]
                }
            ),
            "querySurfaceKinds": sorted(
                {
                    case["querySurfaceKind"]
                    for case in cases
                    if isinstance(case.get("querySurfaceKind"), str) and case["querySurfaceKind"]
                }
            ),
            "recipes": sorted(
                {
                    case["recipe"]
                    for case in cases
                    if isinstance(case.get("recipe"), str) and case["recipe"]
                }
            ),
        },
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit Blueprint embedded-template node classification and residual-gap coverage.")
    parser.add_argument("--output")
    parser.add_argument("--list-cases", action="store_true")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    report = execute_suite()
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["summary"]["failingCases"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
