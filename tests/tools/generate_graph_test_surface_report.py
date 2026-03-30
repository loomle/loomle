#!/usr/bin/env python3
import argparse
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPORT_VERSION = "1"
SURFACES = ("mutate", "queryStructure", "queryTruth", "engineTruth", "verify", "diagnostics")


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected object root in {path}")
    return value


def result_families(result: dict[str, Any]) -> list[str]:
    if isinstance(result.get("family"), str) and result["family"]:
        return [result["family"]]
    families = result.get("families")
    if isinstance(families, list):
        return [family for family in families if isinstance(family, str) and family]
    return ["unknown"]


def build_report(run_report: dict[str, Any], source_path: str) -> dict[str, Any]:
    results = run_report.get("results")
    if not isinstance(results, list):
        raise ValueError("run report missing results[]")

    total_cases = 0
    status_counter = Counter()
    reason_counter = Counter()
    family_buckets: dict[str, dict[str, Any]] = defaultdict(
        lambda: {"status": Counter(), "surfaces": {surface: Counter() for surface in SURFACES}}
    )
    surface_totals = {surface: Counter() for surface in SURFACES}
    weak_cases: list[dict[str, Any]] = []

    for result in results:
        if not isinstance(result, dict):
            continue
        total_cases += 1
        status = str(result.get("status") or "unknown")
        status_counter[status] += 1
        reason = result.get("reason")
        if isinstance(reason, str) and reason:
            reason_counter[reason] += 1

        details = result.get("details")
        surface_matrix = details.get("surfaceMatrix") if isinstance(details, dict) else None
        failed_surfaces: list[str] = []
        if isinstance(surface_matrix, dict):
            for surface in SURFACES:
                value = surface_matrix.get(surface)
                if isinstance(value, str) and value:
                    surface_totals[surface][value] += 1
                    if value == "fail":
                        failed_surfaces.append(surface)

            if failed_surfaces:
                weak_cases.append(
                    {
                        "id": result.get("className") or result.get("caseId"),
                        "displayName": result.get("displayName"),
                        "families": result_families(result),
                        "status": status,
                        "reason": result.get("reason"),
                        "failureKind": result.get("failureKind"),
                        "failedSurfaces": failed_surfaces,
                        "surfaceMatrix": surface_matrix,
                    }
                )

            for family in result_families(result):
                bucket = family_buckets[family]
                bucket["status"][status] += 1
                for surface in SURFACES:
                    value = surface_matrix.get(surface)
                    if isinstance(value, str) and value:
                        bucket["surfaces"][surface][value] += 1
        else:
            for family in result_families(result):
                family_buckets[family]["status"][status] += 1

    family_summary = []
    for family in sorted(family_buckets):
        bucket = family_buckets[family]
        family_summary.append(
            {
                "family": family,
                "status": dict(sorted(bucket["status"].items())),
                "surfaceMatrix": {
                    surface: dict(sorted(counter.items()))
                    for surface, counter in bucket["surfaces"].items()
                    if counter
                },
            }
        )

    return {
        "version": REPORT_VERSION,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "graphType": run_report.get("graphType"),
        "suite": run_report.get("suite"),
        "sourceRunReport": {"path": source_path},
        "summary": {
            "totalCases": total_cases,
            "status": dict(sorted(status_counter.items())),
            "surfaceMatrix": {
                surface: dict(sorted(counter.items()))
                for surface, counter in surface_totals.items()
                if counter
            },
            "failureReasons": dict(sorted(reason_counter.items())),
        },
        "familySummary": family_summary,
        "weakCases": weak_cases,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate multi-surface truth reports from graph test run results.")
    parser.add_argument("--run-report", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    run_report = load_json(args.run_report)
    report = build_report(run_report, str(args.run_report))
    text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
