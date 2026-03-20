#!/usr/bin/env python3
import argparse
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from generate_graph_test_plan import REPO_ROOT, WORKSPACE_ROOT, build_plan, load_json

REPORT_VERSION = "1"


def load_plan(graph_type: str, plan_path: Path | None) -> tuple[dict[str, Any], str]:
    if plan_path:
        return load_json(plan_path), str(plan_path)
    return build_plan(graph_type, WORKSPACE_ROOT), "<generated>"


def coverage_dimensions_for_entry(entry: dict[str, Any]) -> list[str]:
    dims = ["inventory"]
    status = entry.get("status")
    profile = entry.get("profile")
    mode = entry.get("mode")

    if status == "workflow_only":
        dims.append("workflow")
        return dims

    if status != "ready":
        return dims

    if mode == "recipe_case":
        dims.append("recipe_context")

    if profile in {"construct_only", "construct_and_query", "read_write_roundtrip", "dynamic_pin_probe", "context_recipe_required"}:
        dims.append("construct")
    if profile in {"construct_and_query", "dynamic_pin_probe"}:
        dims.append("query_structure")
    if profile == "read_write_roundtrip":
        dims.append("engine_truth")
    if profile == "dynamic_pin_probe":
        dims.append("dynamic_shape")

    return dims


def summarize_entries(entries: list[dict[str, Any]]) -> dict[str, Any]:
    dimension_counts: Counter[str] = Counter()
    blocked_reasons: Counter[str] = Counter()
    profile_status_counts: dict[str, Counter[str]] = defaultdict(Counter)
    family_summary: dict[str, dict[str, Any]] = {}

    ready_nodes = 0
    blocked_nodes = 0
    workflow_only_nodes = 0
    inventory_only_nodes = 0

    for entry in entries:
        status = entry.get("status")
        family = entry.get("family") or "unknown"
        profile = entry.get("profile") or "unknown"
        reason = entry.get("reason")
        dims = coverage_dimensions_for_entry(entry)

        for dim in dims:
            dimension_counts[dim] += 1

        profile_status_counts[str(profile)][str(status)] += 1

        family_bucket = family_summary.setdefault(
            str(family),
            {
                "family": str(family),
                "totalNodes": 0,
                "readyNodes": 0,
                "blockedNodes": 0,
                "workflowOnlyNodes": 0,
                "inventoryOnlyNodes": 0,
                "coverageDimensions": Counter(),
            },
        )
        family_bucket["totalNodes"] += 1
        if status == "ready":
            family_bucket["readyNodes"] += 1
            ready_nodes += 1
        elif status == "blocked":
            family_bucket["blockedNodes"] += 1
            blocked_nodes += 1
            if isinstance(reason, str) and reason:
                blocked_reasons[reason] += 1
        elif status == "workflow_only":
            family_bucket["workflowOnlyNodes"] += 1
            workflow_only_nodes += 1
        elif status == "inventory_only":
            family_bucket["inventoryOnlyNodes"] += 1
            inventory_only_nodes += 1

        for dim in dims:
            family_bucket["coverageDimensions"][dim] += 1

    normalized_family_summary: list[dict[str, Any]] = []
    for family in sorted(family_summary):
        row = dict(family_summary[family])
        row["coverageDimensions"] = dict(sorted(row["coverageDimensions"].items()))
        normalized_family_summary.append(row)

    normalized_profile_status_counts = {
        profile: dict(sorted(counter.items()))
        for profile, counter in sorted(profile_status_counts.items())
    }

    return {
        "summary": {
            "totalNodes": len(entries),
            "readyNodes": ready_nodes,
            "blockedNodes": blocked_nodes,
            "workflowOnlyNodes": workflow_only_nodes,
            "inventoryOnlyNodes": inventory_only_nodes,
            "coverageDimensions": dict(sorted(dimension_counts.items())),
        },
        "blockedReasons": dict(sorted(blocked_reasons.items())),
        "profileStatusCounts": normalized_profile_status_counts,
        "familySummary": normalized_family_summary,
    }


def build_report(graph_type: str, plan_path: Path | None) -> dict[str, Any]:
    plan, resolved_plan_path = load_plan(graph_type, plan_path)
    entries = plan.get("entries")
    if not isinstance(entries, list):
        raise ValueError("plan missing entries[]")
    summary = summarize_entries([entry for entry in entries if isinstance(entry, dict)])
    return {
        "version": REPORT_VERSION,
        "graphType": graph_type,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "sourcePlan": {
            "path": resolved_plan_path,
        },
        **summary,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate graph test coverage reports from JSON plans.")
    parser.add_argument("--graph-type", choices=["pcg"], required=True)
    parser.add_argument("--plan-path", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    report = build_report(args.graph_type, args.plan_path)
    text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
