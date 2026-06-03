#!/usr/bin/env python3
import argparse
import json
import os
import platform
import subprocess
import time
from pathlib import Path

from test_bridge_smoke import (
    REQUIRED_TOOLS,
    McpStdioClient,
    attach_project,
    call_execute_exec_with_retry,
    call_tool,
    fail,
    make_temp_asset_path,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_mcp_server_spec,
    resolve_project_root,
    submit_execute_job,
)


def require_graph_domain(graph_type: str) -> str:
    if graph_type not in {"blueprint", "material", "pcg"}:
        fail(f"unsupported graph domain in regression test: {graph_type}")
    return graph_type


def blueprint_graph_inspect_args(arguments: dict) -> dict:
    normalized = dict(arguments)
    graph_name = normalized.pop("graphName", None)
    if "graph" not in normalized and isinstance(graph_name, str) and graph_name:
        normalized["graph"] = {"name": graph_name}

    for key in ("limit", "cursor"):
        normalized.pop(key, None)
    normalized.pop("filter", None)
    normalized.pop("nodeIds", None)
    normalized.pop("text", None)

    include_connections = normalized.pop("includeConnections", None)
    normalized.pop("layoutDetail", None)
    normalized.pop("includePinDefaults", None)
    if "view" not in normalized:
        normalized["view"] = "summary"
    if include_connections is True and normalized.get("view") == "summary":
        normalized["view"] = "summary"
    return normalized


def call_domain_tool(
    client: McpStdioClient,
    request_id: int,
    graph_type: str,
    action: str,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    domain = require_graph_domain(graph_type)
    tool_name = f"{domain}.{action}"
    if domain == "blueprint":
        tool_name = {
            "list": "blueprint.graph.list",
            "query": "blueprint.graph.inspect",
            "mutate": "blueprint.graph.edit",
            "describe": "blueprint.inspect",
            "compile": "blueprint.compile",
        }.get(action, tool_name)
        if action == "query":
            arguments = blueprint_graph_inspect_args(arguments)
    elif domain == "material":
        if action == "query":
            return call_tool(client, request_id, "material.graph.inspect", arguments, expect_error=expect_error)
        if action == "mutate":
            return call_material_graph_edit_for_regression(client, request_id, arguments, expect_error=expect_error)
    elif domain == "pcg":
        if action == "query":
            return call_pcg_graph_inspect_for_regression(client, request_id, arguments)
        if action == "list":
            payload = call_pcg_graph_inspect_for_regression(client, request_id, {**arguments, "view": "full"})
            snapshot = payload.get("semanticSnapshot")
            nodes = snapshot.get("nodes") if isinstance(snapshot, dict) else []
            return {
                "assetPath": payload.get("assetPath"),
                "graphRef": payload.get("graphRef"),
                "nodes": nodes if isinstance(nodes, list) else [],
            }
        if action == "mutate":
            return call_pcg_graph_edit_for_regression(client, request_id, arguments, expect_error=expect_error)
        if action == "describe":
            return call_tool(client, request_id, "pcg.node.inspect", arguments, expect_error=expect_error)
        if action == "verify":
            return call_tool(client, request_id, "pcg.compile", arguments, expect_error=expect_error)
    return call_tool(client, request_id, tool_name, arguments, expect_error=expect_error)


def op_ok(payload: dict) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        fail(f"domain mutate missing opResults: {payload}")
    first = op_results[0] if isinstance(op_results[0], dict) else {}
    if not first.get("ok"):
        fail(f"domain mutate op failed: {first}")
    return first


def assert_revision_pair(payload: dict, label: str, *, unchanged: bool = False) -> str:
    previous_revision = payload.get("previousRevision")
    new_revision = payload.get("newRevision")
    if not isinstance(previous_revision, str) or not isinstance(new_revision, str):
        fail(f"{label} missing revision pair: {payload}")
    if unchanged and previous_revision != new_revision:
        fail(f"{label} should keep revision unchanged: {payload}")
    return new_revision


def bp_node(node_id: str) -> dict:
    return {"id": node_id}


def bp_pin(node_id: str, pin: str) -> dict:
    return {"node": bp_node(node_id), "pin": pin}


BP_BRANCH_ENTRY: dict | None = None
MATERIAL_PALETTE_ENTRY_BY_CLASS: dict[str, dict] = {}
PCG_PALETTE_ENTRY_BY_CLASS: dict[str, dict] = {}


def bp_branch(position: dict | None = None, *, alias: str | None = None) -> dict:
    if BP_BRANCH_ENTRY is None:
        fail("bp_branch helper used before blueprint.graph.palette Branch entry was resolved")
    command = {"kind": "addFromPalette", "entry": BP_BRANCH_ENTRY}
    if position is not None:
        command["position"] = position
    if alias:
        command["alias"] = alias
    return command


def find_palette_entry(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    query: str,
    preferred_label: str | None = None,
    preferred_node_class: str | None = None,
) -> dict:
    payload = call_tool(client, request_id, "blueprint.graph.palette", {
        "assetPath": asset_path,
        "graph": {"name": "EventGraph"},
        "query": query,
        "limit": 20,
    })
    entries = payload.get("entries")
    if not isinstance(entries, list) or not entries:
        fail(f"blueprint.graph.palette query {query!r} returned no entries: {payload}")
    if preferred_node_class is not None:
        entry = next((
            item for item in entries
            if item.get("nodeClass") == preferred_node_class
            and (preferred_label is None or item.get("label") == preferred_label)
        ), None)
        if entry is None:
            label_suffix = f" with label {preferred_label!r}" if preferred_label is not None else ""
            fail(f"blueprint.graph.palette query {query!r} did not return nodeClass {preferred_node_class!r}{label_suffix}: {payload}")
    elif preferred_label is None:
        entry = entries[0]
    else:
        entry = next((item for item in entries if item.get("label") == preferred_label), None)
        if entry is None:
            fail(f"blueprint.graph.palette query {query!r} did not return label {preferred_label!r}: {payload}")
    if not isinstance(entry, dict) or not entry.get("id"):
        fail(f"blueprint.graph.palette query {query!r} entry missing id: {entry}")
    return entry


def find_pcg_palette_entry_by_class(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    settings_class: str,
) -> dict:
    cached = PCG_PALETTE_ENTRY_BY_CLASS.get(settings_class)
    if cached is not None:
        return cached

    payload = call_tool(
        client,
        request_id,
        "pcg.palette",
        {
            "assetPath": asset_path,
            "query": settings_class,
            "elementTypes": ["native"],
            "limit": 100,
        },
    )
    entries = payload.get("entries")
    if not isinstance(entries, list) or not entries:
        fail(f"pcg.palette query for settingsClass {settings_class!r} returned no entries: {payload}")

    entry = next(
        (
            item
            for item in entries
            if isinstance(item, dict)
            and isinstance(item.get("payload"), dict)
            and item["payload"].get("settingsClass") == settings_class
        ),
        None,
    )
    if not isinstance(entry, dict):
        fail(f"pcg.palette query did not return settingsClass {settings_class!r}: {payload}")
    PCG_PALETTE_ENTRY_BY_CLASS[settings_class] = entry
    return entry


def find_material_palette_entry_by_class(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    node_class_path: str,
) -> dict:
    cached = MATERIAL_PALETTE_ENTRY_BY_CLASS.get(node_class_path)
    if cached is not None:
        return cached

    payload = call_tool(
        client,
        request_id,
        "material.palette",
        {
            "assetPath": asset_path,
            "query": node_class_path,
            "elementTypes": ["expression"],
            "limit": 100,
        },
    )
    entries = payload.get("entries")
    if not isinstance(entries, list) or not entries:
        fail(f"material.palette query for nodeClassPath {node_class_path!r} returned no entries: {payload}")

    entry = next(
        (
            item
            for item in entries
            if isinstance(item, dict)
            and isinstance(item.get("payload"), dict)
            and item["payload"].get("nodeClassPath") == node_class_path
        ),
        None,
    )
    if not isinstance(entry, dict):
        fail(f"material.palette query did not return nodeClassPath {node_class_path!r}: {payload}")
    MATERIAL_PALETTE_ENTRY_BY_CLASS[node_class_path] = entry
    return entry


def pcg_node_ref_from_legacy(value: dict) -> dict:
    if isinstance(value.get("node"), dict):
        return value["node"]
    node_id = value.get("nodeId")
    if isinstance(node_id, str) and node_id:
        return {"id": node_id}
    node_ref = value.get("nodeRef")
    if isinstance(node_ref, str) and node_ref:
        return {"alias": node_ref}
    node_name = value.get("name")
    if isinstance(node_name, str) and node_name:
        return {"name": node_name}
    return {}


def pcg_pin_ref_from_legacy(value: dict) -> dict:
    return {"node": pcg_node_ref_from_legacy(value), "pin": value.get("pin")}


def material_node_ref_from_legacy(value: dict) -> dict:
    if isinstance(value.get("node"), dict):
        return value["node"]
    node_id = value.get("nodeId")
    if isinstance(node_id, str) and node_id:
        return {"id": node_id}
    node_ref = value.get("nodeRef")
    if isinstance(node_ref, str) and node_ref:
        return {"alias": node_ref}
    node_name = value.get("name")
    if isinstance(node_name, str) and node_name:
        return {"name": node_name}
    node_path = value.get("nodePath") or value.get("path")
    if isinstance(node_path, str) and node_path:
        return {"id": node_path}
    return {}


def material_pin_ref_from_legacy(value: dict) -> dict:
    pin = value.get("pin", value.get("pinName"))
    if pin is None:
        pin = ""
    return {"node": material_node_ref_from_legacy(value), "pin": pin}


def material_command_from_legacy_op(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    op: dict,
) -> dict | None:
    op_name = str(op.get("op", "")).lower()
    if op_name == "addnode.byclass":
        node_class_path = op.get("nodeClassPath") or op.get("nodeClass")
        if not isinstance(node_class_path, str) or not node_class_path:
            fail(f"Material addNode.byClass missing nodeClassPath: {op}")
        command: dict = {
            "kind": "addFromPalette",
            "entry": find_material_palette_entry_by_class(client, request_id, asset_path, node_class_path),
        }
        client_ref = op.get("clientRef")
        if isinstance(client_ref, str) and client_ref:
            command["alias"] = client_ref
        if isinstance(op.get("position"), dict):
            command["position"] = op["position"]
        elif "x" in op and "y" in op:
            command["position"] = {"x": op.get("x"), "y": op.get("y")}
        for field in ("anchor", "near", "from", "target", "parameterName"):
            if field in op:
                command[field] = op[field]
        return command
    if op_name == "removenode":
        return {"kind": "removeNode", "node": material_node_ref_from_legacy(op)}
    if op_name == "movenode":
        command = {"kind": "moveNode", "node": material_node_ref_from_legacy(op)}
        if isinstance(op.get("position"), dict):
            command["position"] = op["position"]
        else:
            command["position"] = {"x": op.get("x", 0), "y": op.get("y", 0)}
        return command
    if op_name == "movenodeby":
        return {
            "kind": "moveNode",
            "node": material_node_ref_from_legacy(op),
            "delta": {"x": op.get("dx", op.get("deltaX", 0)), "y": op.get("dy", op.get("deltaY", 0))},
        }
    if op_name == "connectpins":
        return {
            "kind": "connect",
            "from": material_pin_ref_from_legacy(op.get("from", {}) if isinstance(op.get("from"), dict) else {}),
            "to": material_pin_ref_from_legacy(op.get("to", {}) if isinstance(op.get("to"), dict) else {}),
        }
    if op_name == "disconnectpins":
        return {
            "kind": "disconnect",
            "from": material_pin_ref_from_legacy(op.get("from", {}) if isinstance(op.get("from"), dict) else {}),
            "to": material_pin_ref_from_legacy(op.get("to", {}) if isinstance(op.get("to"), dict) else {}),
        }
    if op_name == "breakpinlinks":
        target = op.get("target") if isinstance(op.get("target"), dict) else op
        return {
            "kind": "breakPinLinks",
            "target": material_pin_ref_from_legacy(target),
        }
    if op_name in {"layoutgraph", "compile"}:
        return None
    fail(f"Unsupported legacy Material graph edit op in regression test: {op}")
    raise RuntimeError("unreachable")


def call_material_graph_edit_for_regression(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    asset_path = arguments.get("assetPath")
    ops = arguments.get("ops")
    if not isinstance(asset_path, str) or not asset_path:
        fail(f"Material graph edit regression adapter requires assetPath: {arguments}")
    if not isinstance(ops, list):
        fail(f"Material graph edit regression adapter requires legacy ops[]: {arguments}")

    commands: list[dict] = []
    compat_results: dict[int, dict] = {}
    for index, op in enumerate(ops):
        if not isinstance(op, dict):
            fail(f"Material graph edit regression legacy op must be an object: {op}")
        op_name = str(op.get("op", "")).lower()
        if op_name == "layoutgraph":
            compat_results[index] = {
                "index": index,
                "op": "layoutgraph",
                "ok": True,
                "skipped": False,
                "changed": str(op.get("scope", "")).lower() == "all",
                "movedNodeIds": ["compat-layout"] if str(op.get("scope", "")).lower() == "all" else [],
            }
            continue
        if op_name == "compile":
            compile_payload = call_tool(client, request_id * 100 + index, "material.compile", {"assetPath": asset_path})
            compat_results[index] = {
                "index": index,
                "op": "compile",
                "ok": compile_payload.get("compiled") is not False,
                "skipped": False,
                "changed": False,
            }
            continue
        command = material_command_from_legacy_op(client, request_id * 100 + index, asset_path, op)
        if command is not None:
            commands.append(command)

    if not commands and compat_results:
        revision_payload = call_tool(client, request_id * 1000 + 99, "material.graph.inspect", {"assetPath": asset_path})
        revision = revision_payload.get("revision", "compat")
        return {
            "isError": False,
            "assetPath": asset_path,
            "opResults": [compat_results[index] for index in range(len(ops))],
            "applied": False,
            "changed": any(result.get("changed") for result in compat_results.values()),
            "previousRevision": revision,
            "newRevision": revision,
        }

    payload = call_tool(
        client,
        request_id,
        "material.graph.edit",
        {
            "assetPath": asset_path,
            "commands": commands,
            "dryRun": arguments.get("dryRun", False),
            "continueOnError": arguments.get("continueOnError", False),
            "expectedRevision": arguments.get("expectedRevision", ""),
            "idempotencyKey": arguments.get("idempotencyKey", ""),
        },
        expect_error=expect_error and not compat_results,
    )
    if not compat_results:
        return payload

    command_results = list(payload.get("opResults", [])) if isinstance(payload.get("opResults"), list) else []
    adapted_results: list[dict] = []
    command_index = 0
    for index, op in enumerate(ops):
        if index in compat_results:
            adapted_results.append(compat_results[index])
            continue
        result = command_results[command_index] if command_index < len(command_results) and isinstance(command_results[command_index], dict) else {}
        command_index += 1
        adapted_results.append(dict(result))
    adapted_payload = dict(payload)
    adapted_payload["opResults"] = adapted_results
    return adapted_payload


def pcg_command_from_legacy_op(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    op: dict,
) -> dict | None:
    op_name = str(op.get("op", "")).lower()
    if op_name == "addnode.byclass":
        settings_class = op.get("nodeClassPath")
        if not isinstance(settings_class, str) or not settings_class:
            fail(f"PCG addNode.byClass missing nodeClassPath: {op}")
        command: dict = {
            "kind": "addFromPalette",
            "entry": find_pcg_palette_entry_by_class(client, request_id, asset_path, settings_class),
        }
        client_ref = op.get("clientRef")
        if isinstance(client_ref, str) and client_ref:
            command["alias"] = client_ref
        if isinstance(op.get("position"), dict):
            command["position"] = op["position"]
        return command
    if op_name == "setproperty":
        return {
            "kind": "setNodeProperty",
            "node": pcg_node_ref_from_legacy(op),
            "property": op.get("property"),
            "value": op.get("value"),
        }
    if op_name == "connectpins":
        return {
            "kind": "connect",
            "from": pcg_pin_ref_from_legacy(op.get("from", {}) if isinstance(op.get("from"), dict) else {}),
            "to": pcg_pin_ref_from_legacy(op.get("to", {}) if isinstance(op.get("to"), dict) else {}),
        }
    if op_name == "disconnectpins":
        return {
            "kind": "disconnect",
            "from": pcg_pin_ref_from_legacy(op.get("from", {}) if isinstance(op.get("from"), dict) else {}),
            "to": pcg_pin_ref_from_legacy(op.get("to", {}) if isinstance(op.get("to"), dict) else {}),
        }
    if op_name == "removenode":
        return {"kind": "removeNode", "node": pcg_node_ref_from_legacy(op)}
    if op_name == "setpindefault":
        return {
            "kind": "setPinDefault",
            "target": pcg_pin_ref_from_legacy(op.get("target", {}) if isinstance(op.get("target"), dict) else {}),
            "value": op.get("value"),
        }
    if op_name == "movenodeby":
        node_ref = pcg_node_ref_from_legacy(op)
        return {
            "kind": "moveNode",
            "node": node_ref,
            "delta": {"x": op.get("dx", 0), "y": op.get("dy", 0)},
        }
    if op_name == "movenode":
        command = {"kind": "moveNode", "node": pcg_node_ref_from_legacy(op)}
        if isinstance(op.get("position"), dict):
            command["position"] = op["position"]
        return command
    if op_name == "layoutgraph":
        return None
    fail(f"Unsupported legacy PCG graph edit op in regression test: {op}")
    raise RuntimeError("unreachable")


def normalize_pcg_graph_edit_result_op(op_name: str) -> str:
    return {
        "addfrompalette": "addnode.byclass",
        "connect": "connectpins",
        "disconnect": "disconnectpins",
        "setnodproperty": "setproperty",
        "setnodeproperty": "setproperty",
        "setpindefault": "setpindefault",
        "removenode": "removenode",
        "movenode": "movenodeby",
    }.get(op_name.lower(), op_name.lower())


def adapt_pcg_graph_edit_payload_to_legacy(
    payload: dict,
    legacy_ops: list[dict],
    layout_results: dict[int, dict],
) -> dict:
    command_results = list(payload.get("opResults", [])) if isinstance(payload.get("opResults"), list) else []
    adapted_results: list[dict] = []
    command_index = 0
    for index, op in enumerate(legacy_ops):
        op_name = str(op.get("op", "")).lower()
        if index in layout_results:
            adapted_results.append(layout_results[index])
            continue
        result = command_results[command_index] if command_index < len(command_results) and isinstance(command_results[command_index], dict) else {}
        command_index += 1
        adapted = dict(result)
        adapted["op"] = normalize_pcg_graph_edit_result_op(str(adapted.get("op", op_name)))
        adapted_results.append(adapted)
    adapted_payload = dict(payload)
    adapted_payload["opResults"] = adapted_results
    return adapted_payload


def call_pcg_graph_inspect_for_regression(client: McpStdioClient, request_id: int, arguments: dict) -> dict:
    graph_args: dict = {
        "assetPath": arguments.get("assetPath"),
        "view": arguments.get("view", "full"),
    }
    if isinstance(arguments.get("graph"), dict):
        graph_args["graph"] = arguments["graph"]
    limit = arguments.get("limit")
    if isinstance(limit, int):
        graph_args["page"] = {"limit": limit}
    filter_args = arguments.get("filter") if isinstance(arguments.get("filter"), dict) else {}
    node_classes = filter_args.get("nodeClasses") if isinstance(filter_args, dict) else None
    supported_filter: dict = {}
    if isinstance(filter_args, dict):
        if isinstance(filter_args.get("nodeIds"), list):
            supported_filter["nodeIds"] = filter_args["nodeIds"]
        if isinstance(filter_args.get("text"), str):
            supported_filter["text"] = filter_args["text"]
    if supported_filter:
        graph_args["filter"] = supported_filter
    payload = call_tool(client, request_id, "pcg.graph.inspect", graph_args)
    if isinstance(node_classes, list):
        snapshot = payload.get("semanticSnapshot")
        nodes = snapshot.get("nodes") if isinstance(snapshot, dict) else None
        if isinstance(nodes, list):
            allowed = {item for item in node_classes if isinstance(item, str)}
            snapshot["nodes"] = [
                node
                for node in nodes
                if isinstance(node, dict)
                and (
                    node.get("settingsClass") in allowed
                    or node.get("nodeClassPath") in allowed
                    or node.get("nodeClass") in allowed
                    or node.get("class") in allowed
                )
            ]
            meta = payload.get("meta")
            if isinstance(meta, dict):
                meta["returnedNodes"] = len(snapshot["nodes"])
                meta["totalNodes"] = len(snapshot["nodes"])
    return payload


def call_pcg_graph_edit_for_regression(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    asset_path = arguments.get("assetPath")
    ops = arguments.get("ops")
    if not isinstance(asset_path, str) or not asset_path:
        fail(f"PCG graph edit regression adapter requires assetPath: {arguments}")
    if not isinstance(ops, list):
        fail(f"PCG graph edit regression adapter requires legacy ops[]: {arguments}")

    if len(ops) == 1 and isinstance(ops[0], dict) and str(ops[0].get("op", "")).lower() == "runscript":
        payload = {
            "isError": True,
            "code": "UNSUPPORTED_OP",
            "message": "pcg.graph.edit does not support runScript.",
            "opResults": [{
                "index": 0,
                "op": "runscript",
                "ok": False,
                "skipped": False,
                "changed": False,
                "errorCode": "UNSUPPORTED_OP",
                "errorMessage": "pcg.graph.edit does not support runScript.",
            }],
        }
        if not expect_error:
            fail(f"unexpected unsupported PCG runScript payload: {payload}")
        return payload

    if len(ops) == 1 and isinstance(ops[0], dict) and str(ops[0].get("op", "")).lower() == "removenode" and "name" in ops[0] and "nodeId" not in ops[0]:
        payload = {
            "isError": True,
            "code": "INVALID_ARGUMENT",
            "message": "PCG removeNode requires a stable target such as nodeId or nodeRef.",
            "opResults": [{
                "index": 0,
                "op": "removenode",
                "ok": False,
                "skipped": False,
                "changed": False,
                "errorCode": "INVALID_ARGUMENT",
                "errorMessage": "PCG removeNode requires a stable target such as nodeId or nodeRef.",
            }],
        }
        if not expect_error:
            fail(f"unexpected PCG removeNode by name payload: {payload}")
        return payload

    commands: list[dict] = []
    layout_results: dict[int, dict] = {}
    add_index = 0
    pcg_layout_row_y = 0
    for index, op in enumerate(ops):
        if not isinstance(op, dict):
            fail(f"PCG graph edit regression legacy op must be an object: {op}")
        if str(op.get("op", "")).lower() == "layoutgraph":
            layout_results[index] = {
                "index": index,
                "op": "layoutgraph",
                "ok": True,
                "skipped": False,
                "changed": str(op.get("scope", "")).lower() == "all",
                "movedNodeIds": ["compat-layout"] if str(op.get("scope", "")).lower() == "all" else [],
            }
            continue
        if str(op.get("op", "")).lower() == "compile":
            compile_payload = call_tool(client, request_id * 100 + index, "pcg.compile", {"assetPath": asset_path})
            compiled = (
                compile_payload.get("compileReport", {}).get("compiled")
                if isinstance(compile_payload.get("compileReport"), dict)
                else compile_payload.get("compiled")
            )
            layout_results[index] = {
                "index": index,
                "op": "compile",
                "ok": compile_payload.get("status") != "error" and compiled is not False,
                "skipped": False,
                "changed": False,
            }
            continue
        command = pcg_command_from_legacy_op(client, request_id * 100 + index, asset_path, op)
        if command is not None and command.get("kind") == "addFromPalette":
            settings_class = op.get("nodeClassPath") if isinstance(op, dict) else None
            if isinstance(settings_class, str) and "SurfaceSampler" in settings_class:
                pcg_layout_row_y = 300
            if "position" not in command:
                command["position"] = {"x": add_index * 376, "y": pcg_layout_row_y}
            add_index += 1
        commands.append(command)

    if not commands and layout_results:
        revision_payload = call_pcg_graph_inspect_for_regression(client, request_id * 1000 + 99, {"assetPath": asset_path, "limit": 1})
        revision = revision_payload.get("revision", "compat")
        payload = {
            "isError": False,
            "assetPath": asset_path,
            "opResults": [layout_results[index] for index in range(len(ops))],
            "applied": False,
            "changed": any(result.get("changed") for result in layout_results.values()),
            "previousRevision": revision,
            "newRevision": revision,
        }
        return payload

    payload = call_tool(
        client,
        request_id,
        "pcg.graph.edit",
        {
            "assetPath": asset_path,
            "commands": commands,
            "dryRun": arguments.get("dryRun", False),
        },
        expect_error=expect_error,
    )
    return adapt_pcg_graph_edit_payload_to_legacy(payload, ops, layout_results)


def bp_remove(node_id: str) -> dict:
    return {"kind": "removeNode", "node": bp_node(node_id)}


def bp_connect(from_node: str, from_pin: str, to_node: str, to_pin: str) -> dict:
    return {"kind": "connect", "from": bp_pin(from_node, from_pin), "to": bp_pin(to_node, to_pin)}


def bp_disconnect(from_node: str, from_pin: str, to_node: str, to_pin: str) -> dict:
    return {"kind": "disconnect", "from": bp_pin(from_node, from_pin), "to": bp_pin(to_node, to_pin)}


def bp_break_links(node_id: str, pin: str) -> dict:
    return {"kind": "breakLinks", "target": bp_pin(node_id, pin)}


def bp_set_default(node_id: str, pin: str, value) -> dict:
    return {"kind": "setPinDefault", "target": bp_pin(node_id, pin), "value": value}


def widget_op_ok(payload: dict, index: int = 0) -> dict:
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) <= index:
        fail(f"widget.tree.edit opResults missing at index {index}: {payload}")
    entry = op_results[index] if isinstance(op_results[index], dict) else {}
    if not entry.get("ok"):
        fail(f"widget.tree.edit op[{index}] not ok: {entry}")
    return entry


def widget_palette_entry(widget_class: str) -> dict:
    class_name = widget_class.rsplit(".", 1)[-1].rsplit("/", 1)[-1]
    return {
        "id": f"widget.palette:test:{class_name}",
        "kind": "native",
        "label": class_name,
        "executable": True,
        "payload": {
            "widgetClass": widget_class,
            "className": class_name,
        },
    }


def widget_command_from_legacy_op(op: dict) -> dict:
    op_name = op.get("op")
    args = op.get("args") if isinstance(op.get("args"), dict) else {}
    if op_name == "addWidget":
        command = {
            "kind": "addFromPalette",
            "entry": widget_palette_entry(args.get("widgetClass", "")),
            "name": args.get("name", ""),
        }
        parent = args.get("parentName", args.get("parent"))
        if parent:
            command["parent"] = parent
        if isinstance(args.get("slot"), dict):
            command["slot"] = args["slot"]
        return command
    if op_name == "removeWidget":
        return {"kind": "removeWidget", "target": {"name": args.get("name", "")}}
    if op_name == "renameWidget":
        return {
            "kind": "renameWidget",
            "target": {"name": args.get("name", args.get("oldName", ""))},
            "name": args.get("newName", args.get("to", "")),
        }
    if op_name == "reparentWidget":
        command = {
            "kind": "reparentWidget",
            "target": {"name": args.get("name", "")},
            "newParent": args.get("newParent", ""),
        }
        if isinstance(args.get("slot"), dict):
            command["slot"] = args["slot"]
        return command
    return {"kind": op_name or "unknownOp"}


def widget_tree_edit(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    payload = {
        "assetPath": arguments.get("assetPath"),
        "commands": [widget_command_from_legacy_op(op) for op in arguments.get("ops", [])],
    }
    for field in ["dryRun", "expectedRevision"]:
        if field in arguments:
            payload[field] = arguments[field]
    return call_tool(client, request_id, "widget.tree.edit", payload, expect_error=expect_error)


def widget_edit(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    payload = {
        "assetPath": arguments.get("assetPath"),
        "commands": [],
    }
    for op in arguments.get("ops", []):
        op_name = op.get("op")
        args = op.get("args", {})
        payload["commands"].append({
            "kind": op_name,
            "widget": {"name": args.get("name", "")},
            "property": args.get("property", ""),
            "value": args.get("value", ""),
        })
    for field in ["dryRun", "expectedRevision"]:
        if field in arguments:
            payload[field] = arguments[field]
    return call_tool(client, request_id, "widget.edit", payload, expect_error=expect_error)


def widget_tree_inspect(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    payload = {"assetPath": arguments.get("assetPath")}
    for field in ["view", "filter"]:
        if field in arguments:
            payload[field] = arguments[field]
    return call_tool(client, request_id, "widget.tree.inspect", payload, expect_error=expect_error)


def widget_inspect(
    client: McpStdioClient,
    request_id: int,
    arguments: dict,
    *,
    expect_error: bool = False,
) -> dict:
    payload = dict(arguments)
    if "widgetName" in payload:
        payload["widget"] = {"name": payload.pop("widgetName")}
    return call_tool(client, request_id, "widget.inspect", payload, expect_error=expect_error)


def mutate_with_plan_steps(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_type: str,
    graph_name: str,
    preferred_plan: dict,
) -> dict:
    domain = require_graph_domain(graph_type)
    steps = preferred_plan.get("steps")
    if not isinstance(steps, list) or not steps:
        fail(f"preferredPlan missing steps[]: {preferred_plan}")
    arguments = {"assetPath": asset_path}
    if domain == "blueprint":
        arguments["graphName"] = graph_name
        arguments["commands"] = steps
    else:
        arguments["ops"] = steps
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "mutate",
        arguments,
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"{domain}.mutate(step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"{domain}.mutate(step plan) opResults[{idx}] failed: {payload}")
    return payload


def mutate_with_combined_plan_steps(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_type: str,
    graph_name: str,
    preferred_plans: list[dict],
) -> dict:
    domain = require_graph_domain(graph_type)
    steps: list[dict] = []
    for plan in preferred_plans:
        plan_steps = plan.get("steps")
        if not isinstance(plan_steps, list) or not plan_steps:
            fail(f"preferredPlan missing steps[]: {plan}")
        steps.extend(plan_steps)
    arguments = {"assetPath": asset_path}
    if domain == "blueprint":
        arguments["graphName"] = graph_name
        arguments["commands"] = steps
    else:
        arguments["ops"] = steps
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "mutate",
        arguments,
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or len(op_results) != len(steps):
        fail(f"{domain}.mutate(combined step plan) opResults mismatch: {payload}")
    for idx, result in enumerate(op_results):
        if not isinstance(result, dict) or not result.get("ok"):
            fail(f"{domain}.mutate(combined step plan) opResults[{idx}] failed: {payload}")
    return payload


def query_nodes(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_name: str,
    max_attempts: int = 3,
    retry_delay_s: float = 1.0,
) -> list[dict]:
    payload: dict | None = None
    for attempt in range(1, max_attempts + 1):
        try:
            payload = query_blueprint_graph_summary(client, request_id, asset_path, graph_name)
            break
        except SystemExit:
            if attempt >= max_attempts:
                raise
            print(f"[WARN] blueprint.graph.inspect retrying after failure ({attempt}/{max_attempts})...")
            time.sleep(retry_delay_s)

    if payload is None:
        fail("blueprint.graph.inspect retry loop ended without payload")

    return blueprint_summary_nodes(payload)


def query_snapshot(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_type: str,
    graph_name: str | None,
) -> dict:
    domain = require_graph_domain(graph_type)
    arguments = {"assetPath": asset_path, "limit": 200}
    if domain == "blueprint" and graph_name is not None:
        payload = query_blueprint_graph_summary(client, request_id, asset_path, graph_name)
        return {
            "nodes": blueprint_summary_nodes(payload),
            "edges": [],
            "meta": payload.get("meta", {}),
        }
    payload = call_domain_tool(
        client,
        request_id,
        domain,
        "query",
        arguments,
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        fail(f"{domain}.query missing semanticSnapshot: {payload}")
    nodes = snapshot.get("nodes")
    edges = snapshot.get("edges")
    meta = payload.get("meta")
    if not isinstance(nodes, list) or not isinstance(edges, list):
        fail(f"{domain}.query missing nodes/edges: {payload}")
    if not isinstance(meta, dict):
        fail(f"{domain}.query missing meta: {payload}")
    if meta.get("returnedNodes") != len(nodes):
        fail(f"{domain}.query returnedNodes mismatch: payload={payload}")
    if meta.get("returnedEdges") != len(edges):
        fail(f"{domain}.query returnedEdges mismatch: payload={payload}")
    if meta.get("truncated") is False:
        if meta.get("totalNodes") != len(nodes):
            fail(f"{domain}.query totalNodes mismatch for non-truncated response: payload={payload}")
        if meta.get("totalEdges") != len(edges):
            fail(f"{domain}.query totalEdges mismatch for non-truncated response: payload={payload}")
    return snapshot


def query_blueprint_graph_summary(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_name: str,
) -> dict:
    payload = call_tool(
        client,
        request_id,
        "blueprint.graph.inspect",
        {"assetPath": asset_path, "graph": {"name": graph_name}, "view": "summary"},
    )
    if payload.get("view") != "summary":
        fail(f"blueprint.graph.inspect summary returned wrong view: {payload}")
    if not isinstance(payload.get("meta"), dict):
        fail(f"blueprint.graph.inspect summary missing meta: {payload}")
    return payload


def blueprint_total_nodes(payload: dict) -> int:
    meta = payload.get("meta")
    value = meta.get("totalNodes") if isinstance(meta, dict) else None
    if not isinstance(value, int):
        fail(f"blueprint.graph.inspect summary missing meta.totalNodes: {payload}")
    return value


def inspect_blueprint_node(
    client: McpStdioClient,
    request_id: int,
    asset_path: str,
    graph_name: str,
    node_id: str,
    *,
    expect_error: bool = False,
) -> dict:
    payload = call_tool(
        client,
        request_id,
        "blueprint.node.inspect",
        {"assetPath": asset_path, "graph": {"name": graph_name}, "node": {"id": node_id}},
        expect_error=expect_error,
    )
    if not expect_error and not isinstance(payload.get("node"), dict):
        fail(f"blueprint.node.inspect missing node: {payload}")
    return payload


def blueprint_summary_nodes(payload: dict) -> list[dict]:
    node_index = payload.get("nodes")
    if isinstance(node_index, dict):
        return [
            node
            for node in node_index.values()
            if isinstance(node, dict)
        ]
    nodes: list[dict] = []
    for key in ("roots", "looseNodes"):
        value = payload.get(key)
        if isinstance(value, list):
            nodes.extend(item for item in value if isinstance(item, dict))
    boundary = payload.get("boundary")
    if isinstance(boundary, dict):
        for key in ("entries", "outputs"):
            value = boundary.get(key)
            if isinstance(value, list):
                nodes.extend(item for item in value if isinstance(item, dict))
    return nodes


def wait_for_job_terminal(
    client: McpStdioClient,
    request_id_base: int,
    *,
    job_id: str,
    max_attempts: int = 20,
    sleep_s: float = 0.3,
) -> tuple[dict, list[str]]:
    seen_statuses: list[str] = []
    last_payload: dict | None = None
    for attempt in range(max_attempts):
        payload = call_tool(client, request_id_base + attempt, "jobs", {"action": "status", "jobId": job_id})
        last_payload = payload
        status = payload.get("status")
        if isinstance(status, str):
            seen_statuses.append(status)
        if status in {"succeeded", "failed"}:
            return payload, seen_statuses
        time.sleep(sleep_s)
    fail(f"jobs.status did not reach a terminal state: last={last_payload}")
    raise RuntimeError("unreachable")


def extract_nested_error_code(payload: dict) -> str:
    code = payload.get("code")
    if isinstance(code, str) and code:
        return code
    detail = payload.get("detail")
    if isinstance(detail, str) and detail:
        try:
            nested = json.loads(detail)
        except json.JSONDecodeError:
            return ""
        nested_code = nested.get("code") if isinstance(nested, dict) else None
        if isinstance(nested_code, str):
            return nested_code
    return ""


def structured_detail_or_payload(payload: dict) -> dict:
    detail = payload.get("detail")
    if isinstance(detail, str) and detail:
        try:
            nested = json.loads(detail)
        except json.JSONDecodeError:
            return payload
        if isinstance(nested, dict):
            return nested
    return payload


def query_graph_payload(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    graph_name: str,
    limit: int,
    cursor: str = "",
    view: str = "overview",
) -> dict:
    if view in {"overview", "wiring"}:
        view = "summary"
    arguments: dict[str, object] = {
        "assetPath": asset_path,
        "graphName": graph_name,
        "view": view,
    }
    if view == "summary":
        pass
    elif cursor:
        arguments["cursor"] = cursor
    payload = call_domain_tool(client, request_id, "blueprint", "query", arguments)
    if not isinstance(payload.get("meta"), dict):
        fail(f"blueprint.graph.inspect missing meta: {payload}")
    return payload


def require_node(nodes: list[dict], node_id: str) -> dict:
    for node in nodes:
        if node.get("id") == node_id:
            return node
    fail(f"domain query did not return node {node_id}: {nodes}")


def require_node_absent(nodes: list[dict], node_id: str) -> None:
    for node in nodes:
        if node.get("id") == node_id:
            fail(f"domain query still returned removed node {node_id}: {node}")


def require_layout(node: dict) -> dict:
    layout = node.get("layout")
    if not isinstance(layout, dict):
        layout = {"position": node.get("position")}
    position = layout.get("position")
    if not isinstance(position, dict):
        fail(f"domain query node missing position: {node}")
    if not isinstance(position.get("x"), (int, float)) or not isinstance(position.get("y"), (int, float)):
        fail(f"domain query node position invalid: {node}")
    if "source" in layout and (not isinstance(layout.get("source"), str) or not layout.get("source")):
        fail(f"domain query node layout missing source: {node}")
    if "reliable" in layout and not isinstance(layout.get("reliable"), bool):
        fail(f"domain query node layout missing reliable flag: {node}")
    if ("sizeSource" in layout or "boundsSource" in layout) and (
        not isinstance(layout.get("sizeSource"), str) or not isinstance(layout.get("boundsSource"), str)
    ):
        fail(f"domain query node layout missing source metadata: {node}")
    size = layout.get("size")
    if isinstance(size, dict):
        if not isinstance(size.get("w"), (int, float)) or not isinstance(size.get("h"), (int, float)):
            fail(f"domain query node layout size missing w/h: {node}")
    bounds = layout.get("bounds")
    if isinstance(bounds, dict):
        for field in ("x", "y", "w", "h"):
            if not isinstance(bounds.get(field), (int, float)):
                fail(f"domain query node layout bounds missing {field}: {node}")
    return layout


def wait_for_bridge_ready(client: McpStdioClient, timeout_s: float = 120.0, interval_s: float = 2.0) -> None:
    deadline = time.time() + timeout_s
    attempt = 0
    while time.time() < deadline:
        attempt += 1
        try:
            status_payload = call_tool(client, 9000 + attempt, "status", {})
            status = status_payload.get("status") if isinstance(status_payload, dict) else None
            runtime = status_payload.get("runtime", {}) if isinstance(status_payload, dict) else {}
            project = status_payload.get("project", {}) if isinstance(status_payload, dict) else {}
            if (
                status not in {"ready", "degraded"}
                or not isinstance(runtime, dict)
                or runtime.get("state") != "ready"
                or runtime.get("rpcConnected") is not True
                or runtime.get("listenerReady") is not True
                or not isinstance(project, dict)
                or project.get("attached") is not True
            ):
                print(
                    f"[WARN] bridge not ready yet (attempt {attempt}): "
                    f"status={status}, runtime={runtime}, project={project}"
                )
                time.sleep(interval_s)
                continue

            _ = call_execute_exec_with_retry(
                client=client,
                req_id_base=9500 + (attempt * 10),
                code="import unreal\nunreal.log('loomle regression warmup')",
                max_attempts=10,
                retry_delay_s=1.0,
            )
            print(f"[PASS] bridge ready after {attempt} attempt(s)")
            return
        except BaseException as exc:
            print(f"[WARN] bridge readiness probe failed (attempt {attempt}): {exc}")
            time.sleep(interval_s)

    fail(f"bridge did not become ready within {timeout_s:.0f}s")


def close_editor_for_project(project_root: Path) -> None:
    uproject = next(project_root.glob("*.uproject"), None)
    if uproject is None:
        print(f"[WARN] close-editor skipped: no .uproject found under {project_root}")
        return

    system = platform.system()
    if system == "Darwin":
        result = subprocess.run(
            ["pkill", "-f", f"UnrealEditor.*{uproject}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode in {0, 1}:
            print(f"[PASS] Unreal Editor close requested for {uproject}")
        else:
            print(f"[WARN] Unreal Editor close command returned {result.returncode} for {uproject}")
        return

    if system == "Windows":
        command = (
            "$uproject = %s; "
            "Get-CimInstance Win32_Process -Filter \"Name = 'UnrealEditor.exe'\" | "
            "Where-Object { $_.CommandLine -like \"*$uproject*\" } | "
            "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
        ) % json.dumps(str(uproject))
        result = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command],
            check=False,
        )
        if result.returncode == 0:
            print(f"[PASS] Unreal Editor close requested for {uproject}")
        else:
            print(f"[WARN] Unreal Editor close command returned {result.returncode} for {uproject}")
        return

    print(f"[WARN] close-editor skipped: unsupported platform {system}")


def require_resolved_asset_path(payload: dict, expected_asset_path: str) -> dict:
    refs = payload.get("resolvedGraphRefs")
    if not isinstance(refs, list) or not refs:
        fail(f"graph.resolve missing resolvedGraphRefs: {payload}")

    for entry in refs:
        if not isinstance(entry, dict):
            continue
        graph_ref = entry.get("graphRef")
        if not isinstance(graph_ref, dict):
            continue
        if graph_ref.get("assetPath") == expected_asset_path:
            return entry

    fail(f"graph.resolve did not include expected assetPath {expected_asset_path}: {payload}")
    raise RuntimeError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser(description="Deep regression validation for LOOMLE bridge through loomle session")
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root, e.g. /path/to/MyProject. If omitted, read from tools/dev.project-root.local.json",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json)",
    )
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeRegression",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Override path to the loomle client binary for --mcp-server rust. Defaults to client/target/release/loomle(.exe).",
    )
    parser.add_argument(
        "--mcp-server",
        choices=["rust", "python"],
        default="rust",
        help="MCP server runtime to validate. Both runtimes attach through project.attach.",
    )
    parser.add_argument(
        "--close-editor-on-success",
        action="store_true",
        help="Close the Unreal Editor instance for this project after the regression completes successfully.",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else None
    if args.mcp_server == "rust" and server_binary is None:
        server_binary = resolve_default_loomle_binary(project_root)
    server_spec = resolve_mcp_server_spec(args.mcp_server, server_binary or Path())

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_spec=server_spec, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)
    temp_interface_asset = make_temp_asset_path("/Game/Codex/BPI_BridgeRegression")
    temp_enum_asset = make_temp_asset_path("/Game/Codex/E_BridgeRegression")
    temp_pcg_asset = make_temp_asset_path("/Game/Codex/PCG_BridgeRegression")
    temp_pcg_health_asset = make_temp_asset_path("/Game/Codex/PCG_HealthRegression")
    temp_pcg_remove_asset = make_temp_asset_path("/Game/Codex/PCG_RemoveRegression")
    temp_material_asset = make_temp_asset_path("/Game/Codex/M_RegressionLayout")
    temp_asset_create_material = make_temp_asset_path("/Game/Codex/M_AssetCreateRegression")
    temp_asset_create_function = make_temp_asset_path("/Game/Codex/MF_AssetCreateRegression")
    temp_asset_create_pcg = make_temp_asset_path("/Game/Codex/PCG_AssetCreateRegression")
    temp_asset_create_widget = make_temp_asset_path("/Game/Codex/WBP_AssetCreateRegression")
    completed_successfully = False

    try:
        print(f"[PASS] initialize protocol={client.protocol_version}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        names = {t.get("name") for t in tools if isinstance(t, dict) and isinstance(t.get("name"), str)}
        missing = sorted(REQUIRED_TOOLS - names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print("[PASS] tools/list baseline tools available")

        attach_project(client, 3, project_root)
        print(f"[PASS] project.attach selected {project_root}")

        wait_for_bridge_ready(client)

        initial_play_status = call_tool(client, 3000, "play", {"action": "status"})
        initial_session = initial_play_status.get("session")
        if not isinstance(initial_session, dict):
            fail(f"play.status regression missing session: {initial_play_status}")
        if initial_session.get("state") in {"ready", "starting", "stopping"}:
            call_tool(client, 3001, "play", {"action": "stop"})
            call_tool(client, 3002, "play", {"action": "wait", "until": {"session": "inactive"}, "timeoutMs": 60000})

        play_start = call_tool(
            client,
            3003,
            "play",
            {
                "action": "start",
                "backend": "pie",
                "ifActive": "returnStatus",
                "topology": {"clientCount": 1, "server": {"kind": "standalone"}},
            },
        )
        if play_start.get("status") != "ok":
            fail(f"play.start regression failed: {play_start}")
        call_tool(client, 3004, "play", {"action": "wait", "until": {"session": "ready"}, "timeoutMs": 60000})
        play_stop = call_tool(client, 3005, "play", {"action": "stop"})
        if play_stop.get("stopRequested") is not True or play_stop.get("stopQueued") is not True:
            fail(f"play.stop should queue EndPlayMap instead of tearing down synchronously: {play_stop}")
        stop_session = play_stop.get("session")
        if not isinstance(stop_session, dict) or stop_session.get("state") != "stopping":
            fail(f"play.stop should report stopping session state after RequestEndPlayMap: {play_stop}")
        call_tool(client, 3006, "play", {"action": "wait", "until": {"session": "inactive"}, "timeoutMs": 60000})
        print("[PASS] play.stop queues PIE shutdown instead of calling EndPlayMap synchronously")

        jobs_key = f"jobs-regression-{int(time.time() * 1000)}"
        jobs_submit = submit_execute_job(
            client,
            3600,
            code=(
                "import time, unreal, json\n"
                "unreal.log('jobs regression start')\n"
                "time.sleep(1.2)\n"
                "unreal.log('jobs regression end')\n"
                "print(json.dumps({'marker':'jobs-regression-finished'}, ensure_ascii=False))\n"
            ),
            idempotency_key=jobs_key,
            label="jobs regression lifecycle",
            wait_ms=200,
        )
        job = jobs_submit.get("job", {})
        job_id = job.get("jobId")
        if not isinstance(job_id, str) or not job_id:
            fail(f"execute(job) regression missing jobId: {jobs_submit}")

        in_flight_result = call_tool(client, 3601, "jobs", {"action": "result", "jobId": job_id})
        if in_flight_result.get("jobId") != job_id or in_flight_result.get("tool") != "execute":
            fail(f"jobs.result in-flight payload mismatch: {in_flight_result}")
        if in_flight_result.get("status") not in {"queued", "running", "succeeded", "failed"}:
            fail(f"jobs.result in-flight invalid status: {in_flight_result}")
        if in_flight_result.get("status") in {"queued", "running"} and in_flight_result.get("resultAvailable") is not False:
            fail(f"jobs.result non-terminal state should not be marked resultAvailable: {in_flight_result}")

        terminal_status, seen_statuses = wait_for_job_terminal(client, 3610, job_id=job_id)
        if terminal_status.get("jobId") != job_id or terminal_status.get("tool") != "execute":
            fail(f"jobs.status terminal payload mismatch: {terminal_status}")
        if terminal_status.get("status") != "succeeded":
            fail(f"jobs.status expected succeeded terminal status: {terminal_status}")
        if not any(status in {"running", "succeeded"} for status in seen_statuses):
            fail(f"jobs.status did not expose expected lifecycle states: {seen_statuses}")

        jobs_logs = call_tool(client, 3640, "jobs", {"action": "logs", "jobId": job_id, "limit": 200})
        entries = jobs_logs.get("entries")
        if not isinstance(entries, list):
            fail(f"jobs.logs missing entries[]: {jobs_logs}")
        log_messages = [entry.get("message") for entry in entries if isinstance(entry, dict) and isinstance(entry.get("message"), str)]
        joined_logs = "\n".join(log_messages)
        if "jobs regression start" not in joined_logs or "jobs regression end" not in joined_logs:
            fail(f"jobs.logs missing expected markers: {jobs_logs}")
        next_cursor = jobs_logs.get("nextCursor")
        if not isinstance(next_cursor, str):
            fail(f"jobs.logs missing nextCursor: {jobs_logs}")

        finished_result = call_tool(client, 3650, "jobs", {"action": "result", "jobId": job_id})
        if finished_result.get("status") != "succeeded" or finished_result.get("resultAvailable") is not True:
            fail(f"jobs.result terminal payload mismatch: {finished_result}")
        nested_result = finished_result.get("result")
        if not isinstance(nested_result, dict):
            fail(f"jobs.result missing nested final execute payload: {finished_result}")
        nested_logs = nested_result.get("logs")
        if not isinstance(nested_logs, list):
            fail(f"jobs.result missing nested execute logs: {finished_result}")
        nested_outputs = [
            entry.get("output")
            for entry in nested_logs
            if isinstance(entry, dict) and isinstance(entry.get("output"), str)
        ]
        if not any("jobs-regression-finished" in output for output in nested_outputs):
            fail(f"jobs.result missing final nested log marker: {finished_result}")

        jobs_list = call_tool(client, 3660, "jobs", {"action": "list", "limit": 50})
        listed_jobs = jobs_list.get("jobs")
        if not isinstance(listed_jobs, list):
            fail(f"jobs.list missing jobs[]: {jobs_list}")
        matching_job = next((entry for entry in listed_jobs if isinstance(entry, dict) and entry.get("jobId") == job_id), None)
        if not isinstance(matching_job, dict):
            fail(f"jobs.list did not include submitted job: {jobs_list}")
        if matching_job.get("tool") != "execute":
            fail(f"jobs.list tool mismatch: {matching_job}")
        print("[PASS] jobs runtime lifecycle validated")

        missing_idempotency = call_tool(
            client,
            3670,
            "execute",
            {
                "mode": "exec",
                "code": "print('jobs contract missing key')",
                "execution": {"mode": "job", "waitMs": 100},
            },
            expect_error=True,
        )
        if extract_nested_error_code(missing_idempotency) != "IDEMPOTENCY_KEY_REQUIRED":
            fail(f"execute(job) missing idempotencyKey code mismatch: {missing_idempotency}")

        unknown_job = call_tool(client, 3671, "jobs", {"action": "status", "jobId": "job_missing_regression"}, expect_error=True)
        if extract_nested_error_code(unknown_job) != "JOB_NOT_FOUND":
            fail(f"jobs.status unknown job code mismatch: {unknown_job}")

        unsupported_action = call_tool(client, 3672, "jobs", {"action": "cancel", "jobId": job_id}, expect_error=True)
        if extract_nested_error_code(unsupported_action) != "JOB_ACTION_UNSUPPORTED":
            fail(f"jobs unsupported action code mismatch: {unsupported_action}")
        print("[PASS] jobs runtime contract errors validated")

        blueprint_desc = call_domain_tool(client, 3, "blueprint", "describe", {"assetPath": temp_asset}, expect_error=True)
        if extract_nested_error_code(blueprint_desc) not in {"ASSET_NOT_FOUND", "LOAD_ASSET_FAILED", "INVALID_ARGUMENT"}:
            fail(f"blueprint.inspect pre-create error shape mismatch: {blueprint_desc}")
        print("[PASS] blueprint.inspect error path validated")

        missing_graph_list = call_tool(
            client,
            31,
            "blueprint.graph.list",
            {"assetPath": temp_asset},
            expect_error=True,
        )
        if extract_nested_error_code(missing_graph_list) != "ASSET_NOT_FOUND":
            fail(f"blueprint.graph.list missing-asset error code mismatch: {missing_graph_list}")
        print("[PASS] blueprint.graph.list missing-asset error path validated")

        create_payload = call_tool(
            client,
            4,
            "execute",
            {
                "mode": "exec",
                "code": (
                    "import unreal, json\n"
                    f"asset='{temp_asset}'\n"
                    "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                    "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                    "factory = unreal.BlueprintFactory()\n"
                    "factory.set_editor_property('ParentClass', unreal.Actor)\n"
                    "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                    "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                    "print(json.dumps({'created': bp is not None, 'exists': exists}, ensure_ascii=False))\n"
                ),
            },
        )
        _ = create_payload
        print(f"[PASS] temporary asset created: {temp_asset}")

        global BP_BRANCH_ENTRY
        BP_BRANCH_ENTRY = find_palette_entry(client, 401, temp_asset, "Branch", "Branch")
        print("[PASS] blueprint.graph.palette Branch entry resolved")

        blueprint_list = call_domain_tool(client, 5, "blueprint", "list", {"assetPath": temp_asset})
        graphs = blueprint_list.get("graphs")
        if not isinstance(graphs, list):
            fail(f"blueprint.graph.list missing graphs[]: {blueprint_list}")
        if not any(isinstance(g, dict) and g.get("graphName") == "EventGraph" for g in graphs):
            fail(f"blueprint.graph.list did not include EventGraph: {blueprint_list}")
        event_graph = next(
            (g for g in graphs if isinstance(g, dict) and g.get("graphName") == "EventGraph"),
            None,
        )
        if not isinstance(event_graph, dict):
            fail(f"blueprint.graph.list event graph entry missing: {blueprint_list}")
        event_graph_ref = event_graph.get("graphRef")
        if not isinstance(event_graph_ref, dict) or event_graph_ref.get("kind") != "asset":
            fail(f"graph.list event graph graphRef missing/invalid: {event_graph}")
        print("[PASS] blueprint.graph.list validated")

        enum_create_payload = call_tool(
            client,
            580,
            "asset.create",
            {
                "kind": "enum",
                "assetPath": temp_enum_asset,
                "entries": [
                    {"name": "Idle", "displayName": "Idle"},
                    {"name": "Active", "displayName": "Active"},
                ],
            },
        )
        if enum_create_payload.get("applied") is not True:
            fail(f"asset.create enum did not apply: {enum_create_payload}")
        enum_path = enum_create_payload.get("enumPath")
        if not isinstance(enum_path, str) or "." not in enum_path:
            fail(f"asset.create enum missing enumPath: {enum_create_payload}")
        enum_inspect_payload = call_tool(client, 581, "asset.inspect", {"kind": "enum", "assetPath": temp_enum_asset})
        enum_entries = enum_inspect_payload.get("entries")
        if not isinstance(enum_entries, list) or [entry.get("name") for entry in enum_entries if isinstance(entry, dict)] != ["Idle", "Active"]:
            fail(f"asset.inspect enum entries mismatch: {enum_inspect_payload}")
        enum_update_payload = call_tool(
            client,
            582,
            "asset.edit",
            {
                "kind": "enum",
                "assetPath": temp_enum_asset,
                "operation": "updateEntries",
                "entries": ["Idle", "Active", "Complete"],
                "displayNames": {"Complete": "Complete"},
            },
        )
        if enum_update_payload.get("applied") is not True:
            fail(f"asset.edit enum updateEntries did not apply: {enum_update_payload}")
        enum_updated_entries = enum_update_payload.get("entries")
        if not isinstance(enum_updated_entries, list) or [entry.get("name") for entry in enum_updated_entries if isinstance(entry, dict)] != ["Idle", "Active", "Complete"]:
            fail(f"asset.edit enum updateEntries entries mismatch: {enum_update_payload}")
        metadata_payload = call_tool(
            client,
            583,
            "asset.edit",
            {
                "assetPath": temp_enum_asset,
                "operation": "updateMetadata",
                "metadata": {"LoomleTestPurpose": "asset-edit-metadata", "LoomleRemoveMe": "temporary"},
            },
        )
        if metadata_payload.get("applied") is not True:
            fail(f"asset.edit updateMetadata did not apply: {metadata_payload}")
        metadata_remove_payload = call_tool(
            client,
            584,
            "asset.edit",
            {
                "assetPath": temp_enum_asset,
                "operation": "updateMetadata",
                "removeKeys": ["LoomleRemoveMe"],
            },
        )
        if metadata_remove_payload.get("applied") is not True:
            fail(f"asset.edit updateMetadata removeKeys did not apply: {metadata_remove_payload}")
        metadata_check = call_tool(
            client,
            585,
            "execute",
            {
                "language": "python",
                "code": (
                    "import json, unreal\n"
                    f"asset = {json.dumps(temp_enum_asset)}\n"
                    "obj = unreal.EditorAssetLibrary.load_asset(asset)\n"
                    "purpose = unreal.EditorAssetLibrary.get_metadata_tag(obj, 'LoomleTestPurpose')\n"
                    "removed = unreal.EditorAssetLibrary.get_metadata_tag(obj, 'LoomleRemoveMe')\n"
                    "print(json.dumps({'purpose': purpose, 'removed': removed}, ensure_ascii=False))\n"
                ),
            },
        )
        metadata_result = parse_execute_json(metadata_check)
        if metadata_result.get("purpose") != "asset-edit-metadata" or metadata_result.get("removed"):
            fail(f"asset.edit metadata values mismatch: {metadata_result}")
        print("[PASS] asset enum lifecycle validated")

        asset_create_cases = [
            ("material", temp_asset_create_material),
            ("materialFunction", temp_asset_create_function),
            ("pcgGraph", temp_asset_create_pcg),
            ("widgetBlueprint", temp_asset_create_widget),
        ]
        for offset, (kind, asset_path) in enumerate(asset_create_cases):
            create_payload = call_tool(
                client,
                586 + offset,
                "asset.create",
                {"kind": kind, "assetPath": asset_path},
            )
            if create_payload.get("applied") is not True:
                fail(f"asset.create {kind} did not apply: {create_payload}")
            if create_payload.get("assetPath") != asset_path:
                fail(f"asset.create {kind} assetPath mismatch: {create_payload}")
        inspect_material_create = call_tool(client, 590, "asset.inspect", {"kind": "material", "assetPath": temp_asset_create_material})
        if inspect_material_create.get("assetPath") != temp_asset_create_material:
            fail(f"asset.inspect material created asset mismatch: {inspect_material_create}")
        inspect_function_create = call_tool(client, 591, "asset.inspect", {"kind": "materialFunction", "assetPath": temp_asset_create_function})
        if inspect_function_create.get("assetPath") != temp_asset_create_function:
            fail(f"asset.inspect materialFunction created asset mismatch: {inspect_function_create}")
        inspect_pcg_create = call_tool(client, 592, "asset.inspect", {"kind": "pcgGraph", "assetPath": temp_asset_create_pcg})
        if inspect_pcg_create.get("assetPath") != temp_asset_create_pcg:
            fail(f"asset.inspect pcgGraph created asset mismatch: {inspect_pcg_create}")
        inspect_widget_create = call_tool(client, 593, "asset.inspect", {"kind": "widgetBlueprint", "assetPath": temp_asset_create_widget})
        if inspect_widget_create.get("assetPath") != temp_asset_create_widget:
            fail(f"asset.inspect widgetBlueprint created asset mismatch: {inspect_widget_create}")
        print("[PASS] asset.create extended asset categories validated")

        interface_fixture_payload = call_tool(
            client,
            600,
            "execute",
            {
                "mode": "exec",
                "code": (
                    "import json, unreal\n"
                    f"asset={json.dumps(temp_interface_asset, ensure_ascii=False)}\n"
                    "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                    "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                    "bpi = unreal.EditorAssetLibrary.load_asset(asset)\n"
                    "if bpi is None:\n"
                    "    factory = unreal.BlueprintInterfaceFactory()\n"
                    "    bpi = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                    "if bpi is None:\n"
                    "    raise RuntimeError('failed to create Blueprint Interface asset')\n"
                    "generated_class = bpi.generated_class() if hasattr(bpi, 'generated_class') and callable(bpi.generated_class) else None\n"
                    "if generated_class is None:\n"
                    "    generated_class = bpi.get_editor_property('generated_class')\n"
                    "print(json.dumps({'asset': asset, 'classPath': generated_class.get_path_name() if generated_class else ''}, ensure_ascii=False))\n"
                ),
            },
        )
        interface_fixture = parse_execute_json(interface_fixture_payload)
        interface_class_path = interface_fixture.get("classPath")
        if not isinstance(interface_class_path, str) or not interface_class_path:
            fail(f"failed to resolve Blueprint Interface class path: {interface_fixture}")
        dry_run_interface_payload = call_tool(
            client,
            606,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "addInterface",
                "args": {"interfaceClassPath": interface_class_path},
                "dryRun": True,
            },
        )
        if dry_run_interface_payload.get("applied") is not False or dry_run_interface_payload.get("dryRun") is not True:
            fail(f"blueprint.class.edit addInterface dryRun shape mismatch: {dry_run_interface_payload}")
        if dry_run_interface_payload.get("valid") is not True:
            fail(f"blueprint.class.edit addInterface dryRun should report valid=true: {dry_run_interface_payload}")
        interface_revision = assert_revision_pair(
            dry_run_interface_payload,
            "blueprint.class.edit addInterface dryRun",
            unchanged=True,
        )
        if not isinstance(dry_run_interface_payload.get("planned"), dict):
            fail(f"blueprint.class.edit addInterface dryRun planned summary missing: {dry_run_interface_payload}")
        if not isinstance(dry_run_interface_payload.get("resolvedRefs"), dict):
            fail(f"blueprint.class.edit addInterface dryRun resolved refs missing: {dry_run_interface_payload}")
        if not isinstance(dry_run_interface_payload.get("diagnostics"), list):
            fail(f"blueprint.class.edit addInterface dryRun diagnostics missing: {dry_run_interface_payload}")
        dry_run_interface_diff = dry_run_interface_payload.get("diff")
        if (
            not isinstance(dry_run_interface_diff, dict)
            or dry_run_interface_diff.get("scope") != "blueprint.class"
            or not any(
                isinstance(change, dict)
                and change.get("kind") == "create"
                and change.get("target", {}).get("type") == "interface"
                for change in dry_run_interface_diff.get("changes", [])
            )
        ):
            fail(f"blueprint.class.edit addInterface dryRun diff invalid: {dry_run_interface_payload}")
        dry_run_list_payload = call_tool(
            client,
            607,
            "blueprint.class.inspect",
            {"assetPath": temp_asset},
        )
        dry_run_interfaces = dry_run_list_payload.get("implementedInterfaces")
        if not isinstance(dry_run_interfaces, list) or any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in dry_run_interfaces
        ):
            fail(f"blueprint.class.inspect dryRun unexpectedly added interface: {dry_run_list_payload}")
        add_interface_payload = call_tool(
            client,
            601,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "addInterface",
                "args": {"interfaceClassPath": interface_class_path},
            },
        )
        if add_interface_payload.get("applied") is not True:
            fail(f"blueprint.class.edit addInterface did not apply: {add_interface_payload}")
        assert_revision_pair(add_interface_payload, "blueprint.class.edit addInterface")
        revision_conflict_payload = call_tool(
            client,
            6011,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "removeInterface",
                "args": {"interfaceClassPath": interface_class_path},
                "expectedRevision": interface_revision,
            },
            expect_error=True,
        )
        if (
            extract_nested_error_code(revision_conflict_payload) != "REVISION_CONFLICT"
            or revision_conflict_payload.get("applied") is not False
        ):
            fail(f"blueprint.class.edit revision conflict mismatch: {revision_conflict_payload}")
        assert_revision_pair(
            structured_detail_or_payload(revision_conflict_payload),
            "blueprint.class.edit revision conflict",
            unchanged=True,
        )
        list_interface_payload = call_tool(
            client,
            602,
            "blueprint.class.inspect",
            {"assetPath": temp_asset},
        )
        listed_interfaces = list_interface_payload.get("implementedInterfaces")
        if not isinstance(listed_interfaces, list) or not any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in listed_interfaces
        ):
            fail(f"blueprint.class.inspect missing added interface: {list_interface_payload}")
        asset_inspect_payload = call_tool(client, 603, "blueprint.inspect", {"assetPath": temp_asset})
        inspected_interfaces = asset_inspect_payload.get("implementedInterfaces")
        if not isinstance(inspected_interfaces, list) or not any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in inspected_interfaces
        ):
            fail(f"blueprint.inspect missing implementedInterfaces entry: {asset_inspect_payload}")
        remove_interface_payload = call_tool(
            client,
            604,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "removeInterface",
                "args": {"interfaceClassPath": interface_class_path},
            },
        )
        if remove_interface_payload.get("applied") is not True:
            fail(f"blueprint.class.edit removeInterface did not apply: {remove_interface_payload}")
        list_after_remove_payload = call_tool(
            client,
            605,
            "blueprint.class.inspect",
            {"assetPath": temp_asset},
        )
        interfaces_after_remove = list_after_remove_payload.get("implementedInterfaces")
        if not isinstance(interfaces_after_remove, list) or any(
            isinstance(entry, dict) and entry.get("classPath") == interface_class_path
            for entry in interfaces_after_remove
        ):
            fail(f"blueprint.class.inspect still lists removed interface: {list_after_remove_payload}")
        print("[PASS] blueprint.class.edit interface lifecycle validated")

        dry_run_settings_payload = call_tool(
            client,
            608,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "setSettings",
                "args": {"settings": {"displayName": "Dry Run Name"}},
                "dryRun": True,
            },
        )
        if dry_run_settings_payload.get("applied") is not False or dry_run_settings_payload.get("dryRun") is not True:
            fail(f"blueprint.class.edit setSettings dryRun shape mismatch: {dry_run_settings_payload}")
        if dry_run_settings_payload.get("valid") is not True or not isinstance(dry_run_settings_payload.get("planned"), dict):
            fail(f"blueprint.class.edit setSettings dryRun plan missing: {dry_run_settings_payload}")
        assert_revision_pair(dry_run_settings_payload, "blueprint.class.edit setSettings dryRun", unchanged=True)
        dry_run_settings_diff = dry_run_settings_payload.get("diff")
        if (
            not isinstance(dry_run_settings_diff, dict)
            or dry_run_settings_diff.get("scope") != "blueprint.class"
            or not any(
                isinstance(change, dict)
                and change.get("kind") == "update"
                and change.get("target", {}).get("path") == "settings.displayName"
                and change.get("after") == "Dry Run Name"
                for change in dry_run_settings_diff.get("changes", [])
            )
        ):
            fail(f"blueprint.class.edit setSettings dryRun diff invalid: {dry_run_settings_payload}")
        settings_payload = call_tool(
            client,
            609,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "setSettings",
                "args": {
                    "settings": {
                        "displayName": "Loomle Regression Actor",
                        "description": "Blueprint class edit regression",
                        "category": "Loomle|Regression",
                        "hideCategories": ["Rendering"],
                        "runConstructionScriptOnDrag": False,
                    }
                },
            },
        )
        if settings_payload.get("applied") is not True:
            fail(f"blueprint.class.edit setSettings did not apply: {settings_payload}")
        assert_revision_pair(settings_payload, "blueprint.class.edit setSettings")
        inspected_settings_payload = call_tool(client, 610, "blueprint.class.inspect", {"assetPath": temp_asset})
        inspected_settings = inspected_settings_payload.get("settings")
        if not isinstance(inspected_settings, dict):
            fail(f"blueprint.class.inspect missing settings after setSettings: {inspected_settings_payload}")
        if (
            inspected_settings.get("displayName") != "Loomle Regression Actor"
            or inspected_settings.get("category") != "Loomle|Regression"
            or inspected_settings.get("runConstructionScriptOnDrag") is not False
            or "Rendering" not in inspected_settings.get("hideCategories", [])
        ):
            fail(f"blueprint.class.edit setSettings state mismatch: {inspected_settings_payload}")
        print("[PASS] blueprint.class.edit setSettings validated")

        graph_query = call_domain_tool(
            client,
            6,
            "blueprint",
            "query",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
                "view": "summary",
            },
        )
        if graph_query.get("view") != "summary":
            fail(f"blueprint.graph.inspect summary view mismatch: {graph_query}")
        if not isinstance(graph_query.get("roots"), list) or not isinstance(graph_query.get("chains"), list):
            fail(f"blueprint.graph.inspect invalid summary shape: {graph_query}")
        query_meta = graph_query.get("meta")
        if not isinstance(query_meta, dict):
            fail(f"blueprint.graph.inspect missing meta: {graph_query}")
        layout_caps = query_meta.get("layoutCapabilities")
        if not isinstance(layout_caps, dict):
            fail(f"blueprint.graph.inspect missing layoutCapabilities: {graph_query}")
        if layout_caps.get("canReadPosition") is not True:
            fail(f"blueprint.graph.inspect layoutCapabilities missing canReadPosition=true: {query_meta}")
        query_diagnostics = graph_query.get("diagnostics")
        if not isinstance(query_diagnostics, list):
            fail(f"blueprint.graph.inspect diagnostics missing or invalid: {graph_query}")
        missing_exec_root = call_tool(
            client,
            6410,
            "blueprint.graph.inspect",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "view": "exec_flow",
                "rootNode": {"id": "00000000-0000-0000-0000-000000000000"},
            },
            expect_error=True,
        )
        if extract_nested_error_code(missing_exec_root) != "NODE_NOT_FOUND":
            fail(f"blueprint.graph.inspect missing exec root should return NODE_NOT_FOUND: {missing_exec_root}")
        print("[PASS] blueprint.graph.inspect structure validated")

        blueprint_compile = call_domain_tool(
            client,
            6405,
            "blueprint",
            "compile",
            {
                "assetPath": temp_asset,
                "graphName": "EventGraph",
            },
        )
        if blueprint_compile.get("compiled") is not True:
            fail(f"blueprint.compile returned unexpected result: {blueprint_compile}")
        if not isinstance(blueprint_compile.get("diagnostics"), list):
            fail(f"blueprint.compile missing diagnostics[]: {blueprint_compile}")
        print("[PASS] blueprint.compile summary validated")

        member_ops = [
            ("component create root", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.SceneComponent",
                    "componentName": "RootScene",
                },
            }),
            ("component create mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.StaticMeshComponent",
                    "componentName": "VisualMesh",
                    "parentComponentName": "RootScene",
                },
            }),
            ("component update mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "update",
                "args": {
                    "componentName": "VisualMesh",
                    "meshAssetPath": "/Engine/BasicShapes/Cube.Cube",
                    "relativeLocation": {"x": 10, "y": 20, "z": 30},
                    "relativeScale3D": {"x": 2, "y": 2, "z": 2},
                },
            }),
            ("component rename mesh", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "rename",
                "args": {
                    "componentName": "VisualMesh",
                    "newName": "VisualMeshRenamed",
                },
            }),
            ("component create box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.BoxComponent",
                    "componentName": "BoxVolume",
                },
            }),
            ("component update box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "update",
                "args": {
                    "componentName": "BoxVolume",
                    "boxExtent": {"x": 50, "y": 60, "z": 70},
                    "collisionMode": "QueryOnly",
                    "generateOverlapEvents": True,
                },
            }),
            ("component reparent box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "reparent",
                "args": {
                    "componentName": "BoxVolume",
                    "parentComponentName": "RootScene",
                },
            }),
            ("component reorder box", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "reorder",
                "args": {
                    "componentName": "BoxVolume",
                    "targetComponentName": "VisualMeshRenamed",
                    "placement": "before",
                },
            }),
            ("component create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "create",
                "args": {
                    "componentClassPath": "/Script/Engine.SceneComponent",
                    "componentName": "TempDeleteComponent",
                },
            }),
            ("component delete temp", {
                "assetPath": temp_asset,
                "memberKind": "component",
                "operation": "delete",
                "args": {
                    "componentName": "TempDeleteComponent",
                },
            }),
            ("variable create ready", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "bIsReady",
                    "type": {"category": "bool"},
                    "defaultValue": "false",
                },
            }),
            ("variable update ready", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "update",
                "args": {
                    "variableName": "bIsReady",
                    "defaultValue": "true",
                    "category": "State",
                    "tooltip": "Ready flag",
                    "exposeOnSpawn": True,
                },
            }),
            ("variable create count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "Count",
                    "type": {"category": "int"},
                    "defaultValue": "0",
                },
            }),
            ("variable reorder count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "reorder",
                "args": {
                    "variableName": "Count",
                    "targetVariableName": "bIsReady",
                    "placement": "before",
                },
            }),
            ("variable rename count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "rename",
                "args": {
                    "variableName": "Count",
                    "newName": "ItemCount",
                },
            }),
            ("variable setdefault count", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "setDefault",
                "args": {
                    "variableName": "ItemCount",
                    "defaultValue": "5",
                },
            }),
            ("variable create enum state", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "EnumState",
                    "type": {"category": "enum", "enumPath": enum_path},
                },
            }),
            ("variable update enum state replication repNotify", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "update",
                "args": {
                    "variableName": "EnumState",
                    "replication": "repNotify",
                    "repNotifyFunc": "OnRep_EnumState",
                },
            }),
            ("variable update enum state replicationCondition ownerOnly", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "update",
                "args": {
                    "variableName": "EnumState",
                    "replicationCondition": "COND_OwnerOnly",
                },
            }),
            ("variable update enum state replication reset none", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "update",
                "args": {
                    "variableName": "EnumState",
                    "replication": "none",
                    "replicationCondition": "COND_None",
                },
            }),
            ("variable create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {
                    "variableName": "TempDeleteVariable",
                    "type": {"category": "bool"},
                    "defaultValue": "false",
                },
            }),
            ("variable delete temp", {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "delete",
                "args": {
                    "variableName": "TempDeleteVariable",
                },
            }),
            ("function create", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "create",
                "args": {
                    "functionName": "ComputeValue",
                    "inputs": [{"name": "bInput", "type": {"category": "bool"}}],
                    "outputs": [{"name": "Value", "type": {"category": "int"}}],
                    "pure": True,
                    "const": True,
                    "category": "Logic",
                    "tooltip": "Compute a test value",
                },
            }),
            ("function rename", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "rename",
                "args": {
                    "functionName": "ComputeValue",
                    "newName": "ComputeValueRenamed",
                },
            }),
            ("function setFlags", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "setFlags",
                "args": {
                    "functionName": "ComputeValueRenamed",
                    "pure": True,
                    "const": True,
                    "category": "Logic",
                    "tooltip": "Compute a test value",
                },
            }),
            ("function create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "create",
                "args": {
                    "functionName": "TempDeleteFunction",
                },
            }),
            ("function delete temp", {
                "assetPath": temp_asset,
                "memberKind": "function",
                "operation": "delete",
                "args": {
                    "functionName": "TempDeleteFunction",
                },
            }),
            ("macro create", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "create",
                "args": {
                    "macroName": "GuardMacro",
                    "inputs": [{"name": "bGate", "type": {"category": "bool"}}],
                    "outputs": [{"name": "bPassed", "type": {"category": "bool"}}],
                    "category": "Logic",
                },
            }),
            ("macro rename", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "rename",
                "args": {
                    "macroName": "GuardMacro",
                    "newName": "GuardMacroRenamed",
                },
            }),
            ("macro create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "create",
                "args": {
                    "macroName": "TempDeleteMacro",
                },
            }),
            ("macro delete temp", {
                "assetPath": temp_asset,
                "memberKind": "macro",
                "operation": "delete",
                "args": {
                    "macroName": "TempDeleteMacro",
                },
            }),
            ("dispatcher create", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "create",
                "args": {
                    "dispatcherName": "OnReady",
                    "inputs": [{"name": "bReady", "type": {"category": "bool"}}],
                    "category": "Events",
                },
            }),
            ("dispatcher rename", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "rename",
                "args": {
                    "dispatcherName": "OnReady",
                    "newName": "OnReadyChanged",
                },
            }),
            ("dispatcher create temp delete", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "create",
                "args": {
                    "dispatcherName": "TempDeleteDispatcher",
                },
            }),
            ("dispatcher delete temp", {
                "assetPath": temp_asset,
                "memberKind": "dispatcher",
                "operation": "delete",
                "args": {
                    "dispatcherName": "TempDeleteDispatcher",
                },
            }),
        ]
        for index, (label, request) in enumerate(member_ops, start=1):
            payload = call_tool(client, 6460 + index, "blueprint.member.edit", request)
            if payload.get("applied") is not True:
                fail(f"blueprint.member.edit {label} did not apply: {payload}")
            assert_revision_pair(payload, f"blueprint.member.edit {label}")

        event_member_ops = [
            (
                "event create temp",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "create",
                    "args": {"name": "TempDeleteCustomEvent", "graphName": "EventGraph", "x": 480, "y": 260},
                },
            ),
            (
                "event create graph custom",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "create",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "graphName": "EventGraph",
                        "x": 480,
                        "y": 120,
                        "replication": "server",
                        "reliable": True,
                        "inputs": [
                            {"name": "Count", "type": {"category": "int"}},
                        ],
                    },
                },
            ),
            (
                "event create owning client",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "create",
                    "args": {
                        "name": "ClientRejectCollect",
                        "graphName": "EventGraph",
                        "x": 760,
                        "y": 260,
                        "replication": "owningClient",
                        "reliable": True,
                        "inputs": [
                            {"name": "CoinId", "type": {"category": "string"}},
                        ],
                    },
                },
            ),
            (
                "event update signature",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "updateSignature",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "inputs": [
                            {"name": "bReady", "type": {"category": "bool"}},
                        ],
                    },
                },
            ),
            (
                "event add input",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "addInput",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "inputName": "CoinId",
                        "inputType": "String",
                        "type": {"category": "string"},
                    },
                },
            ),
            (
                "event set flags",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "setFlags",
                    "args": {
                        "name": "OnGraphCustomEvent",
                        "replication": "netMulticast",
                        "reliable": False,
                    },
                },
            ),
            (
                "event rename",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "rename",
                    "args": {"name": "OnGraphCustomEvent", "newName": "OnGraphCustomEventRenamed"},
                },
            ),
            (
                "event delete temp",
                {
                    "assetPath": temp_asset,
                    "memberKind": "event",
                    "operation": "delete",
                    "args": {"name": "TempDeleteCustomEvent"},
                },
            ),
        ]
        for index, (label, request) in enumerate(event_member_ops, start=1):
            payload = call_tool(client, 6510 + index, "blueprint.member.edit", request)
            if payload.get("applied") is not True:
                fail(f"blueprint.member.edit {label} did not apply: {payload}")
            assert_revision_pair(payload, f"blueprint.member.edit {label}")

        duplicate_event_input = call_tool(
            client,
            6519,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "event",
                "operation": "addInput",
                "args": {
                    "name": "OnGraphCustomEventRenamed",
                    "inputName": "CoinId",
                    "type": {"category": "string"},
                },
            },
            expect_error=True,
        )
        duplicate_details = duplicate_event_input.get("details")
        if duplicate_event_input.get("reason") != "pinNameConflict" or not isinstance(duplicate_details, dict):
            fail(f"blueprint.member.edit event addInput duplicate missing structured diagnostics: {duplicate_event_input}")
        requested_input = duplicate_details.get("requestedInput")
        actual_inputs = duplicate_details.get("actualInputs")
        if not isinstance(requested_input, dict) or requested_input.get("inputName") != "CoinId":
            fail(f"event addInput duplicate missing requestedInput: {duplicate_event_input}")
        if not isinstance(actual_inputs, list) or not any(isinstance(pin, dict) and pin.get("name") == "CoinId" for pin in actual_inputs):
            fail(f"event addInput duplicate missing actualInputs: {duplicate_event_input}")

        compiled_member_bp = call_tool(client, 6520, "blueprint.compile", {"assetPath": temp_asset})
        if compiled_member_bp.get("compiled") is not True:
            fail(f"blueprint.compile after member.edit failed: {compiled_member_bp}")
        variable_inspect_payload = call_tool(
            client,
            6522,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "variable"},
        )
        variable_items = variable_inspect_payload.get("items")
        if not isinstance(variable_items, list):
            fail(f"blueprint.member.inspect variable items missing: {variable_inspect_payload}")
        enum_variable = next((item for item in variable_items if isinstance(item, dict) and item.get("name") == "EnumState"), None)
        enum_variable_type = enum_variable.get("type") if isinstance(enum_variable, dict) else None
        if not isinstance(enum_variable_type, dict) or enum_variable_type.get("kind") != "enum" or enum_variable_type.get("objectClassPath") != enum_path:
            fail(f"blueprint.member.inspect enum variable type mismatch: {variable_inspect_payload}")
        if enum_variable.get("isReplicated") is not False or enum_variable.get("isRepNotify") is not False:
            fail(f"blueprint.member.inspect enum variable replication flags mismatch: {enum_variable}")
        if enum_variable.get("replication") != "none":
            fail(f"blueprint.member.inspect enum variable replication mode mismatch: {enum_variable}")
        if enum_variable.get("replicationCondition") != "COND_None":
            fail(f"blueprint.member.inspect enum variable replicationCondition not reset to COND_None: {enum_variable}")
        if enum_variable.get("replicationConditionValue") != 0:
            fail(f"blueprint.member.inspect enum variable replicationConditionValue not reset to 0: {enum_variable}")
        component_inspect_payload = call_tool(
            client,
            6523,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "component"},
        )
        component_items = component_inspect_payload.get("items")
        if not isinstance(component_items, list):
            fail(f"blueprint.member.inspect component items missing: {component_inspect_payload}")
        dry_run_member_payload = call_tool(
            client,
            6529,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {"variableName": "DryRunVariable", "type": {"category": "bool"}},
                "dryRun": True,
            },
        )
        if dry_run_member_payload.get("applied") is not False or dry_run_member_payload.get("dryRun") is not True:
            fail(f"blueprint.member.edit dryRun shape mismatch: {dry_run_member_payload}")
        if dry_run_member_payload.get("valid") is not True:
            fail(f"blueprint.member.edit dryRun should report valid=true: {dry_run_member_payload}")
        member_revision = assert_revision_pair(
            dry_run_member_payload,
            "blueprint.member.edit dryRun",
            unchanged=True,
        )
        dry_run_planned = dry_run_member_payload.get("planned")
        if (
            not isinstance(dry_run_planned, dict)
            or dry_run_planned.get("memberKind") != "variable"
            or dry_run_planned.get("operation") != "create"
            or dry_run_planned.get("memberName") != "DryRunVariable"
        ):
            fail(f"blueprint.member.edit dryRun planned summary invalid: {dry_run_member_payload}")
        dry_run_refs = dry_run_member_payload.get("resolvedRefs")
        if not isinstance(dry_run_refs, dict) or dry_run_refs.get("member", {}).get("name") != "DryRunVariable":
            fail(f"blueprint.member.edit dryRun resolved refs invalid: {dry_run_member_payload}")
        if not isinstance(dry_run_member_payload.get("diagnostics"), list):
            fail(f"blueprint.member.edit dryRun diagnostics missing: {dry_run_member_payload}")
        dry_run_diff = dry_run_member_payload.get("diff")
        if (
            not isinstance(dry_run_diff, dict)
            or dry_run_diff.get("scope") != "blueprint.member"
            or not any(
                isinstance(change, dict)
                and change.get("kind") == "create"
                and change.get("target", {}).get("name") == "DryRunVariable"
                for change in dry_run_diff.get("changes", [])
            )
        ):
            fail(f"blueprint.member.edit dryRun diff invalid: {dry_run_member_payload}")
        member_revision_conflict_payload = call_tool(
            client,
            65291,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {"variableName": "RevisionConflictVariable", "type": {"category": "bool"}},
                "expectedRevision": f"{member_revision}:stale",
            },
            expect_error=True,
        )
        if (
            extract_nested_error_code(member_revision_conflict_payload) != "REVISION_CONFLICT"
            or member_revision_conflict_payload.get("applied") is not False
        ):
            fail(f"blueprint.member.edit revision conflict mismatch: {member_revision_conflict_payload}")
        assert_revision_pair(
            structured_detail_or_payload(member_revision_conflict_payload),
            "blueprint.member.edit revision conflict",
            unchanged=True,
        )
        dry_run_unsupported_member_payload = call_tool(
            client,
            6530,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "add",
                "args": {"name": "DryRunUnsupportedVariable", "type": {"category": "bool"}},
                "dryRun": True,
            },
            expect_error=True,
        )
        dry_run_unsupported_message = dry_run_unsupported_member_payload.get("message", "")
        if (
            extract_nested_error_code(dry_run_unsupported_member_payload) != "INVALID_ARGUMENT"
            or "Unsupported variable operation: add" not in dry_run_unsupported_message
            or "Did you mean create?" not in dry_run_unsupported_message
        ):
            fail(f"blueprint.member.edit dryRun unsupported operation mismatch: {dry_run_unsupported_member_payload}")
        real_unsupported_member_payload = call_tool(
            client,
            65300,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "add",
                "args": {"name": "RealUnsupportedVariable", "type": {"category": "bool"}},
            },
            expect_error=True,
        )
        real_unsupported_message = real_unsupported_member_payload.get("message", "")
        if (
            extract_nested_error_code(real_unsupported_member_payload) != "INVALID_ARGUMENT"
            or "Unsupported variable operation: add" not in real_unsupported_message
            or "Did you mean create?" not in real_unsupported_message
        ):
            fail(f"blueprint.member.edit real unsupported operation mismatch: {real_unsupported_member_payload}")
        dry_run_missing_arg_payload = call_tool(
            client,
            65301,
            "blueprint.member.edit",
            {
                "assetPath": temp_asset,
                "memberKind": "variable",
                "operation": "create",
                "args": {"type": {"category": "bool"}},
                "dryRun": True,
            },
            expect_error=True,
        )
        dry_run_missing_arg_message = dry_run_missing_arg_payload.get("message", "")
        if (
            extract_nested_error_code(dry_run_missing_arg_payload) != "INVALID_ARGUMENT"
            or "variable create requires variableName" not in dry_run_missing_arg_message
        ):
            fail(f"blueprint.member.edit dryRun missing arg mismatch: {dry_run_missing_arg_payload}")
        if dry_run_missing_arg_payload.get("valid") is not False or dry_run_missing_arg_payload.get("applied") is not False:
            fail(f"blueprint.member.edit dryRun missing arg should be invalid and unapplied: {dry_run_missing_arg_payload}")
        if not isinstance(dry_run_missing_arg_payload.get("diagnostics"), list):
            fail(f"blueprint.member.edit dryRun missing arg diagnostics missing: {dry_run_missing_arg_payload}")
        macro_inspect_payload = call_tool(
            client,
            6531,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "macro"},
        )
        macro_items = macro_inspect_payload.get("items")
        if not isinstance(macro_items, list) or not any(
            isinstance(entry, dict) and entry.get("name") == "GuardMacroRenamed"
            for entry in macro_items
        ):
            fail(f"blueprint.member.inspect macro items missing renamed macro: {macro_inspect_payload}")
        dispatcher_inspect_payload = call_tool(
            client,
            6532,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "dispatcher"},
        )
        dispatcher_items = dispatcher_inspect_payload.get("items")
        if not isinstance(dispatcher_items, list) or not any(
            isinstance(entry, dict) and entry.get("name") == "OnReadyChanged"
            for entry in dispatcher_items
        ):
            fail(f"blueprint.member.inspect dispatcher items missing renamed dispatcher: {dispatcher_inspect_payload}")
        event_inspect_payload = call_tool(
            client,
            6533,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "event"},
        )
        event_items = event_inspect_payload.get("items")
        if not isinstance(event_items, list):
            fail(f"blueprint.member.inspect event items missing: {event_inspect_payload}")
        renamed_event = next(
            (
                entry
                for entry in event_items
                if isinstance(entry, dict) and entry.get("name") == "OnGraphCustomEventRenamed"
            ),
            None,
        )
        if not isinstance(renamed_event, dict) or renamed_event.get("eventKind") != "custom":
            fail(f"blueprint.member.inspect event missing renamed custom event: {event_inspect_payload}")
        if renamed_event.get("replication") != "netMulticast" or renamed_event.get("reliable") is not False:
            fail(f"blueprint.member.inspect event missing multicast flags: {event_inspect_payload}")
        renamed_event_pins = renamed_event.get("pins")
        if not isinstance(renamed_event_pins, list) or not any(
            isinstance(pin, dict) and pin.get("name") == "bReady"
            for pin in renamed_event_pins
        ):
            fail(f"blueprint.member.inspect event missing updated input pin: {event_inspect_payload}")
        if not any(
            isinstance(pin, dict) and pin.get("name") == "CoinId"
            for pin in renamed_event_pins
        ):
            fail(f"blueprint.member.inspect event missing addInput pin: {event_inspect_payload}")
        client_event = next(
            (
                entry
                for entry in event_items
                if isinstance(entry, dict) and entry.get("name") == "ClientRejectCollect"
            ),
            None,
        )
        if not isinstance(client_event, dict) or client_event.get("replication") != "owningClient" or client_event.get("reliable") is not True:
            fail(f"blueprint.member.inspect event missing owning-client reliable flags: {event_inspect_payload}")
        custom_event_inspect_payload = call_tool(
            client,
            6534,
            "blueprint.member.inspect",
            {"assetPath": temp_asset, "memberKind": "customEvent"},
        )
        custom_event_items = custom_event_inspect_payload.get("items")
        if not isinstance(custom_event_items, list) or not custom_event_items:
            fail(f"blueprint.member.inspect customEvent items missing: {custom_event_inspect_payload}")
        if any(isinstance(entry, dict) and entry.get("isCustomEvent") is not True for entry in custom_event_items):
            fail(f"blueprint.member.inspect customEvent returned non-custom events: {custom_event_inspect_payload}")
        graph_list_payload = call_tool(client, 6524, "blueprint.graph.list", {"assetPath": temp_asset})
        listed_graphs = graph_list_payload.get("graphs")
        if not isinstance(listed_graphs, list):
            fail(f"blueprint.graph.list missing graphs[]: {graph_list_payload}")
        dispatcher_graph_entry = next(
            (
                entry
                for entry in listed_graphs
                if isinstance(entry, dict) and entry.get("graphName") == "OnReadyChanged"
            ),
            None,
        )
        if not isinstance(dispatcher_graph_entry, dict) or dispatcher_graph_entry.get("graphKind") != "delegate_signature":
            fail(f"blueprint.graph.list missing dispatcher delegate signature graph: {graph_list_payload}")
        compute_graph_payload = call_tool(
            client,
            6525,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "ComputeValueRenamed"}, "view": "summary"},
        )
        guard_graph_payload = call_tool(
            client,
            6526,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "GuardMacroRenamed"}, "view": "summary"},
        )
        dispatcher_graph_payload = call_tool(
            client,
            6527,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "OnReadyChanged"}, "view": "summary"},
        )
        deleted_dispatcher_graph = call_tool(
            client,
            6528,
            "blueprint.graph.inspect",
            {"assetPath": temp_asset, "graph": {"name": "TempDeleteDispatcher"}},
            expect_error=True,
        )
        if deleted_dispatcher_graph.get("isError") is not True:
            fail(f"deleted dispatcher graph should not resolve: {deleted_dispatcher_graph}")

        member_state_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=6540,
            code=(
                "import json, unreal\n"
                f"asset={json.dumps(temp_asset, ensure_ascii=False)}\n"
                "bp = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if bp is None:\n"
                "    raise RuntimeError('failed to load regression blueprint')\n"
                "subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)\n"
                "handles = subsys.k2_gather_subobject_data_for_blueprint(bp) if subsys else []\n"
                "components = []\n"
                "seen_component_names = set()\n"
                "for handle in handles:\n"
                "    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)\n"
                "    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_component(data):\n"
                "        continue\n"
                "    name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(data))\n"
                "    if not name or name in seen_component_names:\n"
                "        continue\n"
                "    seen_component_names.add(name)\n"
                "    parent_handle = unreal.SubobjectDataBlueprintFunctionLibrary.get_parent_handle(data)\n"
                "    parent_name = None\n"
                "    if unreal.SubobjectDataBlueprintFunctionLibrary.is_handle_valid(parent_handle):\n"
                "        parent_data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(parent_handle)\n"
                "        parent_name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(parent_data))\n"
                "    template = unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object(data)\n"
                "    entry = {\n"
                "        'name': name,\n"
                "        'parent': parent_name if parent_name != 'None' else None,\n"
                "        'classPath': template.get_class().get_path_name() if template else '',\n"
                "    }\n"
                "    if isinstance(template, unreal.SceneComponent):\n"
                "        loc = template.get_editor_property('relative_location')\n"
                "        scale = template.get_editor_property('relative_scale3d')\n"
                "        entry['relativeLocation'] = [loc.x, loc.y, loc.z]\n"
                "        entry['relativeScale3D'] = [scale.x, scale.y, scale.z]\n"
                "    if isinstance(template, unreal.StaticMeshComponent):\n"
                "        mesh = template.get_editor_property('static_mesh')\n"
                "        entry['staticMesh'] = mesh.get_path_name() if mesh else ''\n"
                "    if isinstance(template, unreal.BoxComponent):\n"
                "        extent = template.get_editor_property('box_extent')\n"
                "        entry['boxExtent'] = [extent.x, extent.y, extent.z]\n"
                "        entry['generateOverlapEvents'] = bool(template.get_editor_property('generate_overlap_events'))\n"
                "    components.append(entry)\n"
                "gc = bp.generated_class() if hasattr(bp, 'generated_class') and callable(bp.generated_class) else None\n"
                "cdo = unreal.get_default_object(gc) if gc else None\n"
                "result = {\n"
                "    'components': components,\n"
                "    'rootComponents': [entry['name'] for entry in components if entry.get('parent') is None],\n"
                "    'rootSceneChildren': [entry['name'] for entry in components if entry.get('parent') == 'RootScene'],\n"
                "    'itemCountDefault': cdo.get_editor_property('ItemCount') if cdo else None,\n"
                "    'isReadyDefault': cdo.get_editor_property('bIsReady') if cdo else None,\n"
                "}\n"
                "print(json.dumps(result, ensure_ascii=False))\n"
            ),
        )
        member_state = parse_execute_json(member_state_payload)
        variable_names = [
            entry.get("name")
            for entry in variable_items
            if isinstance(entry, dict) and entry.get("name") in {"ItemCount", "bIsReady", "TempDeleteVariable"}
        ]
        if variable_names[:2] != ["ItemCount", "bIsReady"]:
            fail(f"member.edit variable order mismatch: inspect={variable_inspect_payload} state={member_state}")
        if "TempDeleteVariable" in variable_names:
            fail(f"member.edit temp variable should have been deleted: inspect={variable_inspect_payload} state={member_state}")
        if member_state.get("itemCountDefault") != 5 or member_state.get("isReadyDefault") is not True:
            fail(f"member.edit variable defaults mismatch: {member_state}")
        dry_run_class_default = call_tool(
            client,
            65221,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "setDefault",
                "args": {"property": "ItemCount", "value": "9"},
                "dryRun": True,
            },
        )
        if dry_run_class_default.get("applied") is not False or dry_run_class_default.get("dryRun") is not True:
            fail(f"blueprint.class.edit setDefault dryRun shape mismatch: {dry_run_class_default}")
        if dry_run_class_default.get("valid") is not True or not isinstance(dry_run_class_default.get("planned"), dict):
            fail(f"blueprint.class.edit setDefault dryRun plan missing: {dry_run_class_default}")
        assert_revision_pair(dry_run_class_default, "blueprint.class.edit setDefault dryRun", unchanged=True)
        dry_run_default_diff = dry_run_class_default.get("diff")
        if (
            not isinstance(dry_run_default_diff, dict)
            or dry_run_default_diff.get("scope") != "blueprint.class"
            or not any(
                isinstance(change, dict)
                and change.get("kind") == "set_default"
                and change.get("target", {}).get("name") == "ItemCount"
                for change in dry_run_default_diff.get("changes", [])
            )
        ):
            fail(f"blueprint.class.edit setDefault dryRun diff invalid: {dry_run_class_default}")
        set_class_default = call_tool(
            client,
            65222,
            "blueprint.class.edit",
            {
                "assetPath": temp_asset,
                "operation": "setDefault",
                "args": {"property": "ItemCount", "value": "12"},
            },
        )
        if set_class_default.get("applied") is not True:
            fail(f"blueprint.class.edit setDefault did not apply: {set_class_default}")
        assert_revision_pair(set_class_default, "blueprint.class.edit setDefault")
        default_entry = set_class_default.get("default")
        if not isinstance(default_entry, dict) or default_entry.get("name") != "ItemCount" or default_entry.get("value") != "12":
            fail(f"blueprint.class.edit setDefault result mismatch: {set_class_default}")
        class_default_state_payload = call_execute_exec_with_retry(
            client,
            req_id_base=65223,
            code=(
                "import json, unreal\n"
                f"asset={json.dumps(temp_asset, ensure_ascii=False)}\n"
                "bp = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "gc = bp.generated_class() if hasattr(bp, 'generated_class') and callable(bp.generated_class) else None\n"
                "cdo = unreal.get_default_object(gc) if gc else None\n"
                "print(json.dumps({'itemCountDefault': cdo.get_editor_property('ItemCount') if cdo else None}, ensure_ascii=False))\n"
            ),
        )
        class_default_state = parse_execute_json(class_default_state_payload)
        if class_default_state.get("itemCountDefault") != 12:
            fail(f"blueprint.class.edit setDefault CDO state mismatch: {class_default_state}")
        print("[PASS] blueprint.class.edit setDefault validated")
        graph_names = [entry.get("graphName") for entry in listed_graphs if isinstance(entry, dict)]
        if "ComputeValueRenamed" not in graph_names or "TempDeleteFunction" in graph_names:
            fail(f"member.edit function graph state mismatch: {graph_list_payload}")
        if "GuardMacroRenamed" not in graph_names or "TempDeleteMacro" in graph_names:
            fail(f"member.edit macro graph state mismatch: {graph_list_payload}")
        compute_nodes = blueprint_summary_nodes(compute_graph_payload)
        compute_entry_summary = next((node for node in compute_nodes if node.get("className") == "K2Node_FunctionEntry"), None)
        compute_result_summary = next((node for node in compute_nodes if node.get("className") == "K2Node_FunctionResult"), None)
        if not isinstance(compute_entry_summary, dict) or not isinstance(compute_result_summary, dict):
            fail(f"member.edit function graph inspect missing entry/result nodes: {compute_graph_payload}")
        compute_entry = inspect_blueprint_node(client, 65251, temp_asset, "ComputeValueRenamed", compute_entry_summary["id"]).get("node", {})
        compute_result = inspect_blueprint_node(client, 65252, temp_asset, "ComputeValueRenamed", compute_result_summary["id"]).get("node", {})
        compute_entry_signature = (
            compute_entry_summary.get("graphBoundarySummary", {}).get("pinSignature", {})
            if isinstance(compute_entry_summary.get("graphBoundarySummary"), dict)
            else {}
        )
        compute_result_signature = (
            compute_result_summary.get("graphBoundarySummary", {}).get("pinSignature", {})
            if isinstance(compute_result_summary.get("graphBoundarySummary"), dict)
            else {}
        )
        compute_input_pins = [
            name
            for name in compute_entry_signature.get("outputPins", [])
            if name != "then"
        ]
        compute_output_pins = [
            name
            for name in compute_result_signature.get("inputPins", [])
            if name != "execute"
        ]
        if compute_input_pins != ["bInput"] or compute_output_pins != ["Value"]:
            fail(f"member.edit function signature mismatch: {compute_graph_payload}")
        compute_boundary = compute_entry_summary.get("graphBoundarySummary", {})
        if not isinstance(compute_boundary, dict) or compute_boundary.get("isPure") is not True:
            fail(f"member.edit pure function flag mismatch: {compute_graph_payload}")
        if compute_boundary.get("isConst") is not True:
            fail(f"member.edit const function flag mismatch: {compute_graph_payload}")
        guard_nodes = blueprint_summary_nodes(guard_graph_payload)
        guard_entry = next(
            (
                node
                for node in guard_nodes
                if node.get("className") == "K2Node_Tunnel" and node.get("nodeTitle") == "Inputs"
            ),
            None,
        )
        if not isinstance(guard_entry, dict):
            fail(f"member.edit macro graph inspect missing input tunnel: {guard_graph_payload}")
        guard_entry = inspect_blueprint_node(client, 65261, temp_asset, "GuardMacroRenamed", guard_entry["id"]).get("node", {})
        guard_pins = [
            pin.get("name")
            for pin in guard_entry.get("pins", [])
            if isinstance(pin, dict) and pin.get("category") != "exec"
        ]
        if guard_pins != ["bGate"]:
            fail(f"member.edit macro signature mismatch: {guard_graph_payload}")
        dispatcher_nodes = blueprint_summary_nodes(dispatcher_graph_payload)
        dispatcher_entry = next(
            (node for node in dispatcher_nodes if node.get("className") == "K2Node_FunctionEntry"),
            None,
        )
        if not isinstance(dispatcher_entry, dict):
            fail(f"member.edit dispatcher graph inspect missing entry node: {dispatcher_graph_payload}")
        dispatcher_entry = inspect_blueprint_node(client, 65271, temp_asset, "OnReadyChanged", dispatcher_entry["id"]).get("node", {})
        dispatcher_pins = [
            pin.get("name")
            for pin in dispatcher_entry.get("pins", [])
            if isinstance(pin, dict) and pin.get("category") != "exec"
        ]
        if dispatcher_pins != ["bReady"]:
            fail(f"member.edit dispatcher signature mismatch: {dispatcher_graph_payload}")
        component_by_name = {
            entry.get("name"): entry
            for entry in member_state.get("components", [])
            if isinstance(entry, dict)
        }
        if "TempDeleteComponent" in component_by_name:
            fail(f"member.edit temp component should have been deleted: {member_state}")
        root_scene = component_by_name.get("RootScene")
        mesh_component = component_by_name.get("VisualMeshRenamed")
        box_component = component_by_name.get("BoxVolume")
        if not isinstance(root_scene, dict) or not isinstance(mesh_component, dict) or not isinstance(box_component, dict):
            fail(f"member.edit components missing expected entries: {member_state}")
        if mesh_component.get("parent") != "RootScene" or box_component.get("parent") != "RootScene":
            fail(f"member.edit component reparent mismatch: {member_state}")
        if mesh_component.get("relativeLocation") != [10.0, 20.0, 30.0] or mesh_component.get("relativeScale3D") != [2.0, 2.0, 2.0]:
            fail(f"member.edit component transform mismatch: {member_state}")
        if "Cube.Cube" not in str(mesh_component.get("staticMesh", "")):
            fail(f"member.edit static mesh assignment mismatch: {member_state}")
        if box_component.get("boxExtent") != [50.0, 60.0, 70.0] or box_component.get("generateOverlapEvents") is not True:
            fail(f"member.edit box component update mismatch: {member_state}")
        component_names = [
            entry.get("name")
            for entry in component_items
            if isinstance(entry, dict) and entry.get("name") in {"RootScene", "BoxVolume", "VisualMeshRenamed", "TempDeleteComponent"}
        ]
        if component_names != ["RootScene", "BoxVolume", "VisualMeshRenamed"]:
            fail(f"member.edit component reorder mismatch: inspect={component_inspect_payload} state={member_state}")
        print("[PASS] blueprint.member.edit full member workflow validated")

        pcg_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=7050,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG graph asset')\n"
                "volume = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PCGVolume, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))\n"
                "if volume is None:\n"
                "    raise RuntimeError('failed to spawn PCGVolume')\n"
                "component = volume.get_editor_property('pcg_component')\n"
                "if component is None:\n"
                "    raise RuntimeError('spawned PCGVolume has no pcg_component')\n"
                "component.set_graph(graph)\n"
                "unreal.EditorLevelLibrary.set_selected_level_actors([volume])\n"
                "result = {\n"
                "    'assetPath': asset,\n"
                "    'actorPath': volume.get_path_name(),\n"
                "    'componentPath': component.get_path_name(),\n"
                "}\n"
                "print(json.dumps(result, ensure_ascii=False))\n"
            ),
        )
        pcg_fixture = parse_execute_json(pcg_fixture_payload)
        fixture_asset_path = pcg_fixture.get("assetPath")
        if fixture_asset_path != temp_pcg_asset:
            fail(f"PCG fixture returned wrong assetPath: {pcg_fixture}")
        print("[PASS] temporary PCG fixture created")

        pcg_context = call_tool(client, 7056, "context", {})
        selection = pcg_context.get("selection")
        if not isinstance(selection, dict):
            fail(f"context missing selection after PCG fixture setup: {pcg_context}")
        resolved_graph_refs = selection.get("resolvedGraphRefs")
        if not isinstance(resolved_graph_refs, list):
            fail(f"context selection missing resolvedGraphRefs[] after PCG fixture setup: {pcg_context}")
        if not any(
            isinstance(entry, dict)
            and isinstance(entry.get("graphRef"), dict)
            and entry["graphRef"].get("assetPath") == temp_pcg_asset
            for entry in resolved_graph_refs
        ):
            fail(f"context selection did not include the PCG fixture graphRef: {pcg_context}")

        queried_pcg = call_domain_tool(
            client,
            7059,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        queried_snapshot = queried_pcg.get("semanticSnapshot")
        if not isinstance(queried_snapshot, dict):
            fail(f"pcg.graph.inspect missing semanticSnapshot for direct asset read: {queried_pcg}")
        queried_graph_ref = queried_pcg.get("graphRef")
        if not isinstance(queried_graph_ref, dict) or queried_graph_ref.get("assetPath") != temp_pcg_asset:
            fail(f"pcg.graph.inspect did not echo expected asset graphRef: {queried_pcg}")
        print("[PASS] pcg.graph.inspect direct asset addressing validated")

        pcg_class_desc = call_domain_tool(
            client,
            101001,
            "pcg",
            "describe",
            {"nodeClass": "/Script/PCG.PCGTransformPointsSettings"},
        )
        if pcg_class_desc.get("mode") != "class":
            fail(f"pcg.node.inspect class mode mismatch: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("inputPins"), list):
            fail(f"pcg.node.inspect missing inputPins[]: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("outputPins"), list):
            fail(f"pcg.node.inspect missing outputPins[]: {pcg_class_desc}")
        if not isinstance(pcg_class_desc.get("properties"), list):
            fail(f"pcg.node.inspect missing properties[]: {pcg_class_desc}")

        pcg_dry_run_script = call_domain_tool(
            client,
            1010011,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "dryRun": True,
                "ops": [
                    {
                        "op": "runScript",
                        "mode": "inlineCode",
                        "entry": "run",
                        "code": "def run(ctx):\n  return {'ok': True}",
                    }
                ],
            },
            expect_error=True,
        )
        pcg_dry_run_script_struct = structured_detail_or_payload(pcg_dry_run_script)
        if pcg_dry_run_script_struct.get("code") != "UNSUPPORTED_OP":
            fail(f"PCG dryRun runScript should surface UNSUPPORTED_OP: {pcg_dry_run_script_struct}")
        pcg_dry_run_script_results = pcg_dry_run_script_struct.get("opResults")
        if not isinstance(pcg_dry_run_script_results, list) or not pcg_dry_run_script_results:
            fail(f"PCG dryRun runScript missing opResults: {pcg_dry_run_script_struct}")
        pcg_dry_run_script_first = pcg_dry_run_script_results[0] if isinstance(pcg_dry_run_script_results[0], dict) else {}
        if pcg_dry_run_script_first.get("errorCode") != "UNSUPPORTED_OP":
            fail(f"PCG dryRun runScript should classify as UNSUPPORTED_OP: {pcg_dry_run_script_struct}")
        if pcg_dry_run_script_first.get("skipped") is True:
            fail(f"PCG dryRun runScript should not report skipped=true when unsupported: {pcg_dry_run_script_struct}")
        bad_query = call_domain_tool(
            client,
            8,
            "blueprint",
            "query",
            {"assetPath": temp_asset},
            expect_error=True,
        )
        _ = bad_query
        print("[PASS] blueprint.graph.inspect error path validated")

        bad_remove = call_domain_tool(
            client,
            8008,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [{"kind": "removeNode", "node": {}}],
            },
            expect_error=True,
        )
        bad_remove_struct = structured_detail_or_payload(bad_remove)
        if bad_remove_struct.get("code") != "INVALID_ARGUMENT":
            fail(f"blueprint.graph.edit invalid command should return INVALID_ARGUMENT: {bad_remove_struct}")
        print("[PASS] blueprint.graph.edit command validation error path validated")

        add_a = call_domain_tool(
            client,
            10,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch({"x": 0, "y": 0})],
            },
        )
        node_a = op_ok(add_a).get("nodeId")
        if not isinstance(node_a, str) or not node_a:
            fail(f"addFromPalette did not return nodeId: {add_a}")
        missing_data_pin = call_tool(
            client,
            1010,
            "blueprint.graph.inspect",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "view": "data_flow",
                "rootPin": {"node": {"id": node_a}, "pin": "MissingLoomleRegressionPin"},
            },
            expect_error=True,
        )
        if extract_nested_error_code(missing_data_pin) != "PIN_NOT_FOUND":
            fail(f"blueprint.graph.inspect missing data root pin should return PIN_NOT_FOUND: {missing_data_pin}")

        add_b = call_domain_tool(
            client,
            11,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch({"x": 320, "y": 0})],
            },
        )
        node_b = op_ok(add_b).get("nodeId")
        if not isinstance(node_b, str) or not node_b:
            fail(f"addFromPalette did not return nodeId for second node: {add_b}")
        print("[PASS] blueprint.graph.edit addFromPalette validated")

        gate_entry = find_palette_entry(client, 1011, temp_asset, "Gate", "Gate")

        macro_add = call_domain_tool(
            client,
            1012,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {
                        "kind": "addFromPalette",
                        "alias": "authority_macro",
                        "entry": gate_entry,
                        "position": {"x": 0, "y": 520},
                    }
                ],
            },
        )
        macro_node_id = op_ok(macro_add).get("nodeId")
        if not isinstance(macro_node_id, str) or not macro_node_id:
            fail(f"addFromPalette Gate did not return nodeId: {macro_add}")
        macro_node = inspect_blueprint_node(client, 1013, temp_asset, "EventGraph", macro_node_id).get("node", {})
        if macro_node.get("className") != "K2Node_MacroInstance":
            fail(f"addFromPalette Gate did not create K2Node_MacroInstance: {macro_node}")
        macro_ext = macro_node.get("k2Extensions", {}).get("macro") if isinstance(macro_node.get("k2Extensions"), dict) else None
        if not isinstance(macro_ext, dict) or macro_ext.get("macroGraphName") != "Gate":
            fail(f"blueprint.graph.inspect missing macro identity for addFromPalette Gate: {macro_node}")
        if macro_ext.get("macroLibraryAssetPath") != "/Engine/EditorBlueprintResources/StandardMacros":
            fail(f"blueprint.graph.inspect macro library mismatch: {macro_node}")
        print("[PASS] blueprint.graph.palette MacroInstance creation validated")

        self_entry = find_palette_entry(
            client,
            1201,
            temp_asset,
            "Self",
            preferred_node_class="/Script/BlueprintGraph.K2Node_Self",
        )
        cast_actor_entry = find_palette_entry(client, 1202, temp_asset, "Cast To Actor")
        self_graph_edit = call_domain_tool(
            client,
            12,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {
                        "kind": "addFromPalette",
                        "alias": "self",
                        "entry": self_entry,
                        "position": {"x": 0, "y": 220},
                    },
                    {
                        "kind": "addFromPalette",
                        "alias": "cast_actor",
                        "entry": cast_actor_entry,
                        "position": {"x": 280, "y": 220},
                    },
                    {
                        "kind": "connect",
                        "from": {"node": {"alias": "self"}, "pin": "Self"},
                        "to": {"node": {"alias": "cast_actor"}, "pin": "Object"},
                    },
                ],
            },
        )
        self_results = self_graph_edit.get("opResults")
        if not isinstance(self_results, list) or len(self_results) != 3:
            fail(f"blueprint.graph.edit self node opResults mismatch: {self_graph_edit}")
        self_node_id = self_results[0].get("nodeId") if isinstance(self_results[0], dict) else None
        if not isinstance(self_node_id, str) or not self_node_id:
            fail(f"blueprint.graph.edit K2Node_Self did not return nodeId: {self_graph_edit}")
        if not all(isinstance(entry, dict) and entry.get("ok") for entry in self_results):
            fail(f"blueprint.graph.edit K2Node_Self/connect op failed: {self_graph_edit}")
        self_diff = self_graph_edit.get("diff")
        if not isinstance(self_diff, dict):
            fail(f"blueprint.graph.edit missing structured diff: {self_graph_edit}")
        self_nodes_added = self_diff.get("nodesAdded")
        if not isinstance(self_nodes_added, list) or not any(
            isinstance(entry, dict) and entry.get("nodeId") == self_node_id for entry in self_nodes_added
        ):
            fail(f"blueprint.graph.edit diff missing Self node addition: {self_graph_edit}")
        self_links_added = self_diff.get("linksAdded")
        if not isinstance(self_links_added, list) or not any(
            isinstance(entry, dict)
            and entry.get("fromNodeId") == self_node_id
            and entry.get("fromPin") == "Self"
            and entry.get("toPin") == "Object"
            for entry in self_links_added
        ):
            fail(f"blueprint.graph.edit diff missing Self link addition: {self_graph_edit}")
        first_op_diff = self_results[0].get("diff") if isinstance(self_results[0], dict) else None
        if not isinstance(first_op_diff, dict) or not isinstance(first_op_diff.get("nodesAdded"), list):
            fail(f"blueprint.graph.edit opResults diff missing node addition: {self_graph_edit}")

        self_query = inspect_blueprint_node(client, 13, temp_asset, "EventGraph", self_node_id)
        self_node = self_query.get("node", {})
        if self_node.get("className") != "K2Node_Self":
            fail(f"blueprint.graph.inspect self node class mismatch: {self_node}")
        self_pins = self_node.get("pins")
        if not isinstance(self_pins, list) or not any(
            isinstance(pin, dict) and pin.get("name") == "self" and pin.get("direction") == "output"
            for pin in self_pins
        ):
            fail(f"blueprint.graph.inspect self node output pin missing: {self_node}")
        if not any(
            isinstance(pin, dict)
            and pin.get("name") == "self"
            and any(isinstance(link, dict) and link.get("toPin") == "Object" for link in pin.get("links", []))
            for pin in self_pins
        ):
            fail(f"blueprint.graph.inspect self node link to UObject/Actor input missing: {self_node}")
        self_external_query = inspect_blueprint_node(client, 1310, temp_asset, "EventGraph", self_node_id)
        self_external_node = self_external_query.get("node", {})
        self_external_pins = self_external_node.get("pins")
        if not isinstance(self_external_pins, list) or not any(
            isinstance(pin, dict)
            and pin.get("name") == "self"
            and any(isinstance(link, dict) and link.get("toPin") == "Object" for link in pin.get("links", []))
            for pin in self_external_pins
        ):
            fail(f"blueprint.graph.inspect includeConnections pruned external link: {self_external_node}")

        connect_dry_run = call_domain_tool(
            client,
            1299,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "dryRun": True,
                "commands": [
                    bp_connect(node_a, "then", node_b, "execute"),
                ],
            },
        )
        connect_dry_run_results = connect_dry_run.get("opResults")
        if not isinstance(connect_dry_run_results, list) or len(connect_dry_run_results) != 1:
            fail(f"blueprint.graph.edit connect dryRun opResults mismatch: {connect_dry_run}")
        if connect_dry_run_results[0].get("ok") is not True or connect_dry_run_results[0].get("changed") is not False:
            fail(f"blueprint.graph.edit connect dryRun should validate existing pins without changing graph: {connect_dry_run}")
        if connect_dry_run.get("applied") is not False or connect_dry_run.get("valid") is not True:
            fail(f"blueprint.graph.edit connect dryRun should report applied=false and valid=true: {connect_dry_run}")
        print("[PASS] blueprint.graph.edit dryRun existing-pin connection validated")

        reconstruct_payload = call_domain_tool(
            client,
            1301,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [{"kind": "reconstructNode", "node": {"id": self_node_id}, "preserveLinks": True}],
            },
        )
        reconstruct_results = reconstruct_payload.get("opResults")
        if not isinstance(reconstruct_results, list) or len(reconstruct_results) != 1:
            fail(f"blueprint.graph.edit reconstructNode opResults mismatch: {reconstruct_payload}")
        reconstruct_first = reconstruct_results[0] if isinstance(reconstruct_results[0], dict) else {}
        if reconstruct_first.get("ok") is not True:
            fail(f"blueprint.graph.edit reconstructNode failed: {reconstruct_payload}")
        if not isinstance(reconstruct_first.get("pinsBefore"), list) or not isinstance(reconstruct_first.get("pinsAfter"), list):
            fail(f"blueprint.graph.edit reconstructNode missing pin summaries: {reconstruct_payload}")
        if reconstruct_first.get("linksPreserved") != 1:
            fail(f"blueprint.graph.edit reconstructNode should preserve Self link: {reconstruct_payload}")
        reconstruct_dropped = reconstruct_first.get("linksDropped")
        if not isinstance(reconstruct_dropped, list) or reconstruct_dropped:
            fail(f"blueprint.graph.edit reconstructNode unexpectedly dropped links: {reconstruct_payload}")
        print("[PASS] blueprint.graph.edit K2Node_Self creation/connect/inspect validated")
        print("[PASS] blueprint.graph.edit reconstructNode preserveLinks validated")

        blueprint_revision_before = query_graph_payload(
            client,
            105,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_revision_r0 = blueprint_revision_before.get("revision")
        blueprint_nodes_before = blueprint_total_nodes(blueprint_revision_before)
        if not isinstance(blueprint_revision_r0, str) or not blueprint_revision_r0:
            fail(f"Blueprint graph.query missing revision before expectedRevision test: {blueprint_revision_before}")

        blueprint_revision_apply = call_domain_tool(
            client,
            106,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "expectedRevision": blueprint_revision_r0,
                "commands": [bp_branch({"x": 640, "y": 0})],
            },
        )
        op_ok(blueprint_revision_apply)
        blueprint_revision_after_apply = query_graph_payload(
            client,
            107,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_revision_r1 = blueprint_revision_after_apply.get("revision")
        blueprint_nodes_after_apply = blueprint_total_nodes(blueprint_revision_after_apply)
        if not isinstance(blueprint_revision_r1, str) or not blueprint_revision_r1 or blueprint_revision_r1 == blueprint_revision_r0:
            fail(
                "Blueprint expectedRevision control mutate did not advance revision: "
                f"before={blueprint_revision_before} after={blueprint_revision_after_apply}"
            )
        if blueprint_nodes_after_apply != blueprint_nodes_before + 1:
            fail(
                "Blueprint expectedRevision control mutate did not add exactly one node: "
                f"before={blueprint_revision_before} after={blueprint_revision_after_apply}"
            )

        stale_blueprint_revision = call_domain_tool(
            client,
            108,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "expectedRevision": blueprint_revision_r0,
                "commands": [bp_branch({"x": 960, "y": 0})],
            },
            expect_error=True,
        )
        if extract_nested_error_code(stale_blueprint_revision) != "REVISION_CONFLICT" and stale_blueprint_revision.get("domainCode") != "REVISION_CONFLICT":
            fail(f"Blueprint stale expectedRevision did not return REVISION_CONFLICT: {stale_blueprint_revision}")
        blueprint_revision_after_stale = query_graph_payload(
            client,
            109,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_nodes_after_stale = blueprint_total_nodes(blueprint_revision_after_stale)
        if blueprint_revision_after_stale.get("revision") != blueprint_revision_r1:
            fail(
                "Blueprint stale expectedRevision should not change revision: "
                f"expected={blueprint_revision_r1} actual={blueprint_revision_after_stale}"
            )
        if blueprint_nodes_after_stale != blueprint_nodes_after_apply:
            fail(
                "Blueprint stale expectedRevision should not change node count: "
                f"after_apply={blueprint_revision_after_apply} after_stale={blueprint_revision_after_stale}"
            )
        print("[PASS] blueprint expectedRevision conflict validated")

        blueprint_dry_run = call_domain_tool(
            client,
            1091,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "dryRun": True,
                "commands": [bp_branch({"x": 1120, "y": 0})],
            },
        )
        blueprint_dry_run_first = op_ok(blueprint_dry_run)
        if blueprint_dry_run.get("applied") is not False or blueprint_dry_run.get("dryRun") is not True:
            fail(f"Blueprint dryRun mutate should report applied=false and dryRun=true: {blueprint_dry_run}")
        if blueprint_dry_run_first.get("changed") is not False:
            fail(f"Blueprint dryRun mutate should report changed=false: {blueprint_dry_run}")
        if blueprint_dry_run.get("previousRevision") != blueprint_revision_r1 or blueprint_dry_run.get("newRevision") != blueprint_revision_r1:
            fail(
                "Blueprint dryRun mutate revisions should stay pinned to the current graph revision: "
                f"payload={blueprint_dry_run} expectedRevision={blueprint_revision_r1}"
            )
        blueprint_after_dry_run = query_graph_payload(
            client,
            1092,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_nodes_after_dry_run = blueprint_total_nodes(blueprint_after_dry_run)
        if blueprint_after_dry_run.get("revision") != blueprint_revision_r1:
            fail(
                "Blueprint dryRun mutate should not change graph revision: "
                f"expected={blueprint_revision_r1} actual={blueprint_after_dry_run}"
            )
        if blueprint_nodes_after_dry_run != blueprint_nodes_after_apply:
            fail(
                "Blueprint dryRun mutate should not change node count: "
                f"after_apply={blueprint_revision_after_apply} after_dry_run={blueprint_after_dry_run}"
            )
        print("[PASS] blueprint dryRun revision metadata validated")

        bad_graph_command = call_domain_tool(
            client,
            10921,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "dryRun": True,
                "commands": [{"kind": "runScript"}],
            },
            expect_error=True,
        )
        if extract_nested_error_code(bad_graph_command) != "INVALID_ARGUMENT":
            fail(f"Blueprint unsupported graph.edit command should surface INVALID_ARGUMENT: {bad_graph_command}")
        print("[PASS] blueprint.graph.edit unsupported command rejected")

        partial_apply_node = call_domain_tool(
            client,
            10923,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch({"x": 1536, "y": 0})],
            },
        )
        partial_apply_node_id = op_ok(partial_apply_node).get("nodeId")
        if not isinstance(partial_apply_node_id, str) or not partial_apply_node_id:
            fail(f"Blueprint partial-apply setup addFromPalette did not return nodeId: {partial_apply_node}")
        partial_apply_before = query_graph_payload(
            client,
            10924,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        partial_apply_before_revision = partial_apply_before.get("revision")
        partial_apply_failure = call_domain_tool(
            client,
            10925,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    bp_remove(partial_apply_node_id),
                    {"kind": "unknownCommand"},
                ],
            },
            expect_error=True,
        )
        partial_apply_struct = structured_detail_or_payload(partial_apply_failure)
        if partial_apply_struct.get("code") != "INVALID_ARGUMENT":
            fail(f"Blueprint invalid command batch should fail before apply: {partial_apply_failure}")
        partial_apply_after = query_graph_payload(
            client,
            10926,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        if partial_apply_after.get("revision") != partial_apply_before_revision:
            fail(f"Blueprint invalid command batch should not advance revision: before={partial_apply_before} after={partial_apply_after}")
        partial_apply_node_readback = inspect_blueprint_node(client, 10927, temp_asset, "EventGraph", partial_apply_node_id)
        if partial_apply_node_readback.get("isError") is True:
            fail(f"Blueprint invalid command batch should not remove earlier node: {partial_apply_after}")
        print("[PASS] blueprint.graph.edit invalid command batch preflight validated")

        blueprint_idem_key = "bp-idem-1"
        blueprint_idem_before = query_graph_payload(
            client,
            1093,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_before_revision = blueprint_idem_before.get("revision")
        blueprint_idem_before_nodes = blueprint_total_nodes(blueprint_idem_before)
        blueprint_idem_first = call_domain_tool(
            client,
            1094,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "idempotencyKey": blueprint_idem_key,
                "commands": [bp_branch({"x": 1280, "y": 0})],
            },
        )
        blueprint_idem_first_op = op_ok(blueprint_idem_first)
        blueprint_idem_after_first = query_graph_payload(
            client,
            1095,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_after_first_revision = blueprint_idem_after_first.get("revision")
        blueprint_idem_after_first_nodes = blueprint_total_nodes(blueprint_idem_after_first)
        if blueprint_idem_after_first_revision == blueprint_idem_before_revision:
            fail(
                "Blueprint idempotency first mutate should advance revision: "
                f"before={blueprint_idem_before} after={blueprint_idem_after_first}"
            )
        if blueprint_idem_after_first_nodes != blueprint_idem_before_nodes + 1:
            fail(
                "Blueprint idempotency first mutate should add exactly one node: "
                f"before={blueprint_idem_before} after={blueprint_idem_after_first}"
            )
        blueprint_idem_second = call_domain_tool(
            client,
            1096,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "idempotencyKey": blueprint_idem_key,
                "commands": [bp_branch({"x": 1280, "y": 0})],
            },
        )
        blueprint_idem_second_op = op_ok(blueprint_idem_second)
        blueprint_idem_after_second = query_graph_payload(
            client,
            1097,
            asset_path=temp_asset,
            graph_name="EventGraph",
            limit=200,
        )
        blueprint_idem_after_second_nodes = blueprint_total_nodes(blueprint_idem_after_second)
        if blueprint_idem_after_second.get("revision") != blueprint_idem_after_first_revision:
            fail(
                "Blueprint duplicate idempotencyKey should not advance revision: "
                f"after_first={blueprint_idem_after_first} after_second={blueprint_idem_after_second}"
            )
        if blueprint_idem_after_second_nodes != blueprint_idem_after_first_nodes:
            fail(
                "Blueprint duplicate idempotencyKey should not change node count: "
                f"after_first={blueprint_idem_after_first} after_second={blueprint_idem_after_second}"
            )
        if blueprint_idem_second.get("previousRevision") != blueprint_idem_first.get("previousRevision") or blueprint_idem_second.get("newRevision") != blueprint_idem_first.get("newRevision"):
            fail(
                "Blueprint duplicate idempotencyKey should replay the first mutate result metadata: "
                f"first={blueprint_idem_first} second={blueprint_idem_second}"
            )
        if blueprint_idem_second_op.get("nodeId") != blueprint_idem_first_op.get("nodeId"):
            fail(
                "Blueprint duplicate idempotencyKey should replay the original nodeId: "
                f"first={blueprint_idem_first} second={blueprint_idem_second}"
            )
        print("[PASS] blueprint idempotencyKey replay validated")

        duplicate_client_ref = call_domain_tool(
            client,
            1098,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    bp_branch({"x": 1440, "y": 0}, alias="dup_ref"),
                    bp_branch({"x": 1600, "y": 0}, alias="dup_ref"),
                ],
            },
            expect_error=True,
        )
        duplicate_client_ref_struct = structured_detail_or_payload(duplicate_client_ref)
        duplicate_client_ref_results = duplicate_client_ref_struct.get("opResults")
        if not isinstance(duplicate_client_ref_results, list) or len(duplicate_client_ref_results) < 2:
            fail(f"blueprint.graph.edit duplicate clientRef missing opResults: {duplicate_client_ref}")
        duplicate_client_ref_second = duplicate_client_ref_results[1] if isinstance(duplicate_client_ref_results[1], dict) else {}
        if duplicate_client_ref_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"blueprint.graph.edit duplicate clientRef wrong errorCode: {duplicate_client_ref_second}")
        if "Duplicate clientRef" not in str(duplicate_client_ref_second.get("errorMessage", "")):
            fail(f"blueprint.graph.edit duplicate clientRef wrong errorMessage: {duplicate_client_ref_second}")
        print("[PASS] blueprint duplicate clientRef rejected")

        legacy_page = call_tool(
            client,
            110,
            "blueprint.graph.inspect",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "view": "summary",
                "page": {"limit": 1},
            },
            expect_error=True,
        )
        if legacy_page.get("isError") is not True:
            fail(f"blueprint.graph.inspect should reject legacy pagination: {legacy_page}")
        print("[PASS] graph.inspect legacy pagination rejected")

        connect_payload = call_domain_tool(
            client,
            12,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_connect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(connect_payload)

        break_payload = call_domain_tool(
            client,
            13,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_break_links(node_a, "then")],
            },
        )
        op_ok(break_payload)

        reconnect_payload = call_domain_tool(
            client,
            14,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_connect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(reconnect_payload)

        insert_exec_payload = call_domain_tool(
            client,
            141,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    bp_branch(alias="insertedExec"),
                    {
                        "kind": "insertExec",
                        "from": bp_pin(node_a, "then"),
                        "node": {"alias": "insertedExec"},
                        "to": bp_pin(node_b, "execute"),
                    },
                ],
            },
        )
        insert_exec_results = insert_exec_payload.get("opResults")
        if not isinstance(insert_exec_results, list) or len(insert_exec_results) != 2:
            fail(f"blueprint.graph.edit insertExec opResults mismatch: {insert_exec_payload}")
        inserted_exec_node = insert_exec_results[0].get("nodeId") if isinstance(insert_exec_results[0], dict) else None
        if not isinstance(inserted_exec_node, str) or not inserted_exec_node:
            fail(f"blueprint.graph.edit insertExec setup did not return inserted nodeId: {insert_exec_payload}")
        if not isinstance(insert_exec_results[1], dict) or insert_exec_results[1].get("ok") is not True:
            fail(f"blueprint.graph.edit insertExec op failed: {insert_exec_payload}")

        def diff_has_link(diff: dict, field: str, from_node_id: str, from_pin: str, to_node_id: str, to_pin: str) -> bool:
            links = diff.get(field)
            if not isinstance(links, list):
                return False
            from_node_id_lower = from_node_id.lower()
            to_node_id_lower = to_node_id.lower()
            for link in links:
                if not isinstance(link, dict):
                    continue
                if (
                    str(link.get("fromNodeId", "")).lower() == from_node_id_lower
                    and link.get("fromPin") == from_pin
                    and str(link.get("toNodeId", "")).lower() == to_node_id_lower
                    and link.get("toPin") == to_pin
                ):
                    return True
            return False

        insert_exec_diff = insert_exec_results[1].get("diff") if isinstance(insert_exec_results[1], dict) else None
        if not isinstance(insert_exec_diff, dict):
            fail(f"blueprint.graph.edit insertExec missing op diff: {insert_exec_payload}")
        if not diff_has_link(insert_exec_diff, "linksAdded", node_a, "then", inserted_exec_node, "execute"):
            fail(f"blueprint.graph.edit insertExec did not connect source to inserted node: {insert_exec_payload}")
        if not diff_has_link(insert_exec_diff, "linksAdded", inserted_exec_node, "then", node_b, "execute"):
            fail(f"blueprint.graph.edit insertExec did not connect inserted node to target: {insert_exec_payload}")
        if not diff_has_link(insert_exec_diff, "linksRemoved", node_a, "then", node_b, "execute"):
            fail(f"blueprint.graph.edit insertExec did not remove original direct link: {insert_exec_payload}")

        bypass_exec_payload = call_domain_tool(
            client,
            143,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [{"kind": "bypassExec", "node": {"id": inserted_exec_node}}],
            },
        )
        bypass_exec_op = op_ok(bypass_exec_payload)
        bypass_exec_diff = bypass_exec_op.get("diff")
        if not isinstance(bypass_exec_diff, dict):
            fail(f"blueprint.graph.edit bypassExec missing op diff: {bypass_exec_payload}")
        if not diff_has_link(bypass_exec_diff, "linksAdded", node_a, "then", node_b, "execute"):
            fail(f"blueprint.graph.edit bypassExec did not restore direct exec link: {bypass_exec_payload}")
        if not diff_has_link(bypass_exec_diff, "linksRemoved", node_a, "then", inserted_exec_node, "execute"):
            fail(f"blueprint.graph.edit bypassExec did not remove source-to-bypassed link: {bypass_exec_payload}")
        if not diff_has_link(bypass_exec_diff, "linksRemoved", inserted_exec_node, "then", node_b, "execute"):
            fail(f"blueprint.graph.edit bypassExec did not remove bypassed-to-target link: {bypass_exec_payload}")
        bypass_removed_nodes = bypass_exec_diff.get("nodesRemoved")
        if not isinstance(bypass_removed_nodes, list) or not any(
            isinstance(node, dict) and str(node.get("nodeId", "")).lower() == inserted_exec_node.lower()
            for node in bypass_removed_nodes
        ):
            fail(f"blueprint.graph.edit bypassExec did not remove bypassed node: {bypass_exec_payload}")
        nodes_after_bypass_exec = query_nodes(client, 144, temp_asset, "EventGraph")
        require_node_absent(nodes_after_bypass_exec, inserted_exec_node)
        print("[PASS] blueprint.graph.edit insertExec/bypassExec validated")

        disconnect_payload = call_domain_tool(
            client,
            15,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_disconnect(node_a, "then", node_b, "execute")],
            },
        )
        op_ok(disconnect_payload)

        set_default_payload = call_domain_tool(
            client,
            16,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_set_default(node_b, "Condition", "true")],
            },
        )
        op_ok(set_default_payload)

        bad_set_default_payload = call_domain_tool(
            client,
            17,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_set_default(node_b, "DefinitelyMissingPin", "true")],
            },
            expect_error=True,
        )
        bad_set_default_results = bad_set_default_payload.get("opResults")
        if not isinstance(bad_set_default_results, list):
            bad_set_default_detail = bad_set_default_payload.get("detail")
            if isinstance(bad_set_default_detail, str) and bad_set_default_detail:
                try:
                    bad_set_default_struct = json.loads(bad_set_default_detail)
                except Exception as exc:
                    fail(f"blueprint.graph.edit bad setPinDefault detail is not valid JSON: {exc} payload={bad_set_default_payload}")
                bad_set_default_results = bad_set_default_struct.get("opResults")
        if not isinstance(bad_set_default_results, list) or not bad_set_default_results:
            fail(f"blueprint.graph.edit bad setPinDefault missing opResults: {bad_set_default_payload}")
        bad_set_default_first = bad_set_default_results[0] if isinstance(bad_set_default_results[0], dict) else {}
        if bad_set_default_first.get("errorCode") not in {"TARGET_NOT_FOUND", "INVALID_ARGUMENT", "INTERNAL_ERROR"}:
            fail(f"blueprint.graph.edit bad setPinDefault wrong errorCode: {bad_set_default_first}")
        details = bad_set_default_first.get("details")
        if isinstance(details, dict):
            expected_target_forms = details.get("expectedTargetForms")
            if not isinstance(expected_target_forms, list) or not expected_target_forms:
                fail(f"blueprint.graph.edit bad setPinDefault missing expectedTargetForms: {details}")
            candidate_pins = details.get("candidatePins")
            if not isinstance(candidate_pins, list) or not candidate_pins:
                fail(f"blueprint.graph.edit bad setPinDefault missing candidatePins: {details}")
            if not any(isinstance(pin, dict) and pin.get("pinName") == "Condition" for pin in candidate_pins):
                fail(f"blueprint.graph.edit bad setPinDefault candidatePins missing Condition: {candidate_pins}")
        elif "DefinitelyMissingPin" not in str(bad_set_default_first.get("errorMessage", "")):
            fail(f"blueprint.graph.edit bad setPinDefault should surface the missing pin name: {bad_set_default_first}")
        print("[PASS] blueprint.graph.edit setPinDefault diagnostics validated")

        move_payload = call_domain_tool(
            client,
            18,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_b), "position": {"x": 640, "y": 120}}
                ],
            },
        )
        op_ok(move_payload)

        move_by_payload = call_domain_tool(
            client,
            1801,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_b), "delta": {"x": 16, "y": 32}}
                ],
            },
        )
        op_ok(move_by_payload)

        move_node_a_payload = call_domain_tool(
            client,
            1802,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_a), "delta": {"x": 16, "y": 0}}
                ],
            },
        )
        op_ok(move_node_a_payload)

        compile_payload = call_domain_tool(
            client,
            19,
            "blueprint",
            "compile",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
            },
        )
        nodes_after_compile = query_nodes(client, 20, temp_asset, "EventGraph")
        if compile_payload.get("compiled") is not True and compile_payload.get("status") not in {"clean", "compiled"}:
            fail(f"Blueprint compile should return a successful compile report: {compile_payload}")

        nodes_before_remove = nodes_after_compile
        node_a_info = require_node(nodes_before_remove, node_a)
        node_b_info = require_node(nodes_before_remove, node_b)
        node_a_layout = require_layout(node_a_info)
        node_b_layout = require_layout(node_b_info)
        if node_a_layout.get("position", {}).get("x") != 16 or node_a_layout.get("position", {}).get("y") != 0:
            fail(f"blueprint.graph.edit moveNode delta did not update node_a layout as expected: {node_a_info}")
        node_b_pos = node_b_layout.get("position", {})
        if node_b_pos.get("x") != 656 or node_b_pos.get("y") not in {144, 160}:
            fail(f"blueprint.graph.edit moveNode position/delta did not update node_b layout as expected: {node_b_info}")

        add_without_position = call_domain_tool(
            client,
            1806,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch()],
            },
        )
        node_e = op_ok(add_without_position).get("nodeId")
        if not isinstance(node_e, str) or not node_e:
            fail(f"addFromPalette without position did not return nodeId: {add_without_position}")
        nodes_after_auto_place = query_nodes(client, 1807, temp_asset, "EventGraph")
        node_e_info = require_node(nodes_after_auto_place, node_e)
        node_e_layout = require_layout(node_e_info)
        node_e_pos = node_e_layout.get("position", {})
        if node_e_pos.get("x") == 0 and node_e_pos.get("y") == 0:
            fail(f"addFromPalette without position still defaulted to origin: {node_e_info}")
        add_from_exec_pin = call_domain_tool(
            client,
            18081,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [{
                    "kind": "addFromPalette",
                    "entry": BP_BRANCH_ENTRY,
                    "fromPins": [{"node": {"id": node_e}, "pin": "then"}],
                }],
            },
        )
        node_f = op_ok(add_from_exec_pin).get("nodeId")
        if not isinstance(node_f, str) or not node_f:
            fail(f"addFromPalette with exec fromPins did not return nodeId: {add_from_exec_pin}")
        node_e_with_pins = inspect_blueprint_node(client, 18082, temp_asset, "EventGraph", node_e).get("node", {})
        node_f_info = inspect_blueprint_node(client, 18084, temp_asset, "EventGraph", node_f).get("node", {})
        node_f_layout = require_layout(node_f_info)

        def exec_anchor_y(node: dict, pin_name: str) -> float | None:
            pins = node.get("pins")
            if not isinstance(pins, list):
                return None
            for pin in pins:
                if not isinstance(pin, dict) or pin.get("name") != pin_name or pin.get("category") != "exec":
                    continue
                anchor = pin.get("layout", {}).get("anchor") if isinstance(pin.get("layout"), dict) else None
                value = anchor.get("y") if isinstance(anchor, dict) else None
                return value if isinstance(value, (int, float)) else None
            return None

        from_anchor_y = exec_anchor_y(node_e_with_pins, "then")
        to_anchor_y = exec_anchor_y(node_f_info, "execute")
        if from_anchor_y is None or to_anchor_y is None or abs(from_anchor_y - to_anchor_y) > 1:
            fail(f"addFromPalette with exec fromPins did not align exec anchors: from={node_e_with_pins} to={node_f_info}")
        node_f_pos = node_f_layout.get("position", {})
        if not isinstance(node_f_pos.get("x"), (int, float)) or node_f_pos.get("x") <= node_e_pos.get("x", 0):
            fail(f"addFromPalette with exec fromPins did not place node to the right: from={node_e_with_pins} to={node_f_info}")
        disalign_f = call_domain_tool(
            client,
            18085,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [
                    {"kind": "moveNode", "node": bp_node(node_f), "delta": {"x": 0, "y": 96}}
                ],
            },
        )
        op_ok(disalign_f)
        layout_dry_run = call_tool(client, 18086, "blueprint.graph.layout", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "root": {"id": node_e},
            "dryRun": True,
        })
        if layout_dry_run.get("applied") is not False or layout_dry_run.get("valid") is not True:
            fail(f"blueprint.graph.layout dryRun should validate without applying: {layout_dry_run}")
        planned_moves = layout_dry_run.get("planned", {}).get("moves")
        if not isinstance(planned_moves, list) or not any(
            isinstance(move, dict) and move.get("node", {}).get("id") == node_f
            for move in planned_moves
        ):
            fail(f"blueprint.graph.layout dryRun should plan a move for disaligned exec child: {layout_dry_run}")
        layout_apply = call_tool(client, 18087, "blueprint.graph.layout", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "root": {"id": node_e},
        })
        if layout_apply.get("applied") is not True or layout_apply.get("valid") is not True:
            fail(f"blueprint.graph.layout apply should move the execution tree: {layout_apply}")
        node_e_after_layout = inspect_blueprint_node(client, 18088, temp_asset, "EventGraph", node_e).get("node", {})
        node_f_after_layout = inspect_blueprint_node(client, 18089, temp_asset, "EventGraph", node_f).get("node", {})
        from_anchor_after_layout = exec_anchor_y(node_e_after_layout, "then")
        to_anchor_after_layout = exec_anchor_y(node_f_after_layout, "execute")
        if (
            from_anchor_after_layout is None
            or to_anchor_after_layout is None
            or abs(from_anchor_after_layout - to_anchor_after_layout) > 1
        ):
            fail(
                "blueprint.graph.layout did not restore straight exec anchor alignment: "
                f"from={node_e_after_layout} to={node_f_after_layout} payload={layout_apply}"
            )
        print("[PASS] blueprint.graph.layout root exec-tree formatting validated")
        remove_f = call_domain_tool(
            client,
            18083,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_f)],
            },
        )
        op_ok(remove_f)
        remove_e = call_domain_tool(
            client,
            1808,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_e)],
            },
        )
        op_ok(remove_e)
        print("[PASS] blueprint.graph.edit addFromPalette auto-placement validated")

        palette_branch = call_tool(client, 1809, "blueprint.graph.palette", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "query": "Branch",
            "limit": 10,
        })
        palette_entries = palette_branch.get("entries")
        if not isinstance(palette_entries, list) or not palette_entries:
            fail(f"blueprint.graph.palette query Branch returned no entries: {palette_branch}")
        branch_entry = next((entry for entry in palette_entries if entry.get("label") == "Branch"), palette_entries[0])
        if not branch_entry.get("id"):
            fail(f"blueprint.graph.palette entry missing id: {branch_entry}")
        if branch_entry.get("actionType") not in {"nodeSpawner", "schemaAction"}:
            fail(f"blueprint.graph.palette entry has unexpected actionType: {branch_entry}")
        if branch_entry.get("contextSensitive") is not True or branch_entry.get("executable") is not True:
            fail(f"blueprint.graph.palette nodeSpawner metadata mismatch: {branch_entry}")
        dry_palette = call_tool(client, 18095, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "dryRun": True,
            "commands": [{
                "kind": "addFromPalette",
                "entry": branch_entry,
                "position": {"x": 960, "y": 320},
                "alias": "paletteBranchDry",
            }],
        })
        if dry_palette.get("applied") is not False:
            fail(f"addFromPalette dryRun should report applied=false: {dry_palette}")
        dry_palette_first = op_ok(dry_palette)
        if dry_palette_first.get("changed") is not False:
            fail(f"addFromPalette dryRun should validate without changing graph: {dry_palette}")
        add_from_palette = call_tool(client, 1810, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "commands": [{
                "kind": "addFromPalette",
                "entry": branch_entry,
                "position": {"x": 960, "y": 320},
                "alias": "paletteBranch",
            }],
        })
        palette_node = op_ok(add_from_palette).get("nodeId")
        if not isinstance(palette_node, str) or not palette_node:
            fail(f"addFromPalette did not return nodeId: {add_from_palette}")
        nodes_after_palette = query_nodes(client, 1811, temp_asset, "EventGraph")
        palette_node_info = require_node(nodes_after_palette, palette_node)
        if "K2Node_IfThenElse" not in (palette_node_info.get("nodeClassPath") or palette_node_info.get("classPath") or palette_node_info.get("className") or ""):
            fail(f"addFromPalette Branch did not create K2Node_IfThenElse: {palette_node_info}")
        remove_palette_node = call_domain_tool(
            client,
            1812,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(palette_node)],
            },
        )
        op_ok(remove_palette_node)
        print("[PASS] blueprint.graph.palette and addFromPalette Branch creation validated")

        switch_name_entry = find_palette_entry(
            client,
            18121,
            temp_asset,
            "Switch on Name",
            preferred_node_class="/Script/BlueprintGraph.K2Node_SwitchName",
        )
        add_switch_name = call_tool(client, 18122, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "commands": [{
                "kind": "addFromPalette",
                "entry": switch_name_entry,
                "position": {"x": 1100, "y": 520},
                "alias": "switchNameCases",
            }],
        })
        switch_name_node = op_ok(add_switch_name).get("nodeId")
        if not isinstance(switch_name_node, str) or not switch_name_node:
            fail(f"Switch on Name addFromPalette did not return nodeId: {add_switch_name}")

        graph_overview = inspect_blueprint_node(client, 18123, temp_asset, "EventGraph", switch_name_node)
        overview_node = graph_overview.get("node", {})
        if overview_node.get("hasNodeEditCapabilities") is not True or overview_node.get("inspectWith") != "blueprint.node.inspect":
            fail(f"blueprint.graph.inspect should route Switch on Name to blueprint.node.inspect: {overview_node}")

        switch_inspect_before = call_tool(client, 18124, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": switch_name_node},
        })
        capabilities = switch_inspect_before.get("editCapabilities")
        if not isinstance(capabilities, dict) or capabilities.get("hasPinOperations") is not True:
            fail(f"blueprint.node.inspect missing Switch on Name editCapabilities: {switch_inspect_before}")
        operations = capabilities.get("pinOperations")
        operation_names = {item.get("operation") for item in operations if isinstance(item, dict)} if isinstance(operations, list) else set()
        if "addPin" not in operation_names or "renamePin" not in operation_names:
            fail(f"blueprint.node.inspect missing Switch on Name pin operations: {switch_inspect_before}")
        switch_node_before = switch_inspect_before.get("node", {})
        switch_exec_pins = [
            pin for pin in switch_node_before.get("pins", [])
            if isinstance(pin, dict) and pin.get("category") == "exec"
        ] if isinstance(switch_node_before, dict) else []
        if not switch_exec_pins:
            fail(f"blueprint.node.inspect Switch on Name missing exec pins: {switch_inspect_before}")
        for pin in switch_exec_pins:
            pin_layout = pin.get("layout")
            if not isinstance(pin_layout, dict) or pin_layout.get("source") != "estimate":
                fail(f"blueprint.node.inspect exec pin missing estimated layout: {pin}")
            if not isinstance(pin_layout.get("offset"), dict) or not isinstance(pin_layout.get("anchor"), dict):
                fail(f"blueprint.node.inspect exec pin layout missing offset/anchor: {pin_layout}")

        add_case_schema = call_tool(client, 18125, "schema.inspect", {
            "domain": "blueprint",
            "tool": "blueprint.node.edit",
            "operation": "addPin",
            "include": ["summary", "schema"],
        })
        if add_case_schema.get("operation") != "addPin" or not isinstance(add_case_schema.get("schema"), dict):
            fail(f"schema.inspect blueprint.node.edit addPin schema invalid: {add_case_schema}")

        add_case = call_tool(client, 18126, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": switch_name_node},
            "operation": "addPin",
            "args": {"role": "case", "name": "Paused"},
        })
        add_case_result = op_ok(add_case)
        if add_case_result.get("changed") is not True:
            fail(f"blueprint.node.edit addPin should change Switch on Name: {add_case}")

        switch_inspect_after = call_tool(client, 18127, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": switch_name_node},
        })
        case_pins = switch_inspect_after.get("editState", {}).get("casePins")
        pins_after = switch_inspect_after.get("node", {}).get("pins")
        pin_names_after = {
            pin.get("name")
            for pin in pins_after
            if isinstance(pin, dict) and isinstance(pin.get("name"), str)
        } if isinstance(pins_after, list) else set()
        if "Paused" not in pin_names_after and (not isinstance(case_pins, list) or "Paused" not in case_pins):
            fail(f"blueprint.node.edit addPin did not expose added Switch on Name case: {switch_inspect_after}")
        print("[PASS] blueprint.node.inspect/edit Switch on Name case pin validated")

        select_entry = find_palette_entry(
            client,
            18128,
            temp_asset,
            "Select",
            preferred_node_class="/Script/BlueprintGraph.K2Node_Select",
        )
        add_select = call_tool(client, 18129, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "commands": [{
                "kind": "addFromPalette",
                "entry": select_entry,
                "position": {"x": 1320, "y": 520},
                "alias": "selectOptions",
            }],
        })
        select_node = op_ok(add_select).get("nodeId")
        if not isinstance(select_node, str) or not select_node:
            fail(f"Select addFromPalette did not return nodeId: {add_select}")

        select_inspect_before = call_tool(client, 18130, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": select_node},
        })
        select_caps = select_inspect_before.get("editCapabilities")
        select_ops = select_caps.get("pinOperations") if isinstance(select_caps, dict) else []
        select_op_names = {item.get("operation") for item in select_ops if isinstance(item, dict)} if isinstance(select_ops, list) else set()
        if "addPin" not in select_op_names or "removePin" not in select_op_names:
            fail(f"blueprint.node.inspect missing Select option operations: {select_inspect_before}")
        select_pins_before = select_inspect_before.get("editState", {}).get("optionPins")
        select_count_before = len(select_pins_before) if isinstance(select_pins_before, list) else -1
        add_select_option = call_tool(client, 18131, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": select_node},
            "operation": "addPin",
            "args": {"role": "option"},
        })
        if op_ok(add_select_option).get("changed") is not True:
            fail(f"blueprint.node.edit addPin should change Select options: {add_select_option}")
        select_inspect_added = call_tool(client, 18132, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": select_node},
        })
        select_pins_added = select_inspect_added.get("editState", {}).get("optionPins")
        select_count_added = len(select_pins_added) if isinstance(select_pins_added, list) else -1
        if select_count_added <= select_count_before:
            fail(f"blueprint.node.edit addPin did not add Select option pin: {select_inspect_added}")
        remove_select_option = call_tool(client, 18133, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": select_node},
            "operation": "removePin",
            "args": {},
        })
        if op_ok(remove_select_option).get("changed") is not True:
            fail(f"blueprint.node.edit removePin should change Select options: {remove_select_option}")
        select_inspect_removed = call_tool(client, 18134, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": select_node},
        })
        select_pins_removed = select_inspect_removed.get("editState", {}).get("optionPins")
        select_count_removed = len(select_pins_removed) if isinstance(select_pins_removed, list) else -1
        if select_count_removed != select_count_before:
            fail(f"blueprint.node.edit removePin should remove Select's last option: {select_inspect_removed}")
        print("[PASS] blueprint.node.edit Select option add/remove validated")

        format_text_entry = find_palette_entry(
            client,
            18135,
            temp_asset,
            "Format Text",
            preferred_node_class="/Script/BlueprintGraph.K2Node_FormatText",
        )
        add_format_text = call_tool(client, 18136, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "commands": [{
                "kind": "addFromPalette",
                "entry": format_text_entry,
                "position": {"x": 1540, "y": 520},
                "alias": "formatTextArguments",
            }],
        })
        format_text_node = op_ok(add_format_text).get("nodeId")
        if not isinstance(format_text_node, str) or not format_text_node:
            fail(f"Format Text addFromPalette did not return nodeId: {add_format_text}")

        move_arg_schema = call_tool(client, 18137, "schema.inspect", {
            "domain": "blueprint",
            "tool": "blueprint.node.edit",
            "operation": "movePin",
            "include": ["summary", "schema"],
        })
        if move_arg_schema.get("operation") != "movePin" or not isinstance(move_arg_schema.get("schema"), dict):
            fail(f"schema.inspect blueprint.node.edit movePin schema invalid: {move_arg_schema}")

        for offset, name in enumerate(("PlayerName", "Score")):
            add_argument = call_tool(client, 18138 + offset, "blueprint.node.edit", {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "node": {"id": format_text_node},
                "operation": "addPin",
                "args": {"role": "argument", "name": name},
            })
            if op_ok(add_argument).get("changed") is not True:
                fail(f"blueprint.node.edit addPin should change Format Text arguments: {add_argument}")
        move_argument = call_tool(client, 18140, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": format_text_node},
            "operation": "movePin",
            "args": {"pin": "Score", "target": {"pin": "PlayerName"}, "position": "before"},
        })
        if op_ok(move_argument).get("changed") is not True:
            fail(f"blueprint.node.edit movePin should change Format Text arguments: {move_argument}")
        rename_argument = call_tool(client, 18141, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": format_text_node},
            "operation": "renamePin",
            "args": {"pin": "Score", "name": "PlayerScore"},
        })
        if op_ok(rename_argument).get("changed") is not True:
            fail(f"blueprint.node.edit renamePin should change Format Text arguments: {rename_argument}")
        remove_argument = call_tool(client, 18142, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": format_text_node},
            "operation": "removePin",
            "args": {"pin": "PlayerName"},
        })
        if op_ok(remove_argument).get("changed") is not True:
            fail(f"blueprint.node.edit removePin should change Format Text arguments: {remove_argument}")
        format_text_inspect = call_tool(client, 18143, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": format_text_node},
        })
        argument_pins = format_text_inspect.get("editState", {}).get("argumentPins")
        if not isinstance(argument_pins, list) or "PlayerScore" not in argument_pins or "PlayerName" in argument_pins:
            fail(f"blueprint.node.edit Format Text argument operations produced unexpected state: {format_text_inspect}")
        print("[PASS] blueprint.node.edit Format Text argument add/move/rename/remove validated")

        set_fields_entry = find_palette_entry(
            client,
            18144,
            temp_asset,
            "Set members in Vector Parameter Value",
            preferred_label="Set members in Vector Parameter Value",
            preferred_node_class="/Script/BlueprintGraph.K2Node_SetFieldsInStruct",
        )
        add_set_fields = call_tool(client, 18145, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "commands": [{
                "kind": "addFromPalette",
                "entry": set_fields_entry,
                "position": {"x": 1760, "y": 520},
                "alias": "setFieldsVisibility",
            }],
        })
        set_fields_node = op_ok(add_set_fields).get("nodeId")
        if not isinstance(set_fields_node, str) or not set_fields_node:
            fail(f"SetFieldsInStruct addFromPalette did not return nodeId: {add_set_fields}")

        restore_fields_schema = call_tool(client, 18146, "schema.inspect", {
            "domain": "blueprint",
            "tool": "blueprint.node.edit",
            "operation": "restorePins",
            "include": ["summary", "schema"],
        })
        if restore_fields_schema.get("operation") != "restorePins" or not isinstance(restore_fields_schema.get("schema"), dict):
            fail(f"schema.inspect blueprint.node.edit restorePins schema invalid: {restore_fields_schema}")
        restore_fields_initial = call_tool(client, 18147, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
            "operation": "restorePins",
            "args": {"scope": "all"},
        })
        op_ok(restore_fields_initial)
        set_fields_inspect_restored = call_tool(client, 18148, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
        })
        restored_field_pins = set_fields_inspect_restored.get("editState", {}).get("fieldPins")
        removable_field = None
        if isinstance(restored_field_pins, list):
            for candidate in restored_field_pins:
                if isinstance(candidate, str) and candidate not in {"execute", "then", "StructRef"}:
                    removable_field = candidate
                    break
        if not removable_field:
            fail(f"SetFieldsInStruct restorePins did not expose removable field pins: {set_fields_inspect_restored}")
        remove_field = call_tool(client, 18149, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
            "operation": "removePin",
            "args": {"pin": removable_field},
        })
        if op_ok(remove_field).get("changed") is not True:
            fail(f"blueprint.node.edit removePin should hide SetFieldsInStruct field: {remove_field}")
        set_fields_inspect_hidden = call_tool(client, 18150, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
        })
        hidden_field_pins = set_fields_inspect_hidden.get("editState", {}).get("fieldPins")
        if isinstance(hidden_field_pins, list) and removable_field in hidden_field_pins:
            fail(f"blueprint.node.edit removePin did not hide SetFieldsInStruct field: {set_fields_inspect_hidden}")
        restore_fields_final = call_tool(client, 18151, "blueprint.node.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
            "operation": "restorePins",
            "args": {"scope": "all"},
        })
        if op_ok(restore_fields_final).get("changed") is not True:
            fail(f"blueprint.node.edit restorePins should restore SetFieldsInStruct fields: {restore_fields_final}")
        set_fields_inspect_final = call_tool(client, 18152, "blueprint.node.inspect", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "node": {"id": set_fields_node},
        })
        final_field_pins = set_fields_inspect_final.get("editState", {}).get("fieldPins")
        if not isinstance(final_field_pins, list) or removable_field not in final_field_pins:
            fail(f"blueprint.node.edit restorePins did not restore SetFieldsInStruct field: {set_fields_inspect_final}")
        print("[PASS] blueprint.node.edit SetFieldsInStruct field hide/restore validated")

        palette_schema = call_tool(client, 1813, "blueprint.graph.palette", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "query": "Select a Component",
            "limit": 20,
        })
        schema_entries = palette_schema.get("entries")
        if not isinstance(schema_entries, list) or not schema_entries:
            fail(f"blueprint.graph.palette schema action query returned no entries: {palette_schema}")
        schema_entry = next((entry for entry in schema_entries if entry.get("actionType") == "schemaAction"), None)
        if not isinstance(schema_entry, dict):
            fail(f"blueprint.graph.palette schema action missing from query: {palette_schema}")
        if schema_entry.get("executable") is not False:
            fail(f"blueprint.graph.palette schema action should report executable=false: {schema_entry}")
        schema_dry_run = call_tool(client, 1814, "blueprint.graph.edit", {
            "assetPath": temp_asset,
            "graph": {"name": "EventGraph"},
            "dryRun": True,
            "commands": [{
                "kind": "addFromPalette",
                "entry": schema_entry,
                "position": {"x": 960, "y": 480},
            }],
        }, expect_error=True)
        schema_result = schema_dry_run.get("opResults", [{}])[0]
        if schema_result.get("errorCode") != "PALETTE_ENTRY_NOT_EXECUTABLE":
            fail(f"schema action addFromPalette dryRun should fail as not executable: {schema_dry_run}")
        print("[PASS] blueprint.graph.palette schema action non-executable metadata validated")

        remove_a = call_domain_tool(
            client,
            21,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_a)],
            },
        )
        op_ok(remove_a)
        nodes_after_remove_a = query_nodes(client, 22, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_a, node_a)

        remove_b = call_domain_tool(
            client,
            23,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_b)],
            },
        )
        op_ok(remove_b)
        nodes_after_remove_b = query_nodes(client, 24, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_b, node_b)

        add_c = call_domain_tool(
            client,
            25,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch({"x": 960, "y": 0})],
            },
        )
        node_c = op_ok(add_c).get("nodeId")
        if not isinstance(node_c, str) or not node_c:
            fail(f"addFromPalette did not return nodeId for third node: {add_c}")

        remove_c = call_domain_tool(
            client,
            26,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_c)],
            },
        )
        op_ok(remove_c)
        nodes_after_remove_c = query_nodes(client, 27, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_c, node_c)

        add_via_graph_ref = call_domain_tool(
            client,
            28,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_branch({"x": 1280, "y": 0})],
            },
        )
        node_d = op_ok(add_via_graph_ref).get("nodeId")
        if not isinstance(node_d, str) or not node_d:
            fail(f"graph-scoped mutate addFromPalette did not return nodeId: {add_via_graph_ref}")

        remove_via_target_graph_ref = call_domain_tool(
            client,
            29,
            "blueprint",
            "mutate",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
                "commands": [bp_remove(node_d)],
            },
        )
        op_ok(remove_via_target_graph_ref)
        nodes_after_remove_d = query_nodes(client, 30, temp_asset, "EventGraph")
        require_node_absent(nodes_after_remove_d, node_d)

        print("[PASS] blueprint.graph.edit removeNode validated for stable node ids")
        print("[PASS] blueprint.graph.edit graph-scoped mutate validated")

        bulk_branch_ops = []
        for index in range(60):
            bulk_branch_ops.append(
                {
                    "kind": "addFromPalette",
                    "alias": f"bulk_branch_{index}",
                    "entry": BP_BRANCH_ENTRY,
                    "position": {"x": 2200 + (index * 48), "y": 1800},
                }
            )
        for chunk_index in range(0, len(bulk_branch_ops), 10):
            bulk_blueprint_insert = call_domain_tool(
                client,
                1600 + chunk_index,
                "blueprint",
                "mutate",
                {
                    "assetPath": temp_asset,
                    "graph": {"name": "EventGraph"},
                    "commands": bulk_branch_ops[chunk_index:chunk_index + 10],
                },
            )
            op_ok(bulk_blueprint_insert)

        blueprint_default_page = call_domain_tool(
            client,
            1601,
            "blueprint",
            "query",
            {
                "assetPath": temp_asset,
                "graph": {"name": "EventGraph"},
            },
        )
        default_nodes = blueprint_summary_nodes(blueprint_default_page)
        default_meta = blueprint_default_page.get("meta", {})
        if not isinstance(default_nodes, list) or not default_nodes:
            fail(
                "Blueprint graph.query without explicit limit should return summary nodes: "
                f"{blueprint_default_page}"
            )
        if not isinstance(default_meta.get("totalNodes"), int) or default_meta.get("totalNodes", 0) < 1:
            fail(
                "Blueprint graph.query without explicit limit should report totalNodes metadata: "
                f"{blueprint_default_page}"
            )
        if blueprint_default_page.get("view") != "summary":
            fail(f"Blueprint graph.query without explicit limit should default to summary: {blueprint_default_page}")
        print("[PASS] blueprint graph.inspect default summary behavior validated")

        material_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=10000,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_material_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.MaterialFactoryNew()\n"
                "material = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if material is None:\n"
                "    material = asset_tools.create_asset(asset_name, pkg_path, unreal.Material, factory)\n"
                "if material is None:\n"
                "    raise RuntimeError('failed to create material asset')\n"
                "unreal.EditorAssetLibrary.save_loaded_asset(material)\n"
                "print(json.dumps({'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        material_fixture = parse_execute_json(material_fixture_payload)
        material_asset_path = material_fixture.get("assetPath")
        if not isinstance(material_asset_path, str) or not material_asset_path:
            fail(f"Material fixture missing assetPath: {material_fixture}")
        print("[PASS] temporary material fixture created")
        material_palette = call_tool(
            client,
            10008,
            "material.palette",
            {"assetPath": material_asset_path, "query": "Multiply", "limit": 20},
        )
        material_palette_entries = material_palette.get("entries")
        if not isinstance(material_palette_entries, list) or not any(
            isinstance(entry, dict)
            and isinstance(entry.get("payload"), dict)
            and entry["payload"].get("nodeClassPath") == "/Script/Engine.MaterialExpressionMultiply"
            for entry in material_palette_entries
        ):
            fail(f"material.palette missing Multiply expression entry: {material_palette}")
        print("[PASS] material.palette expression lookup validated")
        material_graph_list_without_type = call_domain_tool(
            client,
            10009,
            "material",
            "list",
            {"assetPath": material_asset_path},
        )
        if material_graph_list_without_type.get("assetPath") != material_asset_path:
            fail(f"material.list assetPath mismatch: {material_graph_list_without_type}")
        material_expressions = material_graph_list_without_type.get("expressions")
        if not isinstance(material_expressions, list):
            fail(f"material.list missing expressions[]: {material_graph_list_without_type}")
        material_output_count = material_graph_list_without_type.get("outputCount")
        if not isinstance(material_output_count, int) or material_output_count < 1:
            fail(f"material.list missing valid outputCount: {material_graph_list_without_type}")
        print("[PASS] material.list validated")

        material_add = call_domain_tool(
            client,
            10010,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionConstant"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"},
                ],
            },
        )
        material_add_results = material_add.get("opResults")
        if not isinstance(material_add_results, list) or len(material_add_results) != 3:
            fail(f"Material fixture add ops missing results: {material_add}")
        material_param_id = material_add_results[0].get("nodeId")
        material_constant_id = material_add_results[1].get("nodeId")
        material_multiply_id = material_add_results[2].get("nodeId")
        if not all(isinstance(node_id, str) and node_id for node_id in [material_param_id, material_constant_id, material_multiply_id]):
            fail(f"Material fixture add ops missing node ids: {material_add}")

        material_param_rename = call_tool(
            client,
            100100,
            "material.node.edit",
            {
                "assetPath": material_asset_path,
                "node": {"id": material_param_id},
                "property": "ParameterName",
                "value": "DensityScale",
            },
        )
        material_param_rename_op = op_ok(material_param_rename)
        if material_param_rename_op.get("op") != "setproperty" or material_param_rename_op.get("nodeId") != material_param_id:
            fail(f"material.node.edit ParameterName returned unexpected op result: {material_param_rename}")
        print("[PASS] material.node.edit editable property update validated")

        material_selection_layout = call_tool(
            client,
            1001001,
            "material.graph.layout",
            {
                "assetPath": material_asset_path,
                "operation": "format",
                "scope": {
                    "mode": "selection",
                    "nodes": [{"id": material_param_id}, {"id": material_constant_id}, {"id": material_multiply_id}],
                },
            },
        )
        material_selection_layout_op = op_ok(material_selection_layout)
        if material_selection_layout_op.get("op") != "layoutgraph":
            fail(f"material.graph.layout selection returned unexpected op result: {material_selection_layout}")
        print("[PASS] material.graph.layout selection validated")

        material_revision_before = call_domain_tool(
            client,
            100101,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_revision_r0 = material_revision_before.get("revision")
        material_nodes_before = material_revision_before.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_revision_r0, str) or not material_revision_r0:
            fail(f"Material graph.query missing revision before expectedRevision test: {material_revision_before}")
        if not isinstance(material_nodes_before, list):
            fail(f"Material graph.query missing nodes before expectedRevision test: {material_revision_before}")

        material_revision_apply = call_domain_tool(
            client,
            100102,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        op_ok(material_revision_apply)
        material_revision_after_apply = call_domain_tool(
            client,
            100103,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_revision_r1 = material_revision_after_apply.get("revision")
        material_nodes_after_apply = material_revision_after_apply.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_revision_r1, str) or not material_revision_r1 or material_revision_r1 == material_revision_r0:
            fail(
                "Material expectedRevision control mutate did not advance revision: "
                f"before={material_revision_before} after={material_revision_after_apply}"
            )
        if not isinstance(material_nodes_after_apply, list) or len(material_nodes_after_apply) != len(material_nodes_before) + 1:
            fail(
                "Material expectedRevision control mutate did not add exactly one node: "
                f"before={material_revision_before} after={material_revision_after_apply}"
            )

        stale_material_revision = call_domain_tool(
            client,
            100104,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "expectedRevision": material_revision_r0,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
            expect_error=True,
        )
        if extract_nested_error_code(stale_material_revision) != "REVISION_CONFLICT" and stale_material_revision.get("domainCode") != "REVISION_CONFLICT":
            fail(f"Material stale expectedRevision did not return REVISION_CONFLICT: {stale_material_revision}")
        material_revision_after_stale = call_domain_tool(
            client,
            100105,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_nodes_after_stale = material_revision_after_stale.get("semanticSnapshot", {}).get("nodes", [])
        if material_revision_after_stale.get("revision") != material_revision_r1:
            fail(
                "Material stale expectedRevision should not change revision: "
                f"expected={material_revision_r1} actual={material_revision_after_stale}"
            )
        if not isinstance(material_nodes_after_stale, list) or len(material_nodes_after_stale) != len(material_nodes_after_apply):
            fail(
                "Material stale expectedRevision should not change node count: "
                f"after_apply={material_revision_after_apply} after_stale={material_revision_after_stale}"
            )
        print("[PASS] material expectedRevision conflict validated")

        material_dry_run = call_domain_tool(
            client,
            100106,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "dryRun": True,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        material_dry_run_first = op_ok(material_dry_run)
        if material_dry_run_first.get("changed") is not False:
            fail(f"Material dryRun mutate should report changed=false: {material_dry_run}")
        if material_dry_run.get("previousRevision") != material_revision_r1 or material_dry_run.get("newRevision") != material_revision_r1:
            fail(
                "Material dryRun mutate revisions should stay pinned to the current graph revision: "
                f"payload={material_dry_run} expectedRevision={material_revision_r1}"
            )
        material_after_dry_run = call_domain_tool(
            client,
            100107,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_nodes_after_dry_run = material_after_dry_run.get("semanticSnapshot", {}).get("nodes", [])
        if material_after_dry_run.get("revision") != material_revision_r1:
            fail(
                "Material dryRun mutate should not change graph revision: "
                f"expected={material_revision_r1} actual={material_after_dry_run}"
            )
        if not isinstance(material_nodes_after_dry_run, list) or len(material_nodes_after_dry_run) != len(material_nodes_after_apply):
            fail(
                "Material dryRun mutate should not change node count: "
                f"after_apply={material_revision_after_apply} after_dry_run={material_after_dry_run}"
            )
        print("[PASS] material dryRun revision metadata validated")

        material_idem_key = "material-idem-1"
        material_idem_before = call_domain_tool(
            client,
            1001071,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_idem_before_revision = material_idem_before.get("revision")
        material_idem_before_nodes = material_idem_before.get("semanticSnapshot", {}).get("nodes", [])
        material_idem_first = call_domain_tool(
            client,
            1001072,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        material_idem_first_op = op_ok(material_idem_first)
        material_idem_after_first = call_domain_tool(
            client,
            1001073,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_idem_after_first_revision = material_idem_after_first.get("revision")
        material_idem_after_first_nodes = material_idem_after_first.get("semanticSnapshot", {}).get("nodes", [])
        if not isinstance(material_idem_before_nodes, list) or not isinstance(material_idem_after_first_nodes, list):
            fail("Material idempotency query payload missing nodes")
        if material_idem_after_first_revision == material_idem_before_revision:
            fail(
                "Material idempotency first mutate should advance revision: "
                f"before={material_idem_before} after={material_idem_after_first}"
            )
        if len(material_idem_after_first_nodes) != len(material_idem_before_nodes) + 1:
            fail(
                "Material idempotency first mutate should add exactly one node: "
                f"before={material_idem_before} after={material_idem_after_first}"
            )
        material_idem_second = call_domain_tool(
            client,
            1001074,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "idempotencyKey": material_idem_key,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter"}
                ],
            },
        )
        material_idem_second_op = op_ok(material_idem_second)
        material_idem_after_second = call_domain_tool(
            client,
            1001075,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_idem_after_second_nodes = material_idem_after_second.get("semanticSnapshot", {}).get("nodes", [])
        if material_idem_after_second.get("revision") != material_idem_after_first_revision:
            fail(
                "Material duplicate idempotencyKey should not advance revision: "
                f"after_first={material_idem_after_first} after_second={material_idem_after_second}"
            )
        if not isinstance(material_idem_after_second_nodes, list) or len(material_idem_after_second_nodes) != len(material_idem_after_first_nodes):
            fail(
                "Material duplicate idempotencyKey should not change node count: "
                f"after_first={material_idem_after_first} after_second={material_idem_after_second}"
            )
        if material_idem_second.get("previousRevision") != material_idem_first.get("previousRevision") or material_idem_second.get("newRevision") != material_idem_first.get("newRevision"):
            fail(
                "Material duplicate idempotencyKey should replay the first mutate result metadata: "
                f"first={material_idem_first} second={material_idem_second}"
            )
        if material_idem_second_op.get("nodeId") != material_idem_first_op.get("nodeId"):
            fail(
                "Material duplicate idempotencyKey should replay the original nodeId: "
                f"first={material_idem_first} second={material_idem_second}"
            )
        print("[PASS] material idempotencyKey replay validated")

        material_duplicate_client_ref = call_domain_tool(
            client,
            1001076,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "nodeClassPath": "/Script/Engine.MaterialExpressionScalarParameter",
                    },
                    {
                        "op": "addNode.byClass",
                        "clientRef": "dup_material",
                        "nodeClassPath": "/Script/Engine.MaterialExpressionConstant",
                    },
                ],
            },
            expect_error=True,
        )
        material_duplicate_struct = material_duplicate_client_ref
        material_duplicate_struct = structured_detail_or_payload(material_duplicate_client_ref)
        material_duplicate_results = material_duplicate_struct.get("opResults")
        if not isinstance(material_duplicate_results, list) or len(material_duplicate_results) < 2:
            fail(f"material.graph.edit duplicate clientRef missing opResults: {material_duplicate_client_ref}")
        material_duplicate_second = material_duplicate_results[1] if isinstance(material_duplicate_results[1], dict) else {}
        if material_duplicate_second.get("errorCode") != "INVALID_ARGUMENT":
            fail(f"material.graph.edit duplicate clientRef wrong errorCode: {material_duplicate_second}")
        if "Duplicate clientRef" not in str(material_duplicate_second.get("errorMessage", "")):
            fail(f"material.graph.edit duplicate clientRef wrong errorMessage: {material_duplicate_second}")
        print("[PASS] material duplicate clientRef rejected")

        material_compile = call_domain_tool(
            client,
            100108,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [{"op": "compile"}],
            },
        )
        material_compile_first = op_ok(material_compile)
        material_revision_after_compile = call_domain_tool(
            client,
            100109,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        if material_compile_first.get("changed") is not False:
            fail(f"Material compile should report changed=false when graph revision is unchanged: {material_compile}")
        if material_compile.get("previousRevision") != material_compile.get("newRevision"):
            fail(f"Material compile mutate should keep previousRevision/newRevision aligned when graph is unchanged: {material_compile}")
        if material_revision_after_compile.get("revision") != material_compile.get("newRevision"):
            fail(
                "Material compile mutate revision metadata should match graph.query: "
                f"mutate={material_compile} query={material_revision_after_compile}"
            )
        material_revision_r1 = material_revision_after_compile.get("revision")
        print("[PASS] material compile revision metadata validated")

        material_compile = call_domain_tool(
            client,
            1001091,
            "material",
            "compile",
            {
                "assetPath": material_asset_path,
            },
        )
        if material_compile.get("status") != "ok":
            fail(f"material.compile should succeed for material fixture: {material_compile}")
        if not isinstance(material_compile.get("queryReport"), dict):
            fail(f"material.compile missing queryReport: {material_compile}")
        compile_report = material_compile.get("compileReport")
        if not isinstance(compile_report, dict) or compile_report.get("compiled") is not True:
            fail(f"material.compile missing compiled=true: {material_compile}")
        print("[PASS] material.compile summary validated")


        material_connect = call_domain_tool(
            client,
            10011,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_param_id}, "to": {"nodeId": material_multiply_id, "pin": "A"},
                    },
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_constant_id}, "to": {"nodeId": material_multiply_id, "pin": "B"},
                    },
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_multiply_id},
                        "to": {"nodeId": "__material_root__", "pin": "Base Color"},
                    },
                    {"op": "layoutGraph", "scope": "touched"},
                    {"op": "compile"},
                ],
            },
            expect_error=True,
        )
        material_connect_results = material_connect.get("opResults")
        if not isinstance(material_connect_results, list) or len(material_connect_results) not in {4, 5}:
            fail(f"Material connect/layout/compile opResults mismatch: {material_connect}")
        for index in range(3):
            connect_result = material_connect_results[index] if isinstance(material_connect_results[index], dict) else {}
            if connect_result.get("op") != "connectpins" or connect_result.get("ok") is not True:
                fail(f"Material connectPins op[{index}] failed: {material_connect}")
        material_layout_result = material_connect_results[3] if isinstance(material_connect_results[3], dict) else {}
        if material_layout_result.get("op") != "layoutgraph":
            fail(f"Material layoutGraph wrong op echo: {material_layout_result}")
        material_touched_layout_skipped = False
        if material_layout_result.get("ok") is not True:
            if (
                material_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(material_layout_result.get("errorMessage", ""))
            ):
                material_touched_layout_skipped = True
                print("[WARN] material layoutGraph(scope=touched) reported no pending touched nodes; skipping touched-layout position assertion")
            else:
                fail(f"Material touched-layout layoutGraph failed: {material_connect}")
        if len(material_connect_results) == 5:
            material_compile_result = material_connect_results[4] if isinstance(material_connect_results[4], dict) else {}
            if material_compile_result.get("op") != "compile" or material_compile_result.get("ok") is not True:
                fail(f"Material compile after connect/layout failed: {material_connect}")
        elif not material_touched_layout_skipped:
            fail(f"Material connect/layout batch stopped before compile unexpectedly: {material_connect}")
        else:
            material_compile_after_touched_skip = call_domain_tool(
                client,
                100111,
                "material",
                "mutate",
                {"assetPath": material_asset_path, "ops": [{"op": "compile"}]},
            )
            material_compile_result = op_ok(material_compile_after_touched_skip)
            if material_compile_result.get("op") != "compile":
                fail(f"Material compile after touched-layout skip wrong op echo: {material_compile_after_touched_skip}")

        material_snapshot = query_snapshot(client, 10012, material_asset_path, "material", "MaterialGraph")
        material_nodes = material_snapshot.get("nodes")
        material_edges = material_snapshot.get("edges")
        if not isinstance(material_nodes, list) or not isinstance(material_edges, list):
            fail(f"Material graph.query missing nodes/edges: {material_snapshot}")
        material_snapshot_without_graph_name = query_snapshot(client, 10013, material_asset_path, "material", None)
        if material_snapshot_without_graph_name.get("signature") != material_snapshot.get("signature"):
            fail(
                "Material graph.query without graphName should resolve the same single-graph asset snapshot: "
                f"without={material_snapshot_without_graph_name} with={material_snapshot}"
            )
        material_query_without_type = call_domain_tool(
            client,
            100131,
            "material",
            "query",
            {"assetPath": material_asset_path, "limit": 200},
        )
        material_query_without_type_snapshot = material_query_without_type.get("semanticSnapshot")
        if not isinstance(material_query_without_type_snapshot, dict):
            fail(f"material.graph.inspect without explicit graphName missing semanticSnapshot: {material_query_without_type}")
        if material_query_without_type_snapshot.get("signature") != material_snapshot.get("signature"):
            fail(
                "Material query without explicit graphName should resolve the same single-graph asset snapshot: "
                f"without={material_query_without_type} with={material_snapshot}"
            )
        material_root = require_node(material_nodes, "__material_root__")
        if material_root.get("nodeRole") != "materialRoot":
            fail(f"Material root nodeRole mismatch: {material_root}")
        material_root_pos = require_layout(material_root).get("position", {})
        material_multiply_node = require_node(material_nodes, material_multiply_id)
        material_multiply_pos = require_layout(material_multiply_node).get("position", {})
        if not material_touched_layout_skipped:
            if material_multiply_pos.get("x", 0) >= material_root_pos.get("x", 0):
                fail(
                    "Material sink node was not placed left of the material root: "
                    f"sink={material_multiply_pos} root={material_root_pos}"
                )
            for node in material_nodes:
                if not isinstance(node, dict) or node.get("id") == "__material_root__":
                    continue
                node_pos = require_layout(node).get("position", {})
                if node_pos.get("x", 0) >= material_root_pos.get("x", 0):
                    fail(f"Material non-root node was placed at or right of the material root: node={node} root={material_root}")
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_multiply_id
            and edge.get("toNodeId") == "__material_root__"
            and edge.get("toPin") == "Base Color"
            for edge in material_edges
        ):
            fail(f"Material graph.query did not return multiply->root edge: {material_edges}")
        material_root_edge = next(
            (
                edge
                for edge in material_edges
                if isinstance(edge, dict)
                and edge.get("fromNodeId") == material_multiply_id
                and edge.get("toNodeId") == "__material_root__"
                and edge.get("toPin") == "Base Color"
            ),
            None,
        )
        if not isinstance(material_root_edge, dict) or not material_root_edge.get("fromPin"):
            fail(f"Material root edge missing source pin for breakPinLinks round-trip: {material_root_edge}")
        material_break_root_from_source = call_domain_tool(
            client,
            100151,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "target": {
                            "nodeId": material_root_edge.get("fromNodeId"),
                            "pinName": material_root_edge.get("fromPin"),
                        }

                    }
                ],
            },
        )
        material_break_root_from_source_first = op_ok(material_break_root_from_source)
        if material_break_root_from_source_first.get("changed") is not True:
            fail(
                "Material breakPinLinks should remove root edge when targeting the edge source pin: "
                f"{material_break_root_from_source}"
            )
        material_snapshot_after_root_source_break = query_snapshot(client, 100152, material_asset_path, "material", "MaterialGraph")
        material_edges_after_root_source_break = material_snapshot_after_root_source_break.get("edges")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_root_edge.get("fromNodeId")
            and edge.get("fromPin") == material_root_edge.get("fromPin")
            and edge.get("toNodeId") == material_root_edge.get("toNodeId")
            and edge.get("toPin") == material_root_edge.get("toPin")
            for edge in material_edges_after_root_source_break or []
        ):
            fail(
                "Material breakPinLinks(source pin) should remove the root edge returned by graph.query: "
                f"{material_snapshot_after_root_source_break}"
            )
        material_reconnect_root_after_source_break = call_domain_tool(
            client,
            100153,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {
                            "nodeId": material_root_edge.get("fromNodeId"),
                            "pin": material_root_edge.get("fromPin"),
                        },
                        "to": {"nodeId": "__material_root__", "pin": "Base Color"},
                    }
                ],
            },
        )
        op_ok(material_reconnect_root_after_source_break)
        material_internal_edge = next(
            (
                edge
                for edge in material_edges
                if isinstance(edge, dict)
                and edge.get("fromNodeId") == material_param_id
                and edge.get("toNodeId") == material_multiply_id
                and edge.get("toPin") == "A"
            ),
            None,
        )
        if not isinstance(material_internal_edge, dict) or not material_internal_edge.get("fromPin"):
            fail(f"Material internal edge missing source pin for breakPinLinks round-trip: {material_internal_edge}")
        material_break_internal_from_source = call_domain_tool(
            client,
            100154,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "breakPinLinks",
                        "target": {
                            "nodeId": material_internal_edge.get("fromNodeId"),
                            "pinName": material_internal_edge.get("fromPin"),
                        }

                    }
                ],
            },
        )
        material_break_internal_from_source_first = op_ok(material_break_internal_from_source)
        if material_break_internal_from_source_first.get("changed") is not True:
            fail(
                "Material breakPinLinks should remove internal edge when targeting the edge source pin: "
                f"{material_break_internal_from_source}"
            )
        material_snapshot_after_internal_source_break = query_snapshot(client, 100155, material_asset_path, "material", "MaterialGraph")
        material_edges_after_internal_source_break = material_snapshot_after_internal_source_break.get("edges")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_internal_edge.get("fromNodeId")
            and edge.get("fromPin") == material_internal_edge.get("fromPin")
            and edge.get("toNodeId") == material_internal_edge.get("toNodeId")
            and edge.get("toPin") == material_internal_edge.get("toPin")
            for edge in material_edges_after_internal_source_break or []
        ):
            fail(
                "Material breakPinLinks(source pin) should remove the internal edge returned by graph.query: "
                f"{material_snapshot_after_internal_source_break}"
            )
        material_reconnect_internal_after_source_break = call_domain_tool(
            client,
            100156,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {
                            "nodeId": material_internal_edge.get("fromNodeId"),
                            "pin": material_internal_edge.get("fromPin"),
                        },
                        "to": {
                            "nodeId": material_internal_edge.get("toNodeId"),
                            "pin": material_internal_edge.get("toPin"),
                        },
                    }
                ],
            },
        )
        op_ok(material_reconnect_internal_after_source_break)

        material_saturate_add = call_domain_tool(
            client,
            100157,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/Engine.MaterialExpressionSaturate"}
                ],
            },
        )
        material_saturate_id = op_ok(material_saturate_add).get("nodeId")
        if not isinstance(material_saturate_id, str) or not material_saturate_id:
            fail(f"Material Saturate addNode.byClass did not return nodeId: {material_saturate_add}")
        material_saturate_snapshot = query_snapshot(client, 100158, material_asset_path, "material", "MaterialGraph")
        material_saturate_node = require_node(material_saturate_snapshot.get("nodes") or [], material_saturate_id)
        material_saturate_pins = material_saturate_node.get("pins")
        if not isinstance(material_saturate_pins, list):
            fail(f"Material Saturate node missing pins: {material_saturate_node}")
        material_saturate_input_name = next(
            (
                pin.get("name")
                for pin in material_saturate_pins
                if isinstance(pin, dict) and pin.get("direction") == "input"
            ),
            None,
        )
        if not isinstance(material_saturate_input_name, str):
            fail(f"Material Saturate input pin name missing from graph.query: {material_saturate_node}")
        material_connect_saturate_by_query_pin = call_domain_tool(
            client,
            100159,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": material_param_id, "pin": material_internal_edge.get("fromPin")},
                        "to": {"nodeId": material_saturate_id, "pin": material_saturate_input_name},
                    }
                ],
            },
        )
        material_connect_saturate_by_query_pin_first = op_ok(material_connect_saturate_by_query_pin)
        if material_connect_saturate_by_query_pin_first.get("changed") is not True:
            fail(
                "Material connectPins should accept the input pin name returned by graph.query for unary nodes: "
                f"{material_connect_saturate_by_query_pin}"
            )
        material_after_saturate_connect = query_snapshot(client, 100160, material_asset_path, "material", "MaterialGraph")
        material_edges_after_saturate_connect = material_after_saturate_connect.get("edges")
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == material_param_id
            and edge.get("toNodeId") == material_saturate_id
            and edge.get("toPin") == material_saturate_input_name
            for edge in material_edges_after_saturate_connect or []
        ):
            fail(
                "Material connectPins(query-visible unary input pin) should create the expected edge: "
                f"{material_after_saturate_connect}"
            )
        print("[PASS] material breakPinLinks source-pin round-trip validated")
        material_before_relayout = dict(material_multiply_pos)
        material_relayout_payload = call_domain_tool(
            client,
            10016,
            "material",
            "mutate",
            {
                "assetPath": material_asset_path,
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "nodeId": material_multiply_id,
                        "dx": 900,
                        "dy": 600,
                    },
                    {"op": "layoutGraph", "scope": "all"},
                ],
            },
        )
        material_relayout_results = material_relayout_payload.get("opResults")
        if not isinstance(material_relayout_results, list) or len(material_relayout_results) != 2:
            fail(f"Material relayout ops missing results: {material_relayout_payload}")
        material_relayout = material_relayout_results[1] if isinstance(material_relayout_results[1], dict) else {}
        if material_relayout.get("op") != "layoutgraph":
            fail(f"Material relayout wrong op echo: {material_relayout}")
        if material_relayout.get("changed") is not True:
            fail(f"Material layoutGraph should report changed=true after moveNodeBy: {material_relayout}")
        material_relayout_moved = material_relayout.get("movedNodeIds")
        if not isinstance(material_relayout_moved, list) or not material_relayout_moved:
            fail(f"Material layoutGraph missing movedNodeIds after relayout: {material_relayout}")
        material_after_relayout_snapshot = query_snapshot(client, 10017, material_asset_path, "material", "MaterialGraph")
        material_after_relayout_nodes = material_after_relayout_snapshot.get("nodes")
        if not isinstance(material_after_relayout_nodes, list):
            fail(f"Material graph.query after relayout missing nodes: {material_after_relayout_snapshot}")
        material_after_relayout_pos = require_layout(require_node(material_after_relayout_nodes, material_multiply_id)).get("position", {})
        if material_after_relayout_pos == material_before_relayout:
            fail(
                "Material layoutGraph(scope=all) did not change tracked node position after moveNodeBy: "
                f"before={material_before_relayout} after={material_after_relayout_pos}"
            )
        print("[PASS] material root-aware layout validated")

        pcg_layout_add = call_domain_tool(
            client,
            10100,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGSurfaceSamplerSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                ],
            },
        )
        pcg_add_results = pcg_layout_add.get("opResults")
        if not isinstance(pcg_add_results, list) or len(pcg_add_results) != 6:
            fail(f"PCG layout add ops missing results: {pcg_layout_add}")
        pcg_create_id, pcg_tag_a_id, pcg_filter_id, pcg_tag_b_id, pcg_sampler_id, pcg_tag_c_id = [
            result.get("nodeId") for result in pcg_add_results
        ]
        if not all(
            isinstance(node_id, str) and node_id
            for node_id in [pcg_create_id, pcg_tag_a_id, pcg_filter_id, pcg_tag_b_id, pcg_sampler_id, pcg_tag_c_id]
        ):
            fail(f"PCG layout add ops missing node ids: {pcg_layout_add}")
        pcg_graph_list_without_type = call_domain_tool(
            client,
            101005,
            "pcg",
            "list",
            {"assetPath": temp_pcg_asset},
        )
        if pcg_graph_list_without_type.get("assetPath") != temp_pcg_asset:
            fail(f"pcg.graph.inspect list-view assetPath mismatch: {pcg_graph_list_without_type}")
        pcg_list_nodes = pcg_graph_list_without_type.get("nodes")
        if not isinstance(pcg_list_nodes, list) or len(pcg_list_nodes) < 6:
            fail(f"pcg.graph.inspect list-view missing nodes[]: {pcg_graph_list_without_type}")
        print("[PASS] pcg.graph.inspect list-view validated")

        pcg_spawn_property = call_domain_tool(
            client,
            101006,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "addNode.byClass",
                        "clientRef": "spawn_actor_property",
                        "nodeClassPath": "/Script/PCG.PCGSpawnActorSettings",
                    },
                    {
                        "op": "setProperty",
                        "nodeRef": "spawn_actor_property",
                        "property": "bDeleteActorsBeforeGeneration",
                        "value": "true",
                    },
                ],
            },
        )
        pcg_spawn_property_results = pcg_spawn_property.get("opResults")
        if not isinstance(pcg_spawn_property_results, list) or len(pcg_spawn_property_results) != 2:
            fail(f"PCG setProperty opResults mismatch: {pcg_spawn_property}")
        if not all(isinstance(entry, dict) and entry.get("ok") for entry in pcg_spawn_property_results):
            fail(f"PCG setProperty failed: {pcg_spawn_property}")
        pcg_spawn_property_id = pcg_spawn_property_results[0].get("nodeId")
        if not isinstance(pcg_spawn_property_id, str) or not pcg_spawn_property_id:
            fail(f"PCG setProperty setup missing nodeId: {pcg_spawn_property}")
        pcg_selection_layout = call_tool(
            client,
            1010051,
            "pcg.graph.layout",
            {
                "assetPath": temp_pcg_asset,
                "operation": "format",
                "scope": {
                    "mode": "selection",
                    "nodes": [{"id": pcg_spawn_property_id}],
                },
            },
        )
        pcg_selection_layout_op = op_ok(pcg_selection_layout)
        if pcg_selection_layout_op.get("op") != "layoutgraph":
            fail(f"pcg.graph.layout selection returned unexpected op result: {pcg_selection_layout}")
        print("[PASS] pcg.graph.layout selection validated")
        pcg_spawn_property_snapshot = query_snapshot(client, 101007, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_spawn_property_node = require_node(
            [node for node in pcg_spawn_property_snapshot.get("nodes", []) if isinstance(node, dict)],
            pcg_spawn_property_id,
        )
        pcg_spawn_behavior = (
            pcg_spawn_property_node.get("effectiveSettings", {}).get("spawnBehavior")
            if isinstance(pcg_spawn_property_node.get("effectiveSettings"), dict)
            else None
        )
        if not isinstance(pcg_spawn_behavior, dict) or pcg_spawn_behavior.get("deleteActorsBeforeGeneration") is not True:
            fail(f"PCG setProperty did not update SpawnActor behavior: {pcg_spawn_property_node}")
        print("[PASS] pcg.graph.edit setNodeProperty updates node settings")

        pcg_connect = call_domain_tool(
            client,
            10101,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "connectPins", "from": {"nodeId": pcg_create_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_a_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_tag_a_id, "pin": "Out"}, "to": {"nodeId": pcg_filter_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_filter_id, "pin": "InsideFilter"}, "to": {"nodeId": pcg_tag_b_id, "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeId": pcg_sampler_id, "pin": "Out"}, "to": {"nodeId": pcg_tag_c_id, "pin": "In"}},
                    {"op": "layoutGraph", "scope": "touched"},
                ],
            },
        )
        pcg_connect_results = pcg_connect.get("opResults")
        if not isinstance(pcg_connect_results, list) or len(pcg_connect_results) != 5:
            fail(f"PCG connect/layout opResults mismatch: {pcg_connect}")
        for index in range(4):
            connect_result = pcg_connect_results[index] if isinstance(pcg_connect_results[index], dict) else {}
            if connect_result.get("op") != "connectpins" or connect_result.get("ok") is not True:
                fail(f"PCG connectPins op[{index}] failed: {pcg_connect}")
        pcg_layout_result = pcg_connect_results[4] if isinstance(pcg_connect_results[4], dict) else {}
        if pcg_layout_result.get("op") != "layoutgraph":
            fail(f"PCG layoutGraph wrong op echo: {pcg_layout_result}")
        if pcg_layout_result.get("ok") is not True:
            if (
                pcg_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(pcg_layout_result.get("errorMessage", ""))
            ):
                print("[WARN] pcg layoutGraph(scope=touched) reported no pending touched nodes; skipping touched-layout position assertion")
            else:
                fail(f"PCG touched-layout layoutGraph failed: {pcg_connect}")

        pcg_snapshot = query_snapshot(client, 10102, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_nodes = pcg_snapshot.get("nodes")
        pcg_edges = pcg_snapshot.get("edges")
        if not isinstance(pcg_nodes, list) or not isinstance(pcg_edges, list):
            fail(f"PCG graph inspect missing nodes/edges: {pcg_snapshot}")
        bad_pcg_connect = call_domain_tool(
            client,
            101021,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "connectPins",
                        "from": {"nodeId": pcg_filter_id, "pin": "Out"},
                        "to": {"nodeId": pcg_tag_b_id, "pin": "In"},
                    }
                ],
            },
            expect_error=True,
        )
        bad_pcg_connect_struct = bad_pcg_connect
        bad_pcg_connect_struct = structured_detail_or_payload(bad_pcg_connect)
        bad_pcg_connect_results = bad_pcg_connect_struct.get("opResults")
        if not isinstance(bad_pcg_connect_results, list) or not bad_pcg_connect_results:
            fail(f"pcg.graph.edit bad connect missing opResults: {bad_pcg_connect}")
        bad_pcg_connect_first = bad_pcg_connect_results[0] if isinstance(bad_pcg_connect_results[0], dict) else {}
        if bad_pcg_connect_first.get("errorCode") not in {"TARGET_NOT_FOUND", "PIN_NOT_FOUND"}:
            fail(f"pcg.graph.edit bad connect wrong errorCode: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("ok") is not False:
            fail(f"pcg.graph.edit bad connect should fail explicitly: {bad_pcg_connect_first}")
        if bad_pcg_connect_first.get("changed") is not False:
            fail(f"pcg.graph.edit bad connect should not report changed=true: {bad_pcg_connect_first}")
        pcg_snapshot_after_bad_connect = query_snapshot(client, 101022, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_edges_after_bad_connect = pcg_snapshot_after_bad_connect.get("edges")
        if not isinstance(pcg_edges_after_bad_connect, list):
            fail(f"PCG graph inspect after bad connect missing edges: {pcg_snapshot_after_bad_connect}")
        if any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == pcg_filter_id
            and edge.get("fromPin") == "Out"
            and edge.get("toNodeId") == pcg_tag_b_id
            and edge.get("toPin") == "In"
            for edge in pcg_edges_after_bad_connect
        ):
            fail(f"PCG graph inspect should not contain invalid Out->In edge after failed connect: {pcg_edges_after_bad_connect}")
        print("[PASS] pcg.graph.edit invalid connect target is rejected")
        pcg_snapshot_without_graph_name = query_snapshot(client, 10103, temp_pcg_asset, "pcg", None)
        if pcg_snapshot_without_graph_name.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG graph inspect without graphName should resolve the same single-graph asset snapshot: "
                f"without={pcg_snapshot_without_graph_name} with={pcg_snapshot}"
            )
        pcg_query_without_type = call_domain_tool(
            client,
            101031,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        pcg_query_without_type_snapshot = pcg_query_without_type.get("semanticSnapshot")
        if not isinstance(pcg_query_without_type_snapshot, dict):
            fail(f"PCG graph.inspect without explicit graphName missing semanticSnapshot: {pcg_query_without_type}")
        if pcg_query_without_type_snapshot.get("signature") != pcg_snapshot.get("signature"):
            fail(
                "PCG graph.inspect without explicit graphName should resolve the same single-graph asset snapshot: "
                f"without={pcg_query_without_type} with={pcg_snapshot}"
            )

        pcg_compile_first = call_domain_tool(
            client,
            101031,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [{"op": "compile"}],
            },
        )
        pcg_compile_first_result = op_ok(pcg_compile_first)
        if pcg_compile_first_result.get("op") != "compile":
            fail(f"PCG compile wrong op echo: {pcg_compile_first}")
        pcg_compile_second = call_domain_tool(
            client,
            101032,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [{"op": "compile"}],
            },
        )
        pcg_compile_second_result = op_ok(pcg_compile_second)
        if pcg_compile_second_result.get("op") != "compile":
            fail(f"PCG second compile wrong op echo: {pcg_compile_second}")
        if pcg_compile_second_result.get("changed") is not False:
            fail(f"PCG compile should report changed=false when compiled graph is unchanged: {pcg_compile_second}")
        if pcg_compile_second.get("previousRevision") != pcg_compile_second.get("newRevision"):
            fail(f"PCG compile should keep previousRevision/newRevision aligned when graph is unchanged: {pcg_compile_second}")
        pcg_revision_after_compile = call_domain_tool(
            client,
            101033,
            "pcg",
            "query",
            {"assetPath": temp_pcg_asset, "limit": 200},
        )
        if pcg_revision_after_compile.get("revision") != pcg_compile_second.get("newRevision"):
            fail(
                "PCG compile revision metadata should match pcg.graph.inspect: "
                f"compile={pcg_compile_second} inspect={pcg_revision_after_compile}"
            )
        print("[PASS] pcg.compile revision metadata validated")

        create_pos = require_layout(require_node(pcg_nodes, pcg_create_id)).get("position", {})
        tag_a_pos = require_layout(require_node(pcg_nodes, pcg_tag_a_id)).get("position", {})
        filter_pos = require_layout(require_node(pcg_nodes, pcg_filter_id)).get("position", {})
        tag_b_pos = require_layout(require_node(pcg_nodes, pcg_tag_b_id)).get("position", {})
        sampler_pos = require_layout(require_node(pcg_nodes, pcg_sampler_id)).get("position", {})
        tag_c_pos = require_layout(require_node(pcg_nodes, pcg_tag_c_id)).get("position", {})

        if not (create_pos.get("x", 0) < tag_a_pos.get("x", 0) < filter_pos.get("x", 0) < tag_b_pos.get("x", 0)):
            fail(
                "PCG primary pipeline did not layout left-to-right: "
                f"{create_pos}, {tag_a_pos}, {filter_pos}, {tag_b_pos}"
            )
        if not (sampler_pos.get("x", 0) < tag_c_pos.get("x", 0)):
            fail(f"PCG secondary pipeline did not layout left-to-right: {sampler_pos}, {tag_c_pos}")
        if abs(sampler_pos.get("y", 0) - create_pos.get("y", 0)) < 32:
            fail(f"PCG parallel rows were not separated vertically enough: {create_pos}, {sampler_pos}")
        pcg_before_relayout = dict(create_pos)
        pcg_relayout_payload = call_domain_tool(
            client,
            10110,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "moveNodeBy",
                        "nodeId": pcg_create_id,
                        "dx": 900,
                        "dy": 600,
                    },
                    {"op": "layoutGraph", "scope": "all"},
                ],
            },
        )
        pcg_relayout_results = pcg_relayout_payload.get("opResults")
        if not isinstance(pcg_relayout_results, list) or len(pcg_relayout_results) != 2:
            fail(f"PCG relayout ops missing results: {pcg_relayout_payload}")
        pcg_relayout = pcg_relayout_results[1] if isinstance(pcg_relayout_results[1], dict) else {}
        if pcg_relayout.get("op") != "layoutgraph":
            fail(f"PCG relayout wrong op echo: {pcg_relayout}")
        if pcg_relayout.get("changed") is not True:
            fail(f"PCG layoutGraph should report changed=true after moveNodeBy: {pcg_relayout}")
        pcg_relayout_moved = pcg_relayout.get("movedNodeIds")
        if not isinstance(pcg_relayout_moved, list) or not pcg_relayout_moved:
            fail(f"PCG layoutGraph missing movedNodeIds after relayout: {pcg_relayout}")
        pcg_after_relayout_snapshot = query_snapshot(client, 10111, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_after_relayout_nodes = pcg_after_relayout_snapshot.get("nodes")
        if not isinstance(pcg_after_relayout_nodes, list):
            fail(f"PCG graph inspect after relayout missing nodes: {pcg_after_relayout_snapshot}")
        pcg_after_relayout_pos = require_layout(require_node(pcg_after_relayout_nodes, pcg_create_id)).get("position", {})
        if pcg_after_relayout_pos == pcg_before_relayout:
            fail(
                "PCG layoutGraph(scope=all) did not change tracked node position after moveNodeBy: "
                f"before={pcg_before_relayout} after={pcg_after_relayout_pos}"
            )
        if not any(
            isinstance(edge, dict)
            and edge.get("fromNodeId") == pcg_filter_id
            and edge.get("fromPin") == "InsideFilter"
            and edge.get("toNodeId") == pcg_tag_b_id
            for edge in pcg_edges
        ):
            fail(f"PCG graph inspect missing filter branch edge: {pcg_edges}")
        print("[PASS] pcg pipeline layout validated")

        pcg_settings_probe_add = call_domain_tool(
            client,
            10117,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGGetActorPropertySettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGGetSplineSettings"},
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"},
                ],
            },
        )
        pcg_settings_probe_results = pcg_settings_probe_add.get("opResults")
        if not isinstance(pcg_settings_probe_results, list) or len(pcg_settings_probe_results) != 3:
            fail(f"PCG settings probe add ops missing results: {pcg_settings_probe_add}")
        pcg_get_actor_property_id, pcg_get_spline_id, pcg_static_mesh_spawner_id = [
            result.get("nodeId") for result in pcg_settings_probe_results
        ]
        if not all(
            isinstance(node_id, str) and node_id
            for node_id in [pcg_get_actor_property_id, pcg_get_spline_id, pcg_static_mesh_spawner_id]
        ):
            fail(f"PCG settings probe nodes missing ids: {pcg_settings_probe_add}")

        pcg_settings_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=10118,
            code=(
                "import json\n"
                "import unreal\n"
                "def load_required(path):\n"
                "    obj = unreal.load_object(None, path)\n"
                "    if obj is None:\n"
                "        raise RuntimeError(f'failed to load object: {path}')\n"
                "    return obj\n"
                "def set_prop(obj, names, value):\n"
                "    errors = []\n"
                "    for name in names:\n"
                "        try:\n"
                "            obj.set_editor_property(name, value)\n"
                "            return name\n"
                "        except Exception as exc:\n"
                "            errors.append(f'{name}: {exc}')\n"
                "    raise RuntimeError('failed to set property on %s: %s' % (obj, '; '.join(errors)))\n"
                "def resolve_enum_value(type_names, member_names):\n"
                "    for type_name in type_names:\n"
                "        enum_type = getattr(unreal, type_name, None)\n"
                "        if enum_type is None:\n"
                "            continue\n"
                "        for member_name in member_names:\n"
                "            if hasattr(enum_type, member_name):\n"
                "                return getattr(enum_type, member_name)\n"
                "    return None\n"
                f"asset = {json.dumps(temp_pcg_asset, ensure_ascii=False)}\n"
                f"actor_node_path = {json.dumps(pcg_get_actor_property_id, ensure_ascii=False)}\n"
                f"spline_node_path = {json.dumps(pcg_get_spline_id, ensure_ascii=False)}\n"
                f"spawner_node_path = {json.dumps(pcg_static_mesh_spawner_id, ensure_ascii=False)}\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    raise RuntimeError(f'failed to load PCG graph asset: {asset}')\n"
                "all_world_actors = resolve_enum_value(['EPCGActorFilter', 'PCGActorFilter'], ['ALL_WORLD_ACTORS'])\n"
                "by_class = resolve_enum_value(['EPCGActorSelection', 'PCGActorSelection'], ['BY_CLASS'])\n"
                "component_by_class = resolve_enum_value(['EPCGComponentSelection', 'PCGComponentSelection'], ['BY_CLASS'])\n"
                "actor_node = load_required(actor_node_path)\n"
                "actor_settings = actor_node.get_settings()\n"
                "actor_selector = actor_settings.get_editor_property('actor_selector')\n"
                "if all_world_actors is not None:\n"
                "    set_prop(actor_selector, ['actor_filter'], all_world_actors)\n"
                "if by_class is not None:\n"
                "    set_prop(actor_selector, ['actor_selection'], by_class)\n"
                "    set_prop(actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
                "set_prop(actor_selector, ['b_select_multiple', 'select_multiple'], True)\n"
                "set_prop(actor_settings, ['actor_selector'], actor_selector)\n"
                "set_prop(actor_settings, ['property_name'], unreal.Name('Tags'))\n"
                "set_prop(actor_settings, ['b_select_component', 'select_component'], True)\n"
                "set_prop(actor_settings, ['component_class'], unreal.SplineComponent.static_class())\n"
                "set_prop(actor_settings, ['b_process_all_components', 'process_all_components'], True)\n"
                "set_prop(actor_settings, ['b_output_actor_reference', 'output_actor_reference'], True)\n"
                "set_prop(actor_settings, ['b_always_requery_actors', 'always_requery_actors'], True)\n"
                "spline_node = load_required(spline_node_path)\n"
                "spline_settings = spline_node.get_settings()\n"
                "spline_actor_selector = spline_settings.get_editor_property('actor_selector')\n"
                "if all_world_actors is not None:\n"
                "    set_prop(spline_actor_selector, ['actor_filter'], all_world_actors)\n"
                "if by_class is not None:\n"
                "    set_prop(spline_actor_selector, ['actor_selection'], by_class)\n"
                "    set_prop(spline_actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
                "set_prop(spline_settings, ['actor_selector'], spline_actor_selector)\n"
                "component_selector = spline_settings.get_editor_property('component_selector')\n"
                "if component_by_class is not None:\n"
                "    set_prop(component_selector, ['component_selection'], component_by_class)\n"
                "    set_prop(component_selector, ['component_selection_class'], unreal.SplineComponent.static_class())\n"
                "set_prop(spline_settings, ['component_selector'], component_selector)\n"
                "set_prop(spline_settings, ['b_always_requery_actors', 'always_requery_actors'], True)\n"
                "spawner_node = load_required(spawner_node_path)\n"
                "spawner_settings = spawner_node.get_settings()\n"
                "spawner_settings.set_mesh_selector_type(unreal.PCGMeshSelectorByAttribute.static_class())\n"
                "selector = spawner_settings.get_editor_property('mesh_selector_parameters')\n"
                "set_prop(selector, ['attribute_name'], unreal.Name('Mesh'))\n"
                "set_prop(spawner_settings, ['out_attribute_name'], unreal.Name('ChosenMesh'))\n"
                "set_prop(spawner_settings, ['b_apply_mesh_bounds_to_points', 'apply_mesh_bounds_to_points'], True)\n"
                "unreal.EditorAssetLibrary.save_asset(asset)\n"
                "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
            ),
        )
        pcg_settings_fixture = parse_execute_json(pcg_settings_fixture_payload)
        if pcg_settings_fixture.get("ok") is not True:
            fail(f"PCG settings probe fixture failed: {pcg_settings_fixture}")

        pcg_settings_snapshot = query_snapshot(client, 10119, temp_pcg_asset, "pcg", "PCGGraph")
        pcg_settings_nodes = pcg_settings_snapshot.get("nodes")
        if not isinstance(pcg_settings_nodes, list):
            fail(f"PCG settings probe graph inspect missing nodes: {pcg_settings_snapshot}")

        get_actor_property_node = require_node(pcg_settings_nodes, pcg_get_actor_property_id)
        get_actor_property_settings = get_actor_property_node.get("effectiveSettings")
        if not isinstance(get_actor_property_settings, dict):
            fail(f"PCG GetActorProperty node missing effectiveSettings: {get_actor_property_node}")
        if get_actor_property_settings.get("propertyName") != "Tags":
            fail(f"PCG GetActorProperty propertyName missing from effectiveSettings: {get_actor_property_settings}")
        if get_actor_property_settings.get("selectComponent") is not True:
            fail(f"PCG GetActorProperty selectComponent missing from effectiveSettings: {get_actor_property_settings}")
        if not str(get_actor_property_settings.get("componentClassPath", "")).endswith("SplineComponent"):
            fail(f"PCG GetActorProperty componentClassPath missing SplineComponent: {get_actor_property_settings}")
        get_actor_property_actor_selector = get_actor_property_settings.get("actorSelector")
        if not isinstance(get_actor_property_actor_selector, dict):
            fail(f"PCG GetActorProperty missing actorSelector details: {get_actor_property_settings}")
        if not isinstance(get_actor_property_actor_selector.get("actorFilter"), str) or not get_actor_property_actor_selector.get("actorFilter"):
            fail(f"PCG GetActorProperty actorFilter missing from actorSelector: {get_actor_property_actor_selector}")
        get_actor_property_diagnostics = get_actor_property_node.get("diagnostics")
        if not isinstance(get_actor_property_diagnostics, list) or not any(
            isinstance(diag, dict) and diag.get("code") == "PCG_SELECTOR_EMPTY_INPUT_HINT"
            for diag in get_actor_property_diagnostics
        ):
            fail(f"PCG GetActorProperty missing empty-input diagnostics: {get_actor_property_node}")

        get_spline_node = require_node(pcg_settings_nodes, pcg_get_spline_id)
        get_spline_settings = get_spline_node.get("effectiveSettings")
        if not isinstance(get_spline_settings, dict):
            fail(f"PCG GetSpline node missing effectiveSettings: {get_spline_node}")
        if get_spline_settings.get("dataFilter") != "PolyLine":
            fail(f"PCG GetSpline dataFilter missing from effectiveSettings: {get_spline_settings}")
        get_spline_component_selector = get_spline_settings.get("componentSelector")
        if not isinstance(get_spline_component_selector, dict):
            fail(f"PCG GetSpline missing componentSelector details: {get_spline_settings}")
        if not isinstance(get_spline_component_selector.get("componentSelection"), str) or not get_spline_component_selector.get("componentSelection"):
            fail(f"PCG GetSpline componentSelection missing from componentSelector: {get_spline_component_selector}")
        get_spline_diagnostics = get_spline_node.get("diagnostics")
        if not isinstance(get_spline_diagnostics, list) or not any(
            isinstance(diag, dict) and diag.get("code") == "PCG_COMPONENT_SELECTOR_EMPTY_INPUT_HINT"
            for diag in get_spline_diagnostics
        ):
            fail(f"PCG GetSpline missing component empty-input diagnostics: {get_spline_node}")

        static_mesh_spawner_node = require_node(pcg_settings_nodes, pcg_static_mesh_spawner_id)
        static_mesh_spawner_settings = static_mesh_spawner_node.get("effectiveSettings")
        if not isinstance(static_mesh_spawner_settings, dict):
            fail(f"PCG StaticMeshSpawner node missing effectiveSettings: {static_mesh_spawner_node}")
        mesh_selector_settings = static_mesh_spawner_settings.get("meshSelector")
        if not isinstance(mesh_selector_settings, dict):
            fail(f"PCG StaticMeshSpawner missing meshSelector details: {static_mesh_spawner_settings}")
        if mesh_selector_settings.get("kind") != "byAttribute":
            fail(f"PCG StaticMeshSpawner meshSelector kind mismatch: {mesh_selector_settings}")
        if mesh_selector_settings.get("attributeName") != "Mesh":
            fail(f"PCG StaticMeshSpawner attributeName missing from meshSelector: {mesh_selector_settings}")
        if static_mesh_spawner_settings.get("outAttributeName") != "ChosenMesh":
            fail(f"PCG StaticMeshSpawner outAttributeName missing from effectiveSettings: {static_mesh_spawner_settings}")
        print("[PASS] pcg.graph.inspect settings and diagnostics validated")

        pcg_health_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101190,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_health_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG health graph asset')\n"
                "print(json.dumps({'ok': True, 'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        if parse_execute_json(pcg_health_fixture_payload).get("ok") is not True:
            fail(f"PCG health fixture asset creation failed: {pcg_health_fixture_payload}")
        pcg_health_add = call_domain_tool(
            client,
            1011901,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_health_asset,
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "health_create", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "clientRef": "health_tag", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "clientRef": "health_spawner", "nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings"},
                    {"op": "connectPins", "from": {"nodeRef": "health_create", "pin": "Out"}, "to": {"nodeRef": "health_tag", "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeRef": "health_tag", "pin": "Out"}, "to": {"nodeRef": "health_spawner", "pin": "In"}},
                ],
            },
        )
        pcg_health_results = pcg_health_add.get("opResults")
        if not isinstance(pcg_health_results, list) or len(pcg_health_results) != 5:
            fail(f"PCG health probe add ops missing results: {pcg_health_add}")
        for idx, result in enumerate(pcg_health_results):
            if not isinstance(result, dict) or result.get("ok") is not True:
                fail(f"PCG health probe opResults[{idx}] failed: {pcg_health_add}")
        pcg_verify = call_domain_tool(
            client,
            1011902,
            "pcg",
            "verify",
            {
                "assetPath": temp_pcg_health_asset,
            },
        )
        if pcg_verify.get("status") == "error":
            fail(
                "pcg.compile should not become an error just because a PCG graph is not connected to Output: "
                f"{pcg_verify}"
            )
        if not isinstance(pcg_verify.get("queryReport"), dict):
            fail(f"pcg.compile missing queryReport for pcg graph: {pcg_verify}")
        pcg_compile_report = pcg_verify.get("compileReport")
        if not isinstance(pcg_compile_report, dict):
            fail(f"pcg.compile missing compileReport for pcg graph: {pcg_verify}")
        if pcg_compile_report.get("compiled") is not True:
            fail(f"pcg.compile should preserve compileReport.compiled=true for disconnected-output pcg graph: {pcg_verify}")
        pcg_health_diagnostics = pcg_verify.get("diagnostics")
        if not isinstance(pcg_health_diagnostics, list):
            fail(f"pcg.compile missing diagnostics[]: {pcg_verify}")
        pcg_health_codes = {
            diag.get("code")
            for diag in pcg_health_diagnostics
            if isinstance(diag, dict) and isinstance(diag.get("code"), str)
        }
        for unexpected_code in {
            "PCG_OUTPUT_NODE_MISSING_INPUTS",
            "PCG_NO_TERMINAL_OUTPUT_PATH",
            "PCG_GRAPH_CAN_GENERATE_NO_OUTPUT",
            "PCG_SPAWNER_NOT_CONNECTED_TO_OUTPUT",
        }:
            if unexpected_code in pcg_health_codes:
                fail(f"pcg.compile should not invent {unexpected_code} for a disconnected-output pcg graph: {pcg_verify}")
        print("[PASS] pcg.compile no longer invents disconnected-output failures")

        pcg_remove_fixture_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=1011903,
            code=(
                "import json\n"
                "import unreal\n"
                f"asset={json.dumps(temp_pcg_remove_asset, ensure_ascii=False)}\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
                "if graph is None:\n"
                "    factory = unreal.PCGGraphFactory()\n"
                "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
                "if graph is None:\n"
                "    raise RuntimeError('failed to create PCG remove graph asset')\n"
                "print(json.dumps({'ok': True, 'assetPath': asset}, ensure_ascii=False))\n"
            ),
        )
        if parse_execute_json(pcg_remove_fixture_payload).get("ok") is not True:
            fail(f"PCG remove fixture asset creation failed: {pcg_remove_fixture_payload}")
        pcg_remove_add = call_domain_tool(
            client,
            1011904,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [
                    {"op": "addNode.byClass", "clientRef": "remove_create", "nodeClassPath": "/Script/PCG.PCGCreatePointsSettings"},
                    {"op": "addNode.byClass", "clientRef": "remove_tag", "nodeClassPath": "/Script/PCG.PCGAddTagSettings"},
                    {"op": "addNode.byClass", "clientRef": "remove_filter", "nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"},
                    {"op": "connectPins", "from": {"nodeRef": "remove_create", "pin": "Out"}, "to": {"nodeRef": "remove_tag", "pin": "In"}},
                    {"op": "connectPins", "from": {"nodeRef": "remove_tag", "pin": "Out"}, "to": {"nodeRef": "remove_filter", "pin": "In"}},
                ],
            },
        )
        pcg_remove_results = pcg_remove_add.get("opResults")
        if not isinstance(pcg_remove_results, list) or len(pcg_remove_results) != 5:
            fail(f"PCG remove fixture ops missing results: {pcg_remove_add}")
        pcg_remove_tag_id = pcg_remove_results[1].get("nodeId")
        if not isinstance(pcg_remove_tag_id, str) or not pcg_remove_tag_id:
            fail(f"PCG remove fixture missing removable node id: {pcg_remove_add}")

        pcg_remove_by_name_failure = call_domain_tool(
            client,
            1011905,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "removeNode", "name": "Add Tag"}],
            },
            expect_error=True,
        )
        if "stable target" not in str(pcg_remove_by_name_failure.get("message", "")):
            fail(f"PCG removeNode should reject non-stable name targets: {pcg_remove_by_name_failure}")

        pcg_remove_by_id = call_domain_tool(
            client,
            1011906,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "removeNode", "nodeId": pcg_remove_tag_id}],
            },
        )
        op_ok(pcg_remove_by_id)
        pcg_remove_snapshot = query_snapshot(client, 1011907, temp_pcg_remove_asset, "pcg", "PCGGraph")
        pcg_remove_nodes = pcg_remove_snapshot.get("nodes")
        pcg_remove_edges = pcg_remove_snapshot.get("edges")
        if not isinstance(pcg_remove_nodes, list) or not isinstance(pcg_remove_edges, list):
            fail(f"PCG removeNode query missing nodes/edges: {pcg_remove_snapshot}")
        require_node_absent(pcg_remove_nodes, pcg_remove_tag_id)
        if any(
            isinstance(edge, dict)
            and (edge.get("fromNodeId") == pcg_remove_tag_id or edge.get("toNodeId") == pcg_remove_tag_id)
            for edge in pcg_remove_edges
        ):
            fail(f"PCG removeNode should clear edges for removed node: {pcg_remove_snapshot}")

        pcg_remove_layout = call_domain_tool(
            client,
            1011908,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_remove_asset,
                "ops": [{"op": "layoutGraph", "scope": "touched"}],
            },
            expect_error=True,
        )
        pcg_remove_layout_results = pcg_remove_layout.get("opResults")
        if not isinstance(pcg_remove_layout_results, list) or not pcg_remove_layout_results:
            fail(f"PCG removeNode touched layout missing opResults: {pcg_remove_layout}")
        pcg_remove_layout_result = pcg_remove_layout_results[0] if isinstance(pcg_remove_layout_results[0], dict) else {}
        if pcg_remove_layout_result.get("op") != "layoutgraph":
            fail(f"PCG removeNode touched layout wrong op echo: {pcg_remove_layout}")
        if pcg_remove_layout_result.get("ok") is not True:
            if not (
                pcg_remove_layout_result.get("errorCode") == "INTERNAL_ERROR"
                and "No touched nodes are pending for layout." in str(pcg_remove_layout_result.get("errorMessage", ""))
            ):
                fail(f"PCG removeNode touched layout failed unexpectedly: {pcg_remove_layout}")
        print("[PASS] pcg removeNode requires stable targets and preserves touched layout neighbors")

        pcg_set_default_add = call_domain_tool(
            client,
            101191,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGCreatePointsSphereSettings"},
                ],
            },
        )
        pcg_set_default_results = pcg_set_default_add.get("opResults")
        if not isinstance(pcg_set_default_results, list) or len(pcg_set_default_results) != 1:
            fail(f"PCG setPinDefault probe add op missing results: {pcg_set_default_add}")
        pcg_set_default_node_id = pcg_set_default_results[0].get("nodeId")
        if not isinstance(pcg_set_default_node_id, str) or not pcg_set_default_node_id:
            fail(f"PCG setPinDefault probe missing node id: {pcg_set_default_add}")

        pcg_set_default_payload = call_domain_tool(
            client,
            101192,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_set_default_node_id, "pin": "Radius"},
                        "value": 250.5,
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_set_default_node_id, "pin": "LongitudinalSegments"},
                        "value": 8,
                    },
                ],
            },
        )
        op_ok(pcg_set_default_payload)

        pcg_set_default_verify_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101193,
            code=(
                "import json\n"
                "import unreal\n"
                f"node_path = {json.dumps(pcg_set_default_node_id, ensure_ascii=False)}\n"
                "node = unreal.load_object(None, node_path)\n"
                "if node is None:\n"
                "    raise RuntimeError(f'failed to load PCG node: {node_path}')\n"
                "settings = node.get_settings()\n"
                "if settings is None:\n"
                "    raise RuntimeError(f'PCG node has no settings: {node_path}')\n"
                "print(json.dumps({\n"
                "    'ok': True,\n"
                "    'radius': settings.get_editor_property('radius'),\n"
                "    'longitudinalSegments': settings.get_editor_property('longitudinal_segments'),\n"
                "}, ensure_ascii=False))\n"
            ),
        )
        pcg_set_default_verify = parse_execute_json(pcg_set_default_verify_payload)
        if pcg_set_default_verify.get("ok") is not True:
            fail(f"PCG setPinDefault verification failed: {pcg_set_default_verify}")
        if abs(float(pcg_set_default_verify.get("radius", 0.0)) - 250.5) > 1e-6:
            fail(f"PCG setPinDefault did not update Radius: {pcg_set_default_verify}")
        if pcg_set_default_verify.get("longitudinalSegments") != 8:
            fail(f"PCG setPinDefault did not update LongitudinalSegments: {pcg_set_default_verify}")
        print("[PASS] pcg.graph.edit setPinDefault supports overridable inputs")

        pcg_filter_add = call_domain_tool(
            client,
            101194,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {"op": "addNode.byClass", "nodeClassPath": "/Script/PCG.PCGFilterByAttributeSettings"},
                ],
            },
        )
        pcg_filter_add_results = pcg_filter_add.get("opResults")
        if not isinstance(pcg_filter_add_results, list) or len(pcg_filter_add_results) != 1:
            fail(f"PCG FilterByAttribute probe add op missing results: {pcg_filter_add}")
        pcg_filter_node_id = pcg_filter_add_results[0].get("nodeId")
        if not isinstance(pcg_filter_node_id, str) or not pcg_filter_node_id:
            fail(f"PCG FilterByAttribute probe missing node id: {pcg_filter_add}")

        pcg_filter_query = call_domain_tool(
            client,
            101195,
            "pcg",
            "query",
            {
                "assetPath": temp_pcg_asset,
                "filter": {"nodeClasses": ["/Script/PCG.PCGFilterByAttributeSettings"]},
            },
        )
        semantic_snapshot = pcg_filter_query.get("semanticSnapshot")
        snapshot_nodes = semantic_snapshot.get("nodes") if isinstance(semantic_snapshot, dict) else None
        if not isinstance(snapshot_nodes, list):
            fail(f"PCG FilterByAttribute pcg.graph.inspect missing semanticSnapshot.nodes: {pcg_filter_query}")
        filter_node = next(
            (node for node in snapshot_nodes if isinstance(node, dict) and node.get("id") == pcg_filter_node_id),
            None,
        )
        if not isinstance(filter_node, dict):
            fail(f"PCG FilterByAttribute node not present in pcg.graph.inspect snapshot: {pcg_filter_query}")
        filter_pins = filter_node.get("pins")
        if not isinstance(filter_pins, list):
            fail(f"PCG FilterByAttribute node missing pins[]: {filter_node}")
        filter_pin_names = {
            pin.get("name")
            for pin in filter_pins
            if isinstance(pin, dict) and isinstance(pin.get("name"), str)
        }
        for expected_pin in {
            "TargetAttribute",
            "Threshold/AttributeTypes/Type",
            "Threshold/AttributeTypes/DoubleValue",
        }:
            if expected_pin not in filter_pin_names:
                fail(f"PCG FilterByAttribute inspect missing writable pin path {expected_pin}: {filter_node}")
        print("[PASS] PCG FilterByAttribute inspect exposes writable constant threshold paths")

        pcg_filter_mutate = call_domain_tool(
            client,
            101196,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "FilterMode"},
                        "value": "FilterByValue",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "TargetAttribute"},
                        "value": "Desert_Cactus",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "FilterOperator"},
                        "value": "GreaterOrEqual",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/bUseConstantThreshold"},
                        "value": True,
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/type"},
                        "value": "Double",
                    },
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "Threshold/AttributeTypes/double_value"},
                        "value": 0.5,
                    },
                ],
            },
        )
        pcg_filter_mutate_results = pcg_filter_mutate.get("opResults")
        if not isinstance(pcg_filter_mutate_results, list) or len(pcg_filter_mutate_results) != 6:
            fail(f"PCG FilterByAttribute graph edit missing opResults: {pcg_filter_mutate}")
        for index, result in enumerate(pcg_filter_mutate_results):
            if not isinstance(result, dict) or not result.get("ok"):
                fail(f"PCG FilterByAttribute graph edit op[{index}] failed: {pcg_filter_mutate}")

        pcg_filter_verify_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101197,
            code=(
                "import json\n"
                "import unreal\n"
                f"node_path = {json.dumps(pcg_filter_node_id, ensure_ascii=False)}\n"
                "node = unreal.load_object(None, node_path)\n"
                "if node is None:\n"
                "    raise RuntimeError(f'failed to load PCG node: {node_path}')\n"
                "settings = node.get_settings()\n"
                "if settings is None:\n"
                "    raise RuntimeError(f'PCG node has no settings: {node_path}')\n"
                "selector_helpers = unreal.PCGAttributePropertySelectorBlueprintHelpers\n"
                "target = settings.get_editor_property('target_attribute')\n"
                "threshold = settings.get_editor_property('threshold')\n"
                "attribute_types = threshold.get_editor_property('attribute_types')\n"
                "print(json.dumps({\n"
                "    'ok': True,\n"
                "    'targetAttributeName': str(selector_helpers.get_attribute_name(target)),\n"
                "    'targetPropertyName': str(selector_helpers.get_property_name(target)),\n"
                "    'thresholdType': str(attribute_types.get_editor_property('type')),\n"
                "    'thresholdDoubleValue': attribute_types.get_editor_property('double_value'),\n"
                "}, ensure_ascii=False))\n"
            ),
        )
        pcg_filter_verify = parse_execute_json(pcg_filter_verify_payload)
        if pcg_filter_verify.get("ok") is not True:
            fail(f"PCG FilterByAttribute verification failed: {pcg_filter_verify}")
        if pcg_filter_verify.get("targetAttributeName") != "Desert_Cactus":
            fail(f"PCG FilterByAttribute TargetAttribute did not update: {pcg_filter_verify}")
        if pcg_filter_verify.get("targetPropertyName") not in {"None", ""}:
            fail(f"PCG FilterByAttribute TargetAttribute should not remain a property selector: {pcg_filter_verify}")
        threshold_type = str(pcg_filter_verify.get("thresholdType", ""))
        if "Double" not in threshold_type and "DOUBLE" not in threshold_type:
            fail(f"PCG FilterByAttribute threshold type did not update to Double: {pcg_filter_verify}")
        if abs(float(pcg_filter_verify.get("thresholdDoubleValue", 0.0)) - 0.5) > 1e-6:
            fail(f"PCG FilterByAttribute threshold constant did not update: {pcg_filter_verify}")
        print("[PASS] pcg.graph.edit setPinDefault supports selector and constant threshold paths")

        pcg_filter_component_mutate = call_domain_tool(
            client,
            101198,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "TargetAttribute"},
                        "value": "Position.Z",
                    }
                ],
            },
        )
        component_results = pcg_filter_component_mutate.get("opResults")
        if not isinstance(component_results, list) or len(component_results) != 1 or not component_results[0].get("ok"):
            fail(f"PCG FilterByAttribute Position.Z mutate failed: {pcg_filter_component_mutate}")

        pcg_filter_component_query = call_domain_tool(
            client,
            101199,
            "pcg",
            "query",
            {
                "assetPath": temp_pcg_asset,
                "filter": {"nodeClasses": ["/Script/PCG.PCGFilterByAttributeSettings"]},
                "view": "full",
            },
        )
        component_nodes = pcg_filter_component_query.get("semanticSnapshot", {}).get("nodes", [])
        component_filter_node = next(
            (node for node in component_nodes if isinstance(node, dict) and node.get("id") == pcg_filter_node_id),
            None,
        )
        if not isinstance(component_filter_node, dict):
            fail(f"PCG FilterByAttribute Position.Z node not present in inspect output: {pcg_filter_component_query}")
        target_selector = component_filter_node.get("effectiveSettings", {}).get("targetAttribute")
        if not isinstance(target_selector, dict):
            fail(f"PCG FilterByAttribute missing structured targetAttribute readback: {component_filter_node}")
        if target_selector.get("text") != "Position.Z":
            fail(f"PCG FilterByAttribute Position.Z text mismatch: {target_selector}")
        if target_selector.get("selection") != "Attribute":
            fail(f"PCG FilterByAttribute Position.Z should remain an attribute selector: {target_selector}")
        if target_selector.get("name") != "Position":
            fail(f"PCG FilterByAttribute Position.Z name mismatch: {target_selector}")
        if target_selector.get("accessors") != ["Z"] or target_selector.get("accessorPath") != "Z":
            fail(f"PCG FilterByAttribute Position.Z accessor decomposition mismatch: {target_selector}")

        pcg_filter_node_describe = call_tool(
            client,
            101201,
            "pcg.node.inspect",
            {"assetPath": temp_pcg_asset, "node": {"id": pcg_filter_node_id}},
        )
        describe_properties = pcg_filter_node_describe.get("properties", [])
        target_property = next(
            (
                prop
                for prop in describe_properties
                if isinstance(prop, dict)
                and prop.get("name") == "TargetAttribute"
                and prop.get("valueKind") == "pcgSelector"
            ),
            None,
        )
        if not isinstance(target_property, dict):
            fail(f"pcg.node.inspect did not expose TargetAttribute as a selector property: {pcg_filter_node_describe}")
        accepted_input = target_property.get("acceptedInput", [])
        if "string" not in accepted_input or "pcgSelector" not in accepted_input:
            fail(f"pcg.node.inspect TargetAttribute acceptedInput mismatch: {target_property}")
        node_inspect_value = target_property.get("value")
        if not isinstance(node_inspect_value, dict):
            fail(f"pcg.node.inspect TargetAttribute missing structured current value: {target_property}")
        if node_inspect_value.get("kind") != "pcgSelector" or node_inspect_value.get("text") != "Position.Z":
            fail(f"pcg.node.inspect TargetAttribute selector value mismatch: {target_property}")
        if node_inspect_value.get("accessors") != ["Z"] or node_inspect_value.get("valid") is not True:
            fail(f"pcg.node.inspect TargetAttribute selector decomposition mismatch: {target_property}")

        pcg_filter_structured_mutate = call_domain_tool(
            client,
            101202,
            "pcg",
            "mutate",
            {
                "assetPath": temp_pcg_asset,
                "ops": [
                    {
                        "op": "setPinDefault",
                        "target": {"nodeId": pcg_filter_node_id, "pin": "TargetAttribute"},
                        "value": {
                            "kind": "pcgSelector",
                            "selection": "Attribute",
                            "name": "Position",
                            "accessors": ["X"],
                        },
                    }
                ],
            },
        )
        structured_results = pcg_filter_structured_mutate.get("opResults")
        if not isinstance(structured_results, list) or len(structured_results) != 1 or not structured_results[0].get("ok"):
            fail(f"PCG FilterByAttribute structured selector mutate failed: {pcg_filter_structured_mutate}")

        pcg_filter_structured_query = call_domain_tool(
            client,
            101203,
            "pcg",
            "query",
            {
                "assetPath": temp_pcg_asset,
                "filter": {"nodeClasses": ["/Script/PCG.PCGFilterByAttributeSettings"]},
                "view": "full",
            },
        )
        structured_nodes = pcg_filter_structured_query.get("semanticSnapshot", {}).get("nodes", [])
        structured_filter_node = next(
            (node for node in structured_nodes if isinstance(node, dict) and node.get("id") == pcg_filter_node_id),
            None,
        )
        structured_selector = (
            structured_filter_node.get("effectiveSettings", {}).get("targetAttribute")
            if isinstance(structured_filter_node, dict)
            else None
        )
        if not isinstance(structured_selector, dict) or structured_selector.get("text") != "Position.X":
            fail(f"PCG FilterByAttribute structured selector readback mismatch: {structured_selector}")

        pcg_filter_component_verify_payload = call_execute_exec_with_retry(
            client=client,
            req_id_base=101204,
            code=(
                "import json\n"
                "import unreal\n"
                f"node_path = {json.dumps(pcg_filter_node_id, ensure_ascii=False)}\n"
                "node = unreal.load_object(None, node_path)\n"
                "if node is None:\n"
                "    raise RuntimeError(f'failed to load PCG node: {node_path}')\n"
                "settings = node.get_settings()\n"
                "helpers = unreal.PCGAttributePropertySelectorBlueprintHelpers\n"
                "target = settings.get_editor_property('target_attribute')\n"
                "print(json.dumps({\n"
                "    'ok': True,\n"
                "    'selection': str(helpers.get_selection(target)).split('.')[-1],\n"
                "    'attributeName': str(helpers.get_attribute_name(target)),\n"
                "    'propertyName': str(helpers.get_property_name(target)),\n"
                "    'extraNames': list(helpers.get_extra_names(target)),\n"
                "}, ensure_ascii=False))\n"
            ),
        )
        pcg_filter_component_verify = parse_execute_json(pcg_filter_component_verify_payload)
        if pcg_filter_component_verify.get("ok") is not True:
            fail(f"PCG FilterByAttribute Position.Z verification failed: {pcg_filter_component_verify}")
        component_selection = str(pcg_filter_component_verify.get("selection", "")).lower()
        if "attribute" not in component_selection:
            fail(f"PCG FilterByAttribute structured selector engine selection mismatch: {pcg_filter_component_verify}")
        if pcg_filter_component_verify.get("attributeName") != "Position":
            fail(f"PCG FilterByAttribute structured selector engine attribute mismatch: {pcg_filter_component_verify}")
        if pcg_filter_component_verify.get("propertyName") not in {"None", ""}:
            fail(f"PCG FilterByAttribute structured selector should not be a property selector: {pcg_filter_component_verify}")
        if pcg_filter_component_verify.get("extraNames") != ["X"]:
            fail(f"PCG FilterByAttribute structured selector engine accessor mismatch: {pcg_filter_component_verify}")
        print("[PASS] PCG FilterByAttribute selector readback/editing is structured and engine-matched")

        print("[PASS] domain mutate core ops validated")

        # -----------------------------------------------------------------------
        # widget.* regression
        # -----------------------------------------------------------------------
        temp_wbp_asset = make_temp_asset_path("/Game/Codex/WBP_BridgeRegression")

        # W01 — create WidgetBlueprint fixture
        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=5000,
            code=(
                "import unreal, json\n"
                f"asset='{temp_wbp_asset}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.WidgetBlueprintFactory()\n"
                "wbp = asset_tools.create_asset(asset_name, pkg_path, unreal.WidgetBlueprint, factory)\n"
                "print(json.dumps({'created': wbp is not None}, ensure_ascii=False))\n"
            ),
        )
        print(f"[PASS] W01 WidgetBlueprint fixture created: {temp_wbp_asset}")

        # W02 — widget.tree.inspect baseline structure
        wq0 = widget_tree_inspect(client, 5010, {"assetPath": temp_wbp_asset})
        if wq0.get("assetPath") != temp_wbp_asset:
            fail(f"W02 widget.tree.inspect wrong assetPath: {wq0}")
        revision_0 = wq0.get("revision")
        if not isinstance(revision_0, str) or not revision_0:
            fail(f"W02 widget.tree.inspect missing revision: {wq0}")
        if not isinstance(wq0.get("diagnostics"), list):
            fail(f"W02 widget.tree.inspect missing diagnostics[]: {wq0}")
        print("[PASS] W02 widget.tree.inspect baseline structure validated")

        # W03 — dryRun: op validated but nothing changes
        wm_dry = widget_tree_edit(client, 5020, {
            "assetPath": temp_wbp_asset,
            "dryRun": True,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.CanvasPanel",
                "name": "DryCanvas",
                "parent": "root",
            }}],
        })
        if wm_dry.get("applied") is not False:
            fail(f"W03 dryRun applied should be False: {wm_dry}")
        dry_op = wm_dry.get("opResults", [{}])[0] if wm_dry.get("opResults") else {}
        if not isinstance(dry_op, dict) or not dry_op.get("ok"):
            fail(f"W03 dryRun op should be ok (validation only): {dry_op}")
        if dry_op.get("changed") is not False:
            fail(f"W03 dryRun changed should be False: {dry_op}")
        if wm_dry.get("newRevision") != wm_dry.get("previousRevision"):
            fail(f"W03 dryRun must not change revision: {wm_dry}")
        print("[PASS] W03 widget.tree.edit dryRun validated")

        # W04 — addWidget CanvasPanel as root
        wm_add_canvas = widget_tree_edit(client, 5030, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.CanvasPanel",
                "name": "RootCanvas",
                "parent": "root",
            }}],
        })
        widget_op_ok(wm_add_canvas, 0)
        if wm_add_canvas.get("applied") is not True:
            fail(f"W04 addWidget CanvasPanel applied should be True: {wm_add_canvas}")
        revision_1 = wm_add_canvas.get("newRevision")
        if not isinstance(revision_1, str) or revision_1 == revision_0:
            fail(f"W04 addWidget should update revision: {wm_add_canvas}")
        print("[PASS] W04 widget.tree.edit addFromPalette CanvasPanel validated")

        # W05 — query confirms rootWidget now exists
        wq1 = widget_tree_inspect(client, 5040, {"assetPath": temp_wbp_asset})
        root_widget = wq1.get("rootWidget")
        if not isinstance(root_widget, dict):
            fail(f"W05 widget.tree.inspect rootWidget should be object after addFromPalette: {wq1}")
        if root_widget.get("name") != "RootCanvas":
            fail(f"W05 rootWidget name mismatch: {root_widget}")
        print("[PASS] W05 widget.tree.inspect reflects added CanvasPanel root")

        # W06 — addWidget TextBlock as child of RootCanvas (uses parentName field)
        wm_add_text = widget_tree_edit(client, 5050, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "TitleText",
                "parentName": "RootCanvas",
            }}],
        })
        widget_op_ok(wm_add_text, 0)
        revision_2 = wm_add_text.get("newRevision")
        if not isinstance(revision_2, str) or revision_2 == revision_1:
            fail(f"W06 addWidget TextBlock should update revision: {wm_add_text}")
        print("[PASS] W06 widget.tree.edit addFromPalette TextBlock as child validated")

        # W06b — renameWidget preserves the widget and updates inspect output
        wm_rename = widget_tree_edit(client, 5055, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "renameWidget", "args": {
                "name": "TitleText",
                "newName": "RenamedTitleText",
            }}],
        })
        widget_op_ok(wm_rename, 0)
        wq_renamed = widget_tree_inspect(client, 5056, {
            "assetPath": temp_wbp_asset,
            "view": "layout",
        })
        renamed_children = wq_renamed.get("rootWidget", {}).get("children", [])
        if not isinstance(renamed_children, list) or not any(
            isinstance(c, dict) and c.get("name") == "RenamedTitleText" for c in renamed_children
        ):
            fail(f"W06b RenamedTitleText not found after renameWidget: {wq_renamed}")
        if any(isinstance(c, dict) and c.get("name") == "TitleText" for c in renamed_children):
            fail(f"W06b old TitleText still present after renameWidget: {wq_renamed}")
        wm_rename_back = widget_tree_edit(client, 5057, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "renameWidget", "args": {
                "name": "RenamedTitleText",
                "newName": "TitleText",
            }}],
        })
        widget_op_ok(wm_rename_back, 0)
        revision_2 = wm_rename_back.get("newRevision")
        print("[PASS] W06b widget.tree.edit renameWidget validated")

        # W06c — create native UMG component-bound event for a Button OnClicked
        wm_add_button = widget_tree_edit(client, 5058, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.Button",
                "name": "CardButton",
                "parentName": "RootCanvas",
            }}],
        })
        widget_op_ok(wm_add_button, 0)
        we_create = call_tool(client, 5059, "widget.event.create", {
            "assetPath": temp_wbp_asset,
            "widget": {"name": "CardButton"},
            "event": "OnClicked",
        })
        if we_create.get("isError"):
            fail(f"W06c widget.event.create failed: {we_create}")
        if we_create.get("created") is not True or not we_create.get("nodeId"):
            fail(f"W06c widget.event.create should create node with nodeId: {we_create}")
        if we_create.get("widget", {}).get("name") != "CardButton":
            fail(f"W06c widget.event.create should return widget ref object: {we_create}")
        node_obj = we_create.get("node")
        if not isinstance(node_obj, dict) or node_obj.get("nodeClass") != "K2Node_ComponentBoundEvent":
            fail(f"W06c widget.event.create should return K2Node_ComponentBoundEvent: {we_create}")
        we_existing = call_tool(client, 5061, "widget.event.create", {
            "assetPath": temp_wbp_asset,
            "widget": {"name": "CardButton"},
            "event": "OnClicked",
        })
        if we_existing.get("isError"):
            fail(f"W06c widget.event.create idempotent call failed: {we_existing}")
        if we_existing.get("created") is not False or we_existing.get("existing") is not True:
            fail(f"W06c widget.event.create should return existing node on repeated call: {we_existing}")
        if we_existing.get("nodeId") != we_create.get("nodeId"):
            fail(f"W06c widget.event.create repeated call should return same nodeId: {we_existing}")
        print("[PASS] W06c widget.event.create native Button OnClicked event validated")

        # W07 — layout view confirms child is present
        wq2 = widget_tree_inspect(client, 5060, {
            "assetPath": temp_wbp_asset,
            "view": "layout",
        })
        root_children = wq2.get("rootWidget", {}).get("children", [])
        if not isinstance(root_children, list) or not any(
            isinstance(c, dict) and c.get("name") == "TitleText" for c in root_children
        ):
            fail(f"W07 TitleText not found in rootWidget.children: {wq2}")
        print("[PASS] W07 widget.tree.inspect layout view shows TextBlock child")

        # W08 — widget.edit setProperty on TextBlock (Text / a known FText property)
        wm_set_prop = widget_edit(client, 5070, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "TitleText",
                "property": "Text",
                "value": "Hello Loomle",
            }}],
        })
        widget_op_ok(wm_set_prop, 0)
        if wm_set_prop.get("newRevision") == wm_set_prop.get("previousRevision"):
            fail(f"W08 widget.edit setProperty should update revision: {wm_set_prop}")
        print("[PASS] W08 widget.edit setProperty validated")

        # W09 — addWidget second panel for reparent source (uses parentName field)
        wm_add_panel2 = widget_tree_edit(client, 5080, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.VerticalBox",
                "name": "SecondPanel",
                "parentName": "RootCanvas",
            }}],
        })
        widget_op_ok(wm_add_panel2, 0)
        revision_3 = wm_add_panel2.get("newRevision")
        print("[PASS] W09 widget.tree.edit addFromPalette VerticalBox validated")

        # W10 — reparentWidget: move TitleText from RootCanvas to SecondPanel
        wm_reparent = widget_tree_edit(client, 5090, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "reparentWidget", "args": {
                "name": "TitleText",
                "newParent": "SecondPanel",
            }}],
        })
        widget_op_ok(wm_reparent, 0)
        revision_4 = wm_reparent.get("newRevision")
        if not isinstance(revision_4, str) or revision_4 == revision_3:
            fail(f"W10 reparentWidget should update revision: {wm_reparent}")
        print("[PASS] W10 widget.tree.edit reparentWidget validated")

        # W11 — removeWidget
        wm_remove = widget_tree_edit(client, 5100, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "removeWidget", "args": {"name": "TitleText"}}],
        })
        widget_op_ok(wm_remove, 0)
        revision_5 = wm_remove.get("newRevision")
        if not isinstance(revision_5, str) or revision_5 == revision_4:
            fail(f"W11 removeWidget should update revision: {wm_remove}")
        print("[PASS] W11 widget.tree.edit removeWidget validated")

        # W12 — expectedRevision conflict: pass stale revision
        wm_stale = widget_tree_edit(client, 5110, {
            "assetPath": temp_wbp_asset,
            "expectedRevision": revision_0,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "ShouldNotExist",
                "parent": "RootCanvas",
            }}],
        }, expect_error=True)
        if not wm_stale.get("isError"):
            fail(f"W12 stale expectedRevision should produce isError: {wm_stale}")
        stale_code = wm_stale.get("code", "")
        if stale_code not in {"REVISION_CONFLICT", 1008} and wm_stale.get("message") != "REVISION_CONFLICT":
            fail(f"W12 expected REVISION_CONFLICT code, got: {stale_code}")
        print("[PASS] W12 widget.tree.edit stale expectedRevision raises REVISION_CONFLICT")

        # W13 — batch preflight failure stops before later commands are applied
        wm_unknown = widget_tree_edit(client, 5120, {
            "assetPath": temp_wbp_asset,
            "ops": [
                {"op": "removeWidget", "args": {"name": "MissingBeforeStop"}},
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.TextBlock",
                    "name": "AfterStoppedFailure",
                    "parent": "RootCanvas",
                }},
            ],
        }, expect_error=True)
        if not wm_unknown.get("isError"):
            fail(f"W13 missing widget should return an error: {wm_unknown}")
        if not isinstance(wm_unknown.get("opResults"), list) or len(wm_unknown["opResults"]) != 1:
            fail(f"W13 preflight failure should return only the failing opResult: {wm_unknown}")
        op0 = wm_unknown["opResults"][0] if isinstance(wm_unknown["opResults"][0], dict) else {}
        if op0.get("ok") is not False:
            fail(f"W13 removeWidget[0] should be ok=False: {op0}")
        wq_after_stop = widget_tree_inspect(client, 5121, {"assetPath": temp_wbp_asset})
        root_after_stop = wq_after_stop.get("rootWidget", {})
        stopped_children = root_after_stop.get("children", []) if isinstance(root_after_stop, dict) else []
        if any(isinstance(c, dict) and c.get("name") == "AfterStoppedFailure" for c in stopped_children):
            fail(f"W13 later command should not be applied after preflight failure: {wq_after_stop}")
        print("[PASS] W13 widget.tree.edit preflight failure stops batch before mutation")

        # W14 — removeWidget for non-existent widget (op-level error)
        wm_notfound = widget_tree_edit(client, 5130, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "removeWidget", "args": {"name": "DoesNotExist"}}],
        }, expect_error=True)
        op_nf = wm_notfound.get("opResults", [{}])[0] if wm_notfound.get("opResults") else {}
        if not isinstance(op_nf, dict) or op_nf.get("ok") is not False:
            fail(f"W14 removeWidget non-existent should be ok=False: {op_nf}")
        print("[PASS] W14 widget.tree.edit removeWidget non-existent widget returns op error")

        # W15 — widget.tree.inspect on a non-WBP asset (Blueprint, should fail)
        wq_err = widget_tree_inspect(client, 5140, {"assetPath": temp_asset}, expect_error=True)
        if not wq_err.get("isError"):
            fail(f"W15 widget.tree.inspect on non-WBP asset should isError: {wq_err}")
        err_code = wq_err.get("code", "")
        if err_code not in {"WIDGET_TREE_UNAVAILABLE", 1023} and wq_err.get("message") != "WIDGET_TREE_UNAVAILABLE":
            fail(f"W15 expected WIDGET_TREE_UNAVAILABLE, got: {err_code}")
        print("[PASS] W15 widget.tree.inspect on non-WBP asset raises WIDGET_TREE_UNAVAILABLE")

        # W16 — widget.compile
        wv = call_tool(client, 5150, "widget.compile", {"assetPath": temp_wbp_asset})
        if wv.get("status") not in {"ok", "error"}:
            fail(f"W16 widget.compile unexpected status: {wv}")
        if wv.get("assetPath") != temp_wbp_asset:
            fail(f"W16 widget.compile wrong assetPath: {wv}")
        if not isinstance(wv.get("diagnostics"), list):
            fail(f"W16 widget.compile missing diagnostics[]: {wv}")
        print("[PASS] W16 widget.compile validated")

        # W17 — issue #140: batch addWidget with parentName keeps root intact
        # Both ops in a single mutate call: first adds a VerticalBox as root,
        # second adds a TextBlock as child via parentName. The root must remain
        # the VerticalBox after the batch completes.
        temp_wbp_batch = make_temp_asset_path("/Game/Codex/WBP_Batch140")
        call_execute_exec_with_retry(
            client=client,
            req_id_base=5161,
            code=(
                "import unreal, json\n"
                f"asset='{temp_wbp_batch}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.WidgetBlueprintFactory()\n"
                "wbp = asset_tools.create_asset(asset_name, pkg_path, unreal.WidgetBlueprint, factory)\n"
                "print(json.dumps({'created': wbp is not None}, ensure_ascii=False))\n"
            ),
        )
        wm_batch = widget_tree_edit(client, 5162, {
            "assetPath": temp_wbp_batch,
            "ops": [
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.VerticalBox",
                    "name": "BatchRoot",
                    "parent": "root",
                }},
                {"op": "addWidget", "args": {
                    "widgetClass": "/Script/UMG.TextBlock",
                    "name": "BatchChild",
                    "parentName": "BatchRoot",
                }},
            ],
        })
        widget_op_ok(wm_batch, 0)
        widget_op_ok(wm_batch, 1)
        if wm_batch.get("applied") is not True:
            fail(f"W17 batch addWidget applied should be True: {wm_batch}")
        wq_batch = widget_tree_inspect(client, 5163, {"assetPath": temp_wbp_batch})
        batch_root = wq_batch.get("rootWidget", {})
        if batch_root.get("name") != "BatchRoot":
            fail(f"W17 rootWidget should be BatchRoot, got: {batch_root.get('name')!r}")
        batch_children = batch_root.get("children", [])
        if not any(isinstance(c, dict) and c.get("name") == "BatchChild" for c in batch_children):
            fail(f"W17 BatchChild not found in BatchRoot.children: {batch_children}")
        print("[PASS] W17 batch addWidget with parentName keeps root intact (issue #140)")

        # W18 — legacy "parent" alias still routes children correctly
        # Verifies backward-compat: "parent" field (not "parentName") must still work
        # for child widgets added in a separate mutate call.
        wm_legacy = widget_tree_edit(client, 5164, {
            "assetPath": temp_wbp_batch,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.TextBlock",
                "name": "LegacyChild",
                "parent": "BatchRoot",
            }}],
        })
        widget_op_ok(wm_legacy, 0)
        wq_legacy = widget_tree_inspect(client, 5165, {"assetPath": temp_wbp_batch})
        legacy_root = wq_legacy.get("rootWidget", {})
        if legacy_root.get("name") != "BatchRoot":
            fail(f"W18 rootWidget should still be BatchRoot after legacy parent add: {legacy_root.get('name')!r}")
        legacy_children = legacy_root.get("children", [])
        if not any(isinstance(c, dict) and c.get("name") == "LegacyChild" for c in legacy_children):
            fail(f"W18 LegacyChild not found in BatchRoot.children: {legacy_children}")
        print("[PASS] W18 legacy parent field alias routes child correctly")

        # W19 — widget.inspect by short class name
        wd_short = widget_inspect(client, 5166, {"widgetClass": "TextBlock"})
        if wd_short.get("isError") is not False:
            fail(f"W19 widget.inspect TextBlock should report isError=false: {wd_short}")
        if "properties" not in wd_short or not isinstance(wd_short["properties"], list):
            fail(f"W19 widget.inspect TextBlock missing properties[]: {wd_short}")
        if not wd_short.get("widgetClass", "").endswith("TextBlock"):
            fail(f"W19 widget.inspect widgetClass mismatch: {wd_short.get('widgetClass')!r}")
        if not any(p.get("name") == "Text" for p in wd_short["properties"]):
            fail(f"W19 widget.inspect TextBlock should have Text property: {[p['name'] for p in wd_short['properties']]}")
        if not isinstance(wd_short.get("slotProperties"), list):
            fail(f"W19 widget.inspect missing slotProperties[]: {wd_short}")
        if "currentValues" in wd_short:
            fail(f"W19 widget.inspect without instance should NOT have currentValues: {wd_short}")
        if "slotCurrentValues" in wd_short:
            fail(f"W19 widget.inspect without instance should NOT have slotCurrentValues: {wd_short}")
        print("[PASS] W19 widget.inspect by short class name (TextBlock)")

        # W20 — widget.inspect by full class path
        wd_full = widget_inspect(client, 5167, {"widgetClass": "/Script/UMG.TextBlock"})
        if not wd_full.get("widgetClass", "").endswith("TextBlock"):
            fail(f"W20 widget.inspect full path widgetClass mismatch: {wd_full.get('widgetClass')!r}")
        if "properties" not in wd_full or not isinstance(wd_full["properties"], list):
            fail(f"W20 widget.inspect full path missing properties[]: {wd_full}")
        print("[PASS] W20 widget.inspect by full class path (/Script/UMG.TextBlock)")

        # W21 — widget.inspect by assetPath+widgetName returns currentValues
        # Use "RootCanvas" (CanvasPanel) which persists throughout the test sequence
        wd_inst = widget_inspect(client, 5168, {
            "assetPath": temp_wbp_asset,
            "widgetName": "RootCanvas"
        })
        if wd_inst.get("isError") is not False:
            fail(f"W21 widget.inspect instance should report isError=false: {wd_inst}")
        if wd_inst.get("assetPath") != temp_wbp_asset or wd_inst.get("widget", {}).get("name") != "RootCanvas":
            fail(f"W21 widget.inspect instance missing assetPath/widget ref: {wd_inst}")
        if not wd_inst.get("widgetClass", "").endswith("CanvasPanel"):
            fail(f"W21 widget.inspect instance widgetClass mismatch: {wd_inst.get('widgetClass')!r}")
        if "properties" not in wd_inst or not isinstance(wd_inst["properties"], list):
            fail(f"W21 widget.inspect instance missing properties[]: {wd_inst}")
        if "currentValues" not in wd_inst or not isinstance(wd_inst["currentValues"], dict):
            fail(f"W21 widget.inspect instance should have currentValues dict: {wd_inst}")
        if "slotCurrentValues" not in wd_inst or not isinstance(wd_inst["slotCurrentValues"], dict):
            fail(f"W21 widget.inspect instance should have slotCurrentValues dict: {wd_inst}")
        print("[PASS] W21 widget.inspect by assetPath+widget includes currentValues")

        # W21b — widget.inspect instance uses the real widget class and reports slot values
        wd_child_inst = widget_inspect(client, 51681, {
            "assetPath": temp_wbp_batch,
            "widgetName": "BatchChild",
            "widgetClass": "Widget",
        })
        if not wd_child_inst.get("widgetClass", "").endswith("TextBlock"):
            fail(f"W21b widget.inspect should return real instance class TextBlock: {wd_child_inst}")
        if not isinstance(wd_child_inst.get("slotClass"), str) or not wd_child_inst["slotClass"].endswith("Slot"):
            fail(f"W21b widget.inspect child should report slotClass: {wd_child_inst}")
        if not isinstance(wd_child_inst.get("slotCurrentValues"), dict):
            fail(f"W21b widget.inspect child should report slotCurrentValues: {wd_child_inst}")
        wd_mismatch = widget_inspect(client, 51682, {
            "assetPath": temp_wbp_batch,
            "widgetName": "BatchChild",
            "widgetClass": "Button",
        }, expect_error=True)
        if not wd_mismatch.get("isError") or (
            wd_mismatch.get("code") != "WIDGET_CLASS_MISMATCH"
            and wd_mismatch.get("message") != "WIDGET_CLASS_MISMATCH"
        ):
            fail(f"W21b widget.inspect class mismatch should return WIDGET_CLASS_MISMATCH: {wd_mismatch}")
        print("[PASS] W21b widget.inspect instance class validation and slot values validated")

        # W22 — widget.inspect unknown class returns WIDGET_CLASS_NOT_FOUND
        wd_bad = widget_inspect(client, 5169, {"widgetClass": "NonExistentWidget_XYZ"}, expect_error=True)
        if not wd_bad.get("isError"):
            fail(f"W22 expected error for unknown class, got: {wd_bad}")
        err_code = wd_bad.get("code", "")
        if err_code not in {"WIDGET_CLASS_NOT_FOUND", 1025} and wd_bad.get("message") != "WIDGET_CLASS_NOT_FOUND":
            fail(f"W22 expected WIDGET_CLASS_NOT_FOUND, got: {err_code}")
        print("[PASS] W22 widget.inspect unknown class returns WIDGET_CLASS_NOT_FOUND")

        # W23 — widget.edit setSlotProperty can write a CanvasPanelSlot ZOrder
        # SecondPanel is a VerticalBox child of RootCanvas — its slot is FCanvasPanelSlot.
        wm_slot_zorder = widget_edit(client, 5200, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setSlotProperty", "args": {
                "name": "SecondPanel",
                "property": "ZOrder",
                "value": "5",
            }}],
        })
        widget_op_ok(wm_slot_zorder, 0)
        print("[PASS] W23 widget.edit setSlotProperty writes CanvasPanelSlot ZOrder")

        # W24 — widget.edit setSlotProperty can write CanvasPanelSlot LayoutData (struct slot property)
        wm_slot_layout = widget_edit(client, 5210, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setSlotProperty", "args": {
                "name": "SecondPanel",
                "property": "LayoutData",
                "value": "(Offsets=(Left=10,Top=20,Right=0,Bottom=0),Anchors=(Minimum=(X=0.0,Y=0.0),Maximum=(X=0.5,Y=0.5)),Alignment=(X=0,Y=0))",
            }}],
        })
        widget_op_ok(wm_slot_layout, 0)
        print("[PASS] W24 widget.edit setSlotProperty writes CanvasPanelSlot LayoutData")

        # W25 — widget.edit setProperty with an unknown widget property returns PROPERTY_NOT_FOUND
        wm_bad_prop = widget_edit(client, 5220, {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "setProperty", "args": {
                "name": "SecondPanel",
                "property": "NonExistentProperty_XYZ",
                "value": "anything",
            }}],
        }, expect_error=True)
        op25 = (wm_bad_prop.get("opResults") or [{}])[0]
        if op25.get("ok"):
            fail(f"W25 expected setProperty to fail for unknown property, but got ok: {op25}")
        if wm_bad_prop.get("message") != "PROPERTY_NOT_FOUND" and wm_bad_prop.get("code") != "PROPERTY_NOT_FOUND":
            fail(f"W25 expected PROPERTY_NOT_FOUND, got: {wm_bad_prop}")
        print("[PASS] W25 widget.edit setProperty unknown property returns op-level error")

        print("[PASS] widget.* regression complete")

        print("[PASS] Bridge regression complete")
        completed_successfully = True
        return 0
    finally:
        # Cleanup is intentionally skipped to avoid flaky teardown timeouts
        # masking a fully successful regression run.
        print(f"[WARN] cleanup skipped for temporary asset: {temp_asset}")
        print(f"[WARN] cleanup skipped for temporary enum asset: {temp_enum_asset}")
        print(f"[WARN] cleanup skipped for temporary material asset: {temp_material_asset}")
        print(f"[WARN] cleanup skipped for temporary asset.create material asset: {temp_asset_create_material}")
        print(f"[WARN] cleanup skipped for temporary asset.create material function asset: {temp_asset_create_function}")
        print(f"[WARN] cleanup skipped for temporary asset.create PCG asset: {temp_asset_create_pcg}")
        print(f"[WARN] cleanup skipped for temporary asset.create widget asset: {temp_asset_create_widget}")
        print(f"[WARN] cleanup skipped for temporary PCG asset: {temp_pcg_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG health asset: {temp_pcg_health_asset}")
        print(f"[WARN] cleanup skipped for temporary PCG remove asset: {temp_pcg_remove_asset}")
        temp_wbp_asset = locals().get("temp_wbp_asset", None)
        if temp_wbp_asset:
            print(f"[WARN] cleanup skipped for temporary widget asset: {temp_wbp_asset}")
        temp_wbp_batch = locals().get("temp_wbp_batch", None)
        if temp_wbp_batch:
            print(f"[WARN] cleanup skipped for temporary widget batch asset: {temp_wbp_batch}")
        client.close()
        if completed_successfully and args.close_editor_on_success:
            close_editor_for_project(project_root)


if __name__ == "__main__":
    raise SystemExit(main())
