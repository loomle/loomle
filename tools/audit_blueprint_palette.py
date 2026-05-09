#!/usr/bin/env python3
"""Audit live UE Blueprint palette coverage through Loomle.

This is an offline developer audit script, not an MCP tool. It measures what
`blueprint.palette` returns for real Blueprint graph contexts so the docs can
track UE-backed coverage without maintaining a curated node table.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
TESTS_E2E_DIR = REPO_ROOT / "tests" / "e2e"
if str(TESTS_E2E_DIR) not in sys.path:
    sys.path.insert(0, str(TESTS_E2E_DIR))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_execute_exec_with_retry,
    call_tool,
    fail,
    is_tool_error_payload,
    make_temp_asset_path,
    parse_execute_json,
    parse_tool_payload,
    resolve_default_loomle_binary,
    resolve_project_root,
)


REPRESENTATIVE_QUERIES = {
    "Branch": lambda entry: entry.get("label") == "Branch",
    "Gate": lambda entry: entry.get("label") == "Gate",
    "Self": lambda entry: entry.get("nodeClass") == "/Script/BlueprintGraph.K2Node_Self",
    "Cast To Actor": lambda entry: entry.get("label") == "Cast To Actor",
    "Print String": lambda entry: entry.get("label") == "Print String",
    "Delay": lambda entry: entry.get("label") == "Delay",
    "Sequence": lambda entry: entry.get("label") == "Sequence",
    "Equal Enum": lambda entry: "Equal" in (entry.get("label") or "")
    and "Enum"
    in (
        (entry.get("tooltip") or "")
        + " "
        + (entry.get("category") or "")
        + " "
        + " ".join(entry.get("keywords") or [])
    ),
}


GROUP_ORDER = [
    "Function call",
    "Variable get/set",
    "Event",
    "Delegate",
    "Component",
    "Macro",
    "Flow control",
    "Operator",
    "Cast",
    "Struct",
    "Enum",
    "Input",
    "Subsystem",
    "Other node spawner",
    "Schema action",
]


def create_actor_blueprint(client: McpStdioClient, req_id: int, asset_path: str) -> None:
    payload = call_execute_exec_with_retry(
        client,
        req_id,
        (
            "import unreal, json\n"
            f"asset = {asset_path!r}\n"
            "pkg_path, asset_name = asset.rsplit('/', 1)\n"
            "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
            "factory = unreal.BlueprintFactory()\n"
            "factory.set_editor_property('ParentClass', unreal.Actor)\n"
            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
            "    unreal.EditorAssetLibrary.delete_asset(asset)\n"
            "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
            "unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)\n"
            "print(json.dumps({'created': bp is not None, 'exists': unreal.EditorAssetLibrary.does_asset_exist(asset)}))\n"
        ),
    )
    parsed = parse_execute_json(payload)
    if parsed.get("created") is not True or parsed.get("exists") is not True:
        fail(f"failed to create audit Blueprint asset: {parsed}")


def ensure_member_graphs(client: McpStdioClient, req_id_base: int, asset_path: str) -> None:
    operations = [
        (
            "function",
            {
                "assetPath": asset_path,
                "memberKind": "function",
                "operation": "create",
                "args": {
                    "functionName": "AuditFunction",
                    "inputs": [{"name": "bInput", "type": {"category": "bool"}}],
                    "outputs": [{"name": "Value", "type": {"category": "int"}}],
                    "category": "Audit",
                },
            },
        ),
        (
            "macro",
            {
                "assetPath": asset_path,
                "memberKind": "macro",
                "operation": "create",
                "args": {
                    "macroName": "AuditMacro",
                    "inputs": [{"name": "bInput", "type": {"category": "bool"}}],
                    "outputs": [{"name": "bOutput", "type": {"category": "bool"}}],
                    "category": "Audit",
                },
            },
        ),
    ]
    for index, (label, request) in enumerate(operations):
        payload = call_tool(client, req_id_base + index, "blueprint.member.edit", request)
        if payload.get("applied") is not True:
            fail(f"failed to create audit {label} graph: {payload}")


def list_graph_names(client: McpStdioClient, req_id: int, asset_path: str) -> list[str]:
    payload = call_tool(client, req_id, "blueprint.graph.list", {"assetPath": asset_path})
    graphs = payload.get("graphs")
    if not isinstance(graphs, list):
        fail(f"blueprint.graph.list missing graphs[]: {payload}")

    names: list[str] = []
    for graph in graphs:
        if not isinstance(graph, dict):
            continue
        name = graph.get("graphName")
        if isinstance(name, str) and name:
            names.append(name)
    return names


def collect_palette(
    client: McpStdioClient,
    req_id_base: int,
    *,
    asset_path: str,
    graph_name: str,
    context_sensitive: bool,
    page_size: int,
) -> tuple[list[dict[str, Any]], int]:
    entries: list[dict[str, Any]] = []
    offset = 0
    total: int | None = None

    while True:
        payload = call_tool(
            client,
            req_id_base + offset,
            "blueprint.palette",
            {
                "assetPath": asset_path,
                "graphName": graph_name,
                "contextSensitive": context_sensitive,
                "limit": page_size,
                "offset": offset,
            },
        )
        batch = payload.get("entries")
        if not isinstance(batch, list):
            fail(f"blueprint.palette missing entries[]: {payload}")
        if total is None:
            raw_total = payload.get("total")
            if not isinstance(raw_total, int):
                fail(f"blueprint.palette missing integer total: {payload}")
            total = raw_total

        entries.extend(entry for entry in batch if isinstance(entry, dict))
        offset += len(batch)
        if not batch or offset >= total:
            break

    return entries, int(total or 0)


def inspect_graph_state(client: McpStdioClient, req_id: int, asset_path: str, graph_name: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        req_id,
        "blueprint.graph.inspect",
        {
            "assetPath": asset_path,
            "graphName": graph_name,
            "includeConnections": False,
            "limit": 10000,
        },
    )
    nodes = payload.get("semanticSnapshot", {}).get("nodes", [])
    if not isinstance(nodes, list):
        fail(f"blueprint.graph.inspect missing semanticSnapshot.nodes[]: {payload}")
    return {
        "revision": payload.get("revision"),
        "nodeCount": len(nodes),
    }


def first_op_result(payload: dict[str, Any]) -> dict[str, Any]:
    results = payload.get("opResults")
    if not isinstance(results, list) or not results or not isinstance(results[0], dict):
        return {}
    return results[0]


def safe_call_tool(client: McpStdioClient, req_id: int, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    return parse_tool_payload(response, f"tools/call.{name}")


def classify_entry_group(entry: dict[str, Any]) -> str:
    action_type = str(entry.get("actionType") or "")
    node_class = str(entry.get("nodeClass") or "")
    label = str(entry.get("label") or "")
    category = str(entry.get("category") or "")
    haystack = f"{node_class} {label} {category}"

    if action_type == "schemaAction":
        return "Schema action"
    if "K2Node_CallFunction" in node_class or "K2Node_CallArrayFunction" in node_class or "K2Node_AsyncAction" in node_class:
        return "Function call"
    if "K2Node_VariableGet" in node_class or "K2Node_VariableSet" in node_class:
        return "Variable get/set"
    if "Delegate" in node_class:
        return "Delegate"
    if "K2Node_Event" in node_class or "K2Node_CustomEvent" in node_class:
        return "Event"
    if "K2Node_AddComponent" in node_class:
        return "Component"
    if "K2Node_MacroInstance" in node_class:
        return "Flow control" if label in {"Gate", "Do Once", "Do N", "Flip Flop"} else "Macro"
    if any(
        token in node_class
        for token in ("K2Node_IfThenElse", "K2Node_ExecutionSequence", "K2Node_Switch", "K2Node_ForLoop", "K2Node_DoOnce")
    ):
        return "Flow control"
    if any(token in node_class for token in ("Operator", "Equality", "NotEqual", "Boolean")):
        return "Operator"
    if "DynamicCast" in node_class or "Cast" in label:
        return "Cast"
    if "Struct" in node_class:
        return "Struct"
    if "Enum" in node_class or "Enum" in haystack:
        return "Enum"
    if "Input" in node_class or "Input" in category:
        return "Input"
    if "Subsystem" in node_class or "Subsystem" in label:
        return "Subsystem"
    if action_type == "nodeSpawner":
        return "Other node spawner"
    return action_type or "Unknown"


def select_representative_samples(entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for intent, predicate in REPRESENTATIVE_QUERIES.items():
        entry = next((candidate for candidate in entries if predicate(candidate)), None)
        samples.append(
            {
                "intent": intent,
                "sampleGroup": "Representative",
                "entry": entry,
            }
        )
    return samples


def select_grouped_samples(entries: list[dict[str, Any]], max_samples_per_group: int) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = {name: [] for name in GROUP_ORDER}
    seen: set[tuple[str, str, str]] = set()

    for entry in entries:
        if not isinstance(entry.get("id"), str):
            continue
        group = classify_entry_group(entry)
        grouped.setdefault(group, [])
        key = (
            group,
            str(entry.get("label") or ""),
            str(entry.get("nodeClass") or ""),
        )
        if key in seen:
            continue
        seen.add(key)
        if len(grouped[group]) < max_samples_per_group:
            grouped[group].append(entry)

    samples: list[dict[str, Any]] = []
    ordered_groups = GROUP_ORDER + sorted(group for group in grouped if group not in GROUP_ORDER)
    for group in ordered_groups:
        for index, entry in enumerate(grouped.get(group, []), start=1):
            label = entry.get("label") or "<unnamed>"
            samples.append(
                {
                    "intent": f"{group} #{index}: {label}",
                    "sampleGroup": group,
                    "entry": entry,
                }
            )
    return samples


def execute_palette_sample(
    client: McpStdioClient,
    req_id_base: int,
    *,
    asset_path: str,
    graph_name: str,
    sample_spec: dict[str, Any],
    index: int,
) -> dict[str, Any]:
    entry = sample_spec.get("entry")
    sample: dict[str, Any] = {
        "intent": sample_spec["intent"],
        "sampleGroup": sample_spec.get("sampleGroup") or "Unknown",
        "listed": isinstance(entry, dict),
    }
    if not isinstance(entry, dict):
        return sample

    entry_id = entry.get("id")
    if not isinstance(entry_id, str) or not entry_id:
        sample.update({"dryRunOk": False, "applyOk": False, "error": "palette entry missing id"})
        return sample

    position = {"x": 400 + (index % 4) * 320, "y": (index // 4) * 260}
    command = {
        "kind": "addFromPalette",
        "entry": entry,
        "position": position,
        "alias": f"sample_{index}",
    }
    before_dry = inspect_graph_state(client, req_id_base + index * 10, asset_path, graph_name)
    dry_payload = safe_call_tool(
        client,
        req_id_base + index * 10 + 1,
        "blueprint.graph.edit",
        {
            "assetPath": asset_path,
            "graphName": graph_name,
            "dryRun": True,
            "returnDiff": True,
            "commands": [command],
        },
    )
    after_dry = inspect_graph_state(client, req_id_base + index * 10 + 2, asset_path, graph_name)
    dry_first = first_op_result(dry_payload)
    dry_run_ok = dry_first.get("ok") is True
    dry_run_preserved_graph = before_dry == after_dry
    dry_run_changed = dry_first.get("changed")
    if not dry_run_preserved_graph:
        fail(
            "blueprint.graph.edit dryRun mutated graph state: "
            f"asset={asset_path} graph={graph_name} intent={sample['intent']} before={before_dry} after={after_dry} payload={dry_payload}"
        )
    if dry_run_ok and dry_run_changed is not False:
        fail(
            "blueprint.graph.edit dryRun reported a successful op with changed!=false: "
            f"asset={asset_path} graph={graph_name} intent={sample['intent']} payload={dry_payload}"
        )

    sample.update(
        {
            "label": entry.get("label"),
            "nodeClass": entry.get("nodeClass"),
            "actionType": entry.get("actionType"),
            "category": entry.get("category"),
            "dryRunOk": dry_run_ok,
            "dryRunChanged": dry_run_changed,
            "dryRunApplied": dry_payload.get("applied"),
            "dryRunPreservedGraph": dry_run_preserved_graph,
            "dryRunHasDiff": isinstance(dry_payload.get("diff"), dict)
            or isinstance(dry_first.get("diff"), dict),
        }
    )
    if not dry_run_ok or is_tool_error_payload(dry_payload):
        sample["dryRunErrorCode"] = dry_first.get("errorCode") or dry_payload.get("errorCode") or dry_payload.get("code")
        sample["dryRunErrorMessage"] = dry_first.get("errorMessage") or dry_payload.get("message")
        sample["applySkipped"] = True
        sample["applySkipReason"] = "dryRun failed"
        return sample
    if entry.get("executable") is False:
        sample["applySkipped"] = True
        sample["applySkipReason"] = "palette entry is not executable"
        return sample

    before_apply = inspect_graph_state(client, req_id_base + index * 10 + 3, asset_path, graph_name)
    apply_payload = safe_call_tool(
        client,
        req_id_base + index * 10 + 4,
        "blueprint.graph.edit",
        {
            "assetPath": asset_path,
            "graphName": graph_name,
            "returnDiff": True,
            "commands": [command],
        },
    )
    after_apply = inspect_graph_state(client, req_id_base + index * 10 + 5, asset_path, graph_name)
    apply_first = first_op_result(apply_payload)
    apply_ok = apply_first.get("ok") is True
    node_id = apply_first.get("nodeId")

    sample.update(
        {
            "applyOk": apply_ok,
            "applyNodeId": node_id if isinstance(node_id, str) else None,
            "applyChangedNodeCount": after_apply.get("nodeCount") != before_apply.get("nodeCount"),
            "applyChangedRevision": after_apply.get("revision") != before_apply.get("revision"),
        }
    )
    if not apply_ok or is_tool_error_payload(apply_payload):
        sample["applyErrorCode"] = apply_first.get("errorCode") or apply_payload.get("errorCode") or apply_payload.get("code")
        sample["applyErrorMessage"] = apply_first.get("errorMessage") or apply_payload.get("message")

    return sample


def run_palette_execution_samples(
    client: McpStdioClient,
    req_id_base: int,
    *,
    asset_path: str,
    graph_name: str,
    entries: list[dict[str, Any]],
    sample_mode: str,
    max_samples_per_group: int,
) -> list[dict[str, Any]]:
    if sample_mode == "representative":
        specs = select_representative_samples(entries)
    elif sample_mode == "groups":
        specs = select_grouped_samples(entries, max_samples_per_group)
    else:
        fail(f"unsupported execution sample mode: {sample_mode}")

    return [
        execute_palette_sample(
            client,
            req_id_base,
            asset_path=asset_path,
            graph_name=graph_name,
            sample_spec=spec,
            index=index,
        )
        for index, spec in enumerate(specs)
    ]


def summarize_entries(entries: list[dict[str, Any]], total: int) -> dict[str, Any]:
    action_types = Counter(str(entry.get("actionType") or "<missing>") for entry in entries)
    node_classes = Counter(str(entry.get("nodeClass") or "<missing>") for entry in entries)
    categories = Counter(top_level_category(str(entry.get("category") or "<missing>")) for entry in entries)
    labels = [entry.get("label") for entry in entries if isinstance(entry.get("label"), str)]

    samples: dict[str, Any] = {}
    for name, predicate in REPRESENTATIVE_QUERIES.items():
        matches = [entry for entry in entries if predicate(entry)]
        first = matches[0] if matches else None
        samples[name] = {
            "present": first is not None,
            "matches": len(matches),
            "label": first.get("label") if first else None,
            "nodeClass": first.get("nodeClass") if first else None,
            "category": first.get("category") if first else None,
            "actionType": first.get("actionType") if first else None,
        }

    return {
        "reportedTotal": total,
        "fetched": len(entries),
        "uniqueLabels": len(set(labels)),
        "uniqueNodeClasses": len([key for key in node_classes if key != "<missing>"]),
        "actionTypes": action_types.most_common(),
        "topNodeClasses": node_classes.most_common(25),
        "topCategories": categories.most_common(25),
        "representativeEntries": samples,
    }


def top_level_category(category: str) -> str:
    if "|" not in category:
        return category.strip() or "<missing>"
    parts = [part.strip() for part in category.split("|") if part.strip()]
    return parts[0] if parts else "<missing>"


def write_markdown_report(report: dict[str, Any], path: Path) -> None:
    def status(value: Any, *, attempted: bool) -> str:
        if not attempted:
            return "n/a"
        return "yes" if value else "no"

    lines = [
        "# Blueprint Palette Audit",
        "",
        f"- Generated at: `{report['generatedAt']}`",
        f"- Blueprint: `{report['assetPath']}`",
        f"- Page size: `{report['pageSize']}`",
        "",
        "## Scenarios",
        "",
        "| Scenario | Graph | Context Sensitive | UE Entries | Fetched | Unique Labels | Unique Node Classes |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for scenario in report["scenarios"]:
        summary = scenario["summary"]
        lines.append(
            "| {name} | `{graph}` | `{context}` | {total} | {fetched} | {labels} | {classes} |".format(
                name=scenario["name"],
                graph=scenario["graphName"],
                context=str(scenario["contextSensitive"]).lower(),
                total=summary["reportedTotal"],
                fetched=summary["fetched"],
                labels=summary["uniqueLabels"],
                classes=summary["uniqueNodeClasses"],
            )
        )

    lines.extend(["", "## Representative Entries", ""])
    for scenario in report["scenarios"]:
        lines.extend(
            [
                f"### {scenario['name']} (`contextSensitive={str(scenario['contextSensitive']).lower()}`)",
                "",
                "| Intent | Present | UE Label | Node Class | Category |",
                "| --- | --- | --- | --- | --- |",
            ]
        )
        for intent, entry in scenario["summary"]["representativeEntries"].items():
            lines.append(
                "| {intent} | {present} | `{label}` | `{node_class}` | `{category}` |".format(
                    intent=intent,
                    present="yes" if entry["present"] else "no",
                    label=entry["label"] or "",
                    node_class=entry["nodeClass"] or "",
                    category=entry["category"] or "",
                )
            )
        lines.append("")

    if report.get("sampleExecution"):
        lines.extend(["", "## Execution Samples", ""])
        lines.append("| Scenario | Group | Intent | Listed | Dry Run | Dry Run Preserved Graph | Apply | Node Class |")
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
        for scenario in report["scenarios"]:
            for sample in scenario.get("executionSamples") or []:
                attempted = sample.get("listed") is True
                lines.append(
                    "| {scenario} | {group} | {intent} | {listed} | {dry} | {preserved} | {apply} | `{node_class}` |".format(
                        scenario=f"{scenario['name']} contextSensitive={str(scenario['contextSensitive']).lower()}",
                        group=sample.get("sampleGroup") or "",
                        intent=sample["intent"],
                        listed="yes" if sample.get("listed") else "no",
                        dry=status(sample.get("dryRunOk"), attempted=attempted),
                        preserved=status(sample.get("dryRunPreservedGraph"), attempted=attempted),
                        apply=status(sample.get("applyOk"), attempted=attempted),
                        node_class=sample.get("nodeClass") or "",
                    )
                )
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit live UE Blueprint palette coverage.")
    parser.add_argument("--project-root", default="", help="UE project root. Defaults to tools/dev.project-root.local.json.")
    parser.add_argument("--dev-config", default="", help="Optional project-root config JSON.")
    parser.add_argument("--loomle-bin", default="", help="Path to loomle release binary.")
    parser.add_argument("--asset-path", default="", help="Existing Blueprint asset to audit. If omitted, create a temp Actor Blueprint.")
    parser.add_argument("--asset-prefix", default="/Game/Codex/BP_PaletteAudit", help="Temp Blueprint asset prefix.")
    parser.add_argument("--page-size", type=int, default=500, help="blueprint.palette page size.")
    parser.add_argument("--timeout", type=float, default=120.0, help="Per-request timeout seconds.")
    parser.add_argument("--sample-execution", action="store_true", help="Dry-run and apply representative addFromPalette entries.")
    parser.add_argument(
        "--execution-sample-mode",
        choices=["representative", "groups"],
        default="representative",
        help="Execution sample selector used with --sample-execution.",
    )
    parser.add_argument("--max-samples-per-group", type=int, default=2, help="Maximum entries per group in groups mode.")
    parser.add_argument("--json-out", default="", help="Optional JSON report output path.")
    parser.add_argument("--markdown-out", default="", help="Optional Markdown report output path.")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_bin = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)
    asset_path = args.asset_path or make_temp_asset_path(args.asset_prefix)

    client = McpStdioClient(project_root=project_root, server_binary=loomle_bin, timeout_s=args.timeout)
    try:
        created_temp_asset = not bool(args.asset_path)
        if created_temp_asset:
            create_actor_blueprint(client, 100, asset_path)
            ensure_member_graphs(client, 200, asset_path)

        graph_names = list_graph_names(client, 300, asset_path)
        requested_graphs = ["EventGraph", "ConstructionScript", "UserConstructionScript", "AuditFunction", "AuditMacro"]
        selected_graphs: list[tuple[str, str]] = []
        for graph_name in requested_graphs:
            if graph_name in graph_names:
                selected_graphs.append((graph_name, graph_name))
        if not selected_graphs:
            fail(f"no auditable graphs found in {asset_path}: {graph_names}")

        scenarios: list[dict[str, Any]] = []
        scenario_entries: list[list[dict[str, Any]]] = []
        req_base = 1000
        for graph_name, scenario_name in selected_graphs:
            for context_sensitive in (True, False):
                entries, total = collect_palette(
                    client,
                    req_base,
                    asset_path=asset_path,
                    graph_name=graph_name,
                    context_sensitive=context_sensitive,
                    page_size=args.page_size,
                )
                req_base += 100000
                scenario: dict[str, Any] = {
                    "name": scenario_name,
                    "assetPath": asset_path,
                    "graphName": graph_name,
                    "contextSensitive": context_sensitive,
                    "summary": summarize_entries(entries, total),
                }
                scenarios.append(scenario)
                scenario_entries.append(entries)

        if args.sample_execution:
            for index, scenario in enumerate(scenarios):
                scenario["executionSamples"] = run_palette_execution_samples(
                    client,
                    1000000 + index * 1000,
                    asset_path=asset_path,
                    graph_name=scenario["graphName"],
                    entries=scenario_entries[index],
                    sample_mode=args.execution_sample_mode,
                    max_samples_per_group=args.max_samples_per_group,
                )

        report = {
            "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "assetPath": asset_path,
            "createdTempAsset": created_temp_asset,
            "pageSize": args.page_size,
            "sampleExecution": args.sample_execution,
            "executionSampleMode": args.execution_sample_mode if args.sample_execution else None,
            "maxSamplesPerGroup": args.max_samples_per_group if args.sample_execution else None,
            "scenarios": scenarios,
        }

        text = json.dumps(report, indent=2, sort_keys=True)
        print(text)
        if args.json_out:
            output = Path(args.json_out).resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(text + "\n", encoding="utf-8")
        if args.markdown_out:
            write_markdown_report(report, Path(args.markdown_out).resolve())
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
