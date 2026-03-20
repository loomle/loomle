#!/usr/bin/env python3
import argparse
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPORT_VERSION = "1"


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected object root in {path}")
    return value


def build_report(run_report: dict[str, Any], source_path: str) -> dict[str, Any]:
    results = run_report.get("results")
    if not isinstance(results, list):
        raise ValueError("run report missing results[]")

    total_cases = 0
    roundtrip_cases = 0
    query_total = 0
    query_pin_found = 0
    query_surfaced = 0
    query_matched = 0
    family_buckets: dict[str, Counter[str]] = defaultdict(Counter)
    weak_cases: list[dict[str, Any]] = []

    for result in results:
        if not isinstance(result, dict):
            continue
        if result.get("status") != "pass":
            continue
        total_cases += 1
        details = result.get("details")
        if not isinstance(details, dict):
            continue
        query_audit = details.get("queryAudit")
        if not isinstance(query_audit, dict):
            continue

        roundtrip_cases += 1
        family = str(result.get("family") or "unknown")
        counts = query_audit.get("counts")
        if not isinstance(counts, dict):
            continue

        total_fields = int(counts.get("totalFields", 0) or 0)
        pin_found_fields = int(counts.get("pinFoundFields", 0) or 0)
        surfaced_fields = int(counts.get("surfacedFields", 0) or 0)
        matched_fields = int(counts.get("matchedFields", 0) or 0)

        query_total += total_fields
        query_pin_found += pin_found_fields
        query_surfaced += surfaced_fields
        query_matched += matched_fields

        family_buckets[family]["roundtripCases"] += 1
        family_buckets[family]["totalFields"] += total_fields
        family_buckets[family]["pinFoundFields"] += pin_found_fields
        family_buckets[family]["surfacedFields"] += surfaced_fields
        family_buckets[family]["matchedFields"] += matched_fields

        if surfaced_fields < total_fields or matched_fields < total_fields:
            weak_cases.append(
                {
                    "className": result.get("className"),
                    "displayName": result.get("displayName"),
                    "family": family,
                    "queryAudit": counts,
                }
            )

    family_summary = []
    for family in sorted(family_buckets):
        counter = family_buckets[family]
        family_summary.append(
            {
                "family": family,
                "roundtripCases": counter["roundtripCases"],
                "totalFields": counter["totalFields"],
                "pinFoundFields": counter["pinFoundFields"],
                "surfacedFields": counter["surfacedFields"],
                "matchedFields": counter["matchedFields"],
            }
        )

    return {
        "version": REPORT_VERSION,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "sourceRunReport": {
            "path": source_path,
        },
        "summary": {
            "passedCases": total_cases,
            "roundtripCases": roundtrip_cases,
            "queryAudit": {
                "totalFields": query_total,
                "pinFoundFields": query_pin_found,
                "surfacedFields": query_surfaced,
                "matchedFields": query_matched,
            },
        },
        "familySummary": family_summary,
        "weakCases": weak_cases,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate query-truth audit reports from graph test run results.")
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
