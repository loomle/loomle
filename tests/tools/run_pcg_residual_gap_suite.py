#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
PCG_NODE_DATABASE = REPO_ROOT / "workspace" / "Loomle" / "pcg" / "catalogs" / "node-database.json"


def load_pcg_nodes() -> list[dict[str, Any]]:
    payload = json.loads(PCG_NODE_DATABASE.read_text(encoding="utf-8"))
    nodes = payload.get("nodes")
    if not isinstance(nodes, list):
        raise ValueError(f"pcg node database missing nodes[]: {PCG_NODE_DATABASE}")
    return [node for node in nodes if isinstance(node, dict)]


def residual_gap_cases() -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    for node in load_pcg_nodes():
        testing = node.get("testing")
        if not isinstance(testing, dict):
            continue
        query_surface = testing.get("querySurface")
        if not isinstance(query_surface, dict) or query_surface.get("kind") != "residual_gap":
            continue
        cases.append(
            {
                "id": node.get("className"),
                "className": node.get("className"),
                "displayName": node.get("displayName"),
                "family": node.get("family"),
                "fallback": query_surface.get("fallback"),
                "reason": testing.get("reason"),
            }
        )
    return cases


def list_cases_payload() -> dict[str, Any]:
    cases = residual_gap_cases()
    return {
        "version": "1",
        "graphType": "pcg",
        "suite": "residual_gap",
        "summary": {
            "totalCases": len(cases),
            "documentedCases": len(cases),
            "missingFallback": 0,
            "missingReason": 0,
            "fallbackKinds": sorted(
                {
                    case["fallback"]
                    for case in cases
                    if isinstance(case.get("fallback"), str) and case["fallback"]
                }
            ),
        },
        "cases": cases,
    }


def execute_suite() -> dict[str, Any]:
    cases = residual_gap_cases()
    results: list[dict[str, Any]] = []
    missing_fallback = 0
    missing_reason = 0
    for case in cases:
        status = "pass"
        failure_kind = None
        reason = None
        if not isinstance(case.get("fallback"), str) or not case["fallback"]:
            status = "fail"
            failure_kind = "residual_gap_missing_fallback"
            reason = "residual_gap nodes must declare a fallback path"
            missing_fallback += 1
        elif not isinstance(case.get("reason"), str) or not case["reason"]:
            status = "fail"
            failure_kind = "residual_gap_missing_reason"
            reason = "residual_gap nodes must explain why fallback is still required"
            missing_reason += 1
        result = {
            "className": case.get("className"),
            "displayName": case.get("displayName"),
            "family": case.get("family"),
            "status": status,
            "details": {
                "fallback": case.get("fallback"),
                "reason": case.get("reason"),
            },
        }
        if failure_kind:
            result["failureKind"] = failure_kind
        if reason:
            result["reason"] = reason
        results.append(result)

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "residual_gap",
        "summary": {
            "totalCases": len(cases),
            "documentedCases": len(cases) - missing_fallback - missing_reason,
            "missingFallback": missing_fallback,
            "missingReason": missing_reason,
            "fallbackKinds": sorted(
                {
                    case["fallback"]
                    for case in cases
                    if isinstance(case.get("fallback"), str) and case["fallback"]
                }
            ),
        },
        "results": results,
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit PCG residual-gap metadata and fallback declarations.")
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
    return 0 if report["summary"]["missingFallback"] == 0 and report["summary"]["missingReason"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
