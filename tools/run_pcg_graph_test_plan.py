#!/usr/bin/env python3
import argparse
import contextlib
import io
import json
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_execute_exec_with_retry,
    call_tool,
    fail,
    parse_execute_json,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tools"))
from generate_graph_test_plan import build_plan  # noqa: E402

GRAPH_NAME = "PCGGraph"
SUPPORTED_ROUNDTRIP_TYPES = {"bool", "int32", "int", "float", "double", "FString", "FName", "enum"}
SUPPORTED_DYNAMIC_TRIGGER_TYPES = {"bool", "int32", "int", "float", "double", "FString", "FName"}


def wait_for_bridge_ready(client: McpStdioClient, timeout_s: float = 120.0, interval_s: float = 2.0) -> None:
    deadline = time.time() + timeout_s
    attempt = 0
    while time.time() < deadline:
        attempt += 1
        try:
            loomle = call_tool(client, 9000 + attempt, "loomle", {})
            status = loomle.get("status")
            rpc_health = loomle.get("runtime", {}).get("rpcHealth", {})
            if status not in {"ok", "degraded"} or rpc_health.get("status") not in {"ok", "degraded"}:
                print(f"[WARN] bridge not ready yet (attempt {attempt}): status={status}, rpc={rpc_health}")
                time.sleep(interval_s)
                continue

            _ = call_execute_exec_with_retry(
                client=client,
                req_id_base=9500 + (attempt * 10),
                code="import unreal\nunreal.log('loomle PCG plan warmup')",
                max_attempts=10,
                retry_delay_s=1.0,
            )
            print(f"[PASS] bridge ready after {attempt} attempt(s)")
            return
        except BaseException as exc:
            print(f"[WARN] bridge readiness probe failed (attempt {attempt}): {exc}")
            time.sleep(interval_s)

    raise RuntimeError(f"bridge did not become ready within {timeout_s:.0f}s")


def compact_json(value: Any, limit: int = 1200) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def camel_to_snake(name: str) -> str:
    chars: list[str] = []
    for index, char in enumerate(name):
        if char.isupper() and index > 0 and (not name[index - 1].isupper()):
            chars.append("_")
        chars.append(char.lower())
    return "".join(chars)


def property_candidates(field_name: str) -> list[str]:
    candidates: list[str] = []
    snake = camel_to_snake(field_name)
    for value in (field_name, snake, field_name.lower()):
        if value and value not in candidates:
            candidates.append(value)
    if field_name.startswith("b") and len(field_name) > 1 and field_name[1].isupper():
        stripped = field_name[1:]
        for value in (stripped, camel_to_snake(stripped), stripped.lower()):
            if value and value not in candidates:
                candidates.append(value)
    return candidates


def normalized_cpp_type_for_field(field_name: str, properties_by_name: dict[str, dict[str, Any]]) -> str:
    prop = properties_by_name.get(field_name)
    cpp_type = prop.get("cppType") if isinstance(prop, dict) else ""
    if isinstance(cpp_type, str) and cpp_type:
        return cpp_type

    if "/" in field_name:
        leaf = field_name.rsplit("/", 1)[-1]
        leaf_prop = properties_by_name.get(leaf)
        leaf_cpp_type = leaf_prop.get("cppType") if isinstance(leaf_prop, dict) else ""
        if isinstance(leaf_cpp_type, str) and leaf_cpp_type:
            return leaf_cpp_type
        if leaf.startswith("b"):
            return "bool"

    return ""


def sample_value_for_type(cpp_type: str, field_name: str) -> Any | None:
    if cpp_type.startswith("E"):
        return 1
    if cpp_type == "bool":
        return True
    if cpp_type in {"int32", "int"}:
        return 7
    if cpp_type in {"float", "double"}:
        return 1.75
    if cpp_type == "FString":
        return f"Loomle.{field_name}.Sample"
    if cpp_type == "FName":
        return f"Loomle{field_name}"
    return None


def create_pcg_fixture(client: McpStdioClient, request_id: int, *, asset_path: str, fixture_id: str, actor_offset: float) -> dict[str, Any]:
    if fixture_id not in {"pcg_graph", "pcg_graph_with_world_actor"}:
        raise RuntimeError(f"unsupported fixture {fixture_id}")

    create_actor = fixture_id == "pcg_graph_with_world_actor"
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
            f"create_actor={'True' if create_actor else 'False'}\n"
            f"actor_offset={float(actor_offset)}\n"
            "pkg_path, asset_name = asset.rsplit('/', 1)\n"
            "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
            "graph = unreal.EditorAssetLibrary.load_asset(asset)\n"
            "if graph is None:\n"
            "    factory = unreal.PCGGraphFactory()\n"
            "    graph = asset_tools.create_asset(asset_name, pkg_path, unreal.PCGGraph, factory)\n"
            "if graph is None:\n"
            "    raise RuntimeError(f'failed to create PCG graph asset: {asset}')\n"
            "result = {'assetPath': asset}\n"
            "if create_actor:\n"
            "    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.Actor, unreal.Vector(actor_offset, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))\n"
            "    if actor is None:\n"
            "        raise RuntimeError('failed to spawn Actor for PCG fixture')\n"
            "    result['actorPath'] = actor.get_path_name()\n"
            "print(json.dumps(result, ensure_ascii=False))\n"
        ),
    )
    return parse_execute_json(payload)


def cleanup_pcg_fixture(client: McpStdioClient, request_id: int, *, asset_path: str, actor_path: str | None) -> None:
    _ = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"asset={json.dumps(asset_path, ensure_ascii=False)}\n"
            f"actor_path={json.dumps(actor_path or '', ensure_ascii=False)}\n"
            "if actor_path:\n"
            "    actor = unreal.load_object(None, actor_path)\n"
            "    if actor is not None:\n"
            "        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
            "        if subsystem is not None:\n"
            "            subsystem.destroy_actor(actor)\n"
            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
            "    unreal.EditorAssetLibrary.delete_asset(asset)\n"
            "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
        ),
    )


def query_pcg_snapshot(client: McpStdioClient, request_id: int, asset_path: str) -> dict[str, Any]:
    payload = call_tool(
        client,
        request_id,
        "graph.query",
        {"assetPath": asset_path, "graphType": "pcg", "graphName": GRAPH_NAME, "limit": 400},
    )
    snapshot = payload.get("semanticSnapshot")
    if not isinstance(snapshot, dict):
        raise RuntimeError(f"graph.query missing semanticSnapshot: {compact_json(payload)}")
    nodes = snapshot.get("nodes")
    if not isinstance(nodes, list):
        raise RuntimeError(f"graph.query missing nodes[]: {compact_json(payload)}")
    return snapshot


def find_node(snapshot: dict[str, Any], node_id: str) -> dict[str, Any] | None:
    for node in snapshot.get("nodes", []):
        if isinstance(node, dict) and node.get("id") == node_id:
            return node
    return None


def add_node(client: McpStdioClient, request_id: int, *, asset_path: str, node_class_path: str) -> str:
    payload = call_tool(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphType": "pcg",
            "graphName": GRAPH_NAME,
            "ops": [{"op": "addNode.byClass", "args": {"nodeClassPath": node_class_path}}],
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        raise RuntimeError(f"addNode.byClass missing opResults: {compact_json(payload)}")
    first = op_results[0]
    if not isinstance(first, dict) or not first.get("ok"):
        raise RuntimeError(f"addNode.byClass failed: {compact_json(payload)}")
    node_id = first.get("nodeId")
    if not isinstance(node_id, str) or not node_id:
        raise RuntimeError(f"addNode.byClass missing nodeId: {compact_json(payload)}")
    return node_id


def set_pin_default(client: McpStdioClient, request_id: int, *, asset_path: str, node_id: str, pin: str, value: Any) -> None:
    payload = call_tool(
        client,
        request_id,
        "graph.mutate",
        {
            "assetPath": asset_path,
            "graphType": "pcg",
            "graphName": GRAPH_NAME,
            "ops": [{"op": "setPinDefault", "args": {"target": {"nodeId": node_id, "pin": pin}, "value": value}}],
        },
    )
    op_results = payload.get("opResults")
    if not isinstance(op_results, list) or not op_results:
        raise RuntimeError(f"setPinDefault missing opResults: {compact_json(payload)}")
    first = op_results[0]
    if not isinstance(first, dict) or not first.get("ok"):
        raise RuntimeError(f"setPinDefault failed: {compact_json(payload)}")


def read_back_fields(
    client: McpStdioClient,
    request_id: int,
    *,
    node_id: str,
    field_specs: list[dict[str, Any]],
) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            f"field_specs={json.dumps(field_specs, ensure_ascii=False)}\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "def normalize(value):\n"
            "    if isinstance(value, (bool, int, float, str)):\n"
            "        return value\n"
            "    enum_value = getattr(value, 'value', None)\n"
            "    if isinstance(enum_value, (bool, int, float, str)):\n"
            "        return enum_value\n"
            "    return str(value)\n"
            "out = {'ok': True, 'fields': {}}\n"
            "for spec in field_specs:\n"
            "    field = spec['field']\n"
            "    candidates = spec['candidates']\n"
            "    matched = None\n"
            "    value = None\n"
            "    for candidate in candidates:\n"
            "        try:\n"
            "            value = settings.get_editor_property(candidate)\n"
            "            matched = candidate\n"
            "            break\n"
            "        except Exception:\n"
            "            continue\n"
            "    if matched is None:\n"
            "        raise RuntimeError(f'failed to resolve property for {field} on {node_path}')\n"
            "    out['fields'][field] = {'property': matched, 'value': normalize(value)}\n"
            "print(json.dumps(out, ensure_ascii=False))\n"
        ),
    )
    return parse_execute_json(payload)


def _is_empty_surface_value(value: Any) -> bool:
    if value is None:
        return True
    if isinstance(value, str):
        return value == ""
    return False


def _normalize_comparable_value(value: Any) -> Any:
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"true", "false"}:
            return lowered == "true"
        try:
            if "." in lowered:
                return float(lowered)
            return int(lowered)
        except Exception:
            return value
    return value


def audit_roundtrip_query_surface(node: dict[str, Any], supported_specs: list[dict[str, Any]]) -> dict[str, Any]:
    pins = node.get("pins")
    pin_by_name: dict[str, dict[str, Any]] = {}
    if isinstance(pins, list):
        for pin in pins:
            if isinstance(pin, dict) and isinstance(pin.get("name"), str):
                pin_by_name[pin["name"]] = pin

    fields: dict[str, Any] = {}
    pin_found = 0
    surfaced = 0
    matched = 0
    for spec in supported_specs:
        field = spec["field"]
        pin = pin_by_name.get(field)
        if not isinstance(pin, dict):
            fields[field] = {
                "pinFound": False,
                "surfaced": False,
                "matched": False,
                "surfaceValues": [],
            }
            continue

        pin_found += 1
        candidates: list[Any] = []
        default_obj = pin.get("default")
        for value in (
            pin.get("defaultValue"),
            pin.get("defaultText"),
            default_obj.get("value") if isinstance(default_obj, dict) else None,
            default_obj.get("text") if isinstance(default_obj, dict) else None,
        ):
            if not _is_empty_surface_value(value) and value not in candidates:
                candidates.append(value)

        expected_cmp = _normalize_comparable_value(spec["sample"])
        matched_here = any(_normalize_comparable_value(value) == expected_cmp for value in candidates)
        surfaced_here = bool(candidates)
        if surfaced_here:
            surfaced += 1
        if matched_here:
            matched += 1
        fields[field] = {
            "pinFound": True,
            "surfaced": surfaced_here,
            "matched": matched_here,
            "surfaceValues": candidates,
        }

    return {
        "counts": {
            "totalFields": len(supported_specs),
            "pinFoundFields": pin_found,
            "surfacedFields": surfaced,
            "matchedFields": matched,
        },
        "fields": fields,
    }


def classify_query_truth_gap(query_audit: dict[str, Any] | None) -> dict[str, list[str]]:
    gaps = {
        "missingPins": [],
        "unsurfacedFields": [],
        "mismatchedFields": [],
    }
    if not isinstance(query_audit, dict):
        return gaps

    fields = query_audit.get("fields")
    if not isinstance(fields, dict):
        return gaps

    for field_name, audit in fields.items():
        if not isinstance(field_name, str) or not isinstance(audit, dict):
            continue
        if not audit.get("pinFound"):
            gaps["missingPins"].append(field_name)
        elif not audit.get("surfaced"):
            gaps["unsurfacedFields"].append(field_name)
        elif not audit.get("matched"):
            gaps["mismatchedFields"].append(field_name)

    return gaps


def execute_construct_case(client: McpStdioClient, request_id: int, *, asset_path: str, node_class_path: str) -> dict[str, Any]:
    node_id = add_node(client, request_id, asset_path=asset_path, node_class_path=node_class_path)
    snapshot = query_pcg_snapshot(client, request_id + 1, asset_path)
    node = find_node(snapshot, node_id)
    if not isinstance(node, dict):
        raise RuntimeError(f"graph.query did not return added node {node_id}")
    return {"nodeId": node_id, "pinCount": len(node.get("pins", [])) if isinstance(node.get("pins"), list) else None}


def execute_roundtrip_case(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    node_class_path: str,
    properties_by_name: dict[str, dict[str, Any]],
    focus_fields: list[str],
) -> dict[str, Any]:
    supported_specs: list[dict[str, Any]] = []
    skipped_fields: list[str] = []
    for field in focus_fields:
        prop = properties_by_name.get(field)
        cpp_type = prop.get("cppType") if isinstance(prop, dict) else ""
        sample = sample_value_for_type(cpp_type or "", field)
        if sample is None:
            skipped_fields.append(field)
            continue
        supported_specs.append(
            {
                "field": field,
                "cppType": cpp_type,
                "normalizedCppType": "enum" if isinstance(cpp_type, str) and cpp_type.startswith("E") else cpp_type,
                "sample": sample,
                "candidates": property_candidates(field),
            }
        )

    if not supported_specs:
        return {"outcome": "skip", "reason": "unsupported_roundtrip_field_types", "skippedFields": skipped_fields}

    node_id = add_node(client, request_id, asset_path=asset_path, node_class_path=node_class_path)
    for index, spec in enumerate(supported_specs):
        set_pin_default(
            client,
            request_id + 1 + index,
            asset_path=asset_path,
            node_id=node_id,
            pin=spec["field"],
            value=spec["sample"],
        )

    query_snapshot = query_pcg_snapshot(client, request_id + 10, asset_path)
    query_node = find_node(query_snapshot, node_id)
    query_audit = audit_roundtrip_query_surface(query_node, supported_specs) if isinstance(query_node, dict) else None

    readback = read_back_fields(
        client,
        request_id + 20,
        node_id=node_id,
        field_specs=[{"field": spec["field"], "candidates": spec["candidates"]} for spec in supported_specs],
    )
    fields = readback.get("fields")
    if not isinstance(fields, dict):
        raise RuntimeError(f"roundtrip readback missing fields: {readback}")
    mismatches: list[str] = []
    for spec in supported_specs:
        actual = fields.get(spec["field"])
        if not isinstance(actual, dict):
            mismatches.append(spec["field"])
            continue
        expected = spec["sample"]
        actual_value = actual.get("value")
        if spec.get("normalizedCppType") == "enum":
            try:
                if int(actual_value) != int(expected):
                    mismatches.append(spec["field"])
            except Exception:
                mismatches.append(spec["field"])
        elif isinstance(expected, float):
            try:
                if abs(float(actual_value) - expected) > 1e-6:
                    mismatches.append(spec["field"])
            except Exception:
                mismatches.append(spec["field"])
        else:
            if actual_value != expected:
                mismatches.append(spec["field"])

    if mismatches:
        raise RuntimeError(
            f"roundtrip mismatch fields={mismatches} readback={compact_json(readback)} expected={compact_json({spec['field']: spec['sample'] for spec in supported_specs})}"
        )

    query_truth_gaps = classify_query_truth_gap(query_audit)
    if any(query_truth_gaps.values()):
        return {
            "outcome": "fail",
            "reason": "query_truth_gap",
            "nodeId": node_id,
            "checkedFields": [spec["field"] for spec in supported_specs],
            "skippedFields": skipped_fields,
            "queryAudit": query_audit,
            "queryTruthGaps": query_truth_gaps,
        }

    return {
        "outcome": "pass",
        "nodeId": node_id,
        "checkedFields": [spec["field"] for spec in supported_specs],
        "skippedFields": skipped_fields,
        "queryAudit": query_audit,
    }


def execute_dynamic_case(
    client: McpStdioClient,
    request_id: int,
    *,
    asset_path: str,
    node_class_path: str,
    properties_by_name: dict[str, dict[str, Any]],
    triggers: list[str],
) -> dict[str, Any]:
    supported: list[dict[str, Any]] = []
    skipped_triggers: list[str] = []
    for trigger in triggers:
        cpp_type = normalized_cpp_type_for_field(trigger, properties_by_name)
        sample = sample_value_for_type(cpp_type, trigger)
        if sample is None:
            skipped_triggers.append(trigger)
            continue
        supported.append({"field": trigger, "sample": sample})

    if not supported:
        return {"outcome": "skip", "reason": "unsupported_dynamic_trigger_types", "skippedTriggers": skipped_triggers}

    node_id = add_node(client, request_id, asset_path=asset_path, node_class_path=node_class_path)
    before_snapshot = query_pcg_snapshot(client, request_id + 1, asset_path)
    before_node = find_node(before_snapshot, node_id)
    if not isinstance(before_node, dict):
        raise RuntimeError(f"graph.query did not return dynamic node before mutation {node_id}")
    before_pins = before_node.get("pins")
    before_count = len(before_pins) if isinstance(before_pins, list) else 0

    trigger = supported[0]
    set_pin_default(
        client,
        request_id + 2,
        asset_path=asset_path,
        node_id=node_id,
        pin=trigger["field"],
        value=trigger["sample"],
    )
    after_snapshot = query_pcg_snapshot(client, request_id + 3, asset_path)
    after_node = find_node(after_snapshot, node_id)
    if not isinstance(after_node, dict):
        raise RuntimeError(f"graph.query did not return dynamic node after mutation {node_id}")
    after_pins = after_node.get("pins")
    after_count = len(after_pins) if isinstance(after_pins, list) else 0
    if after_count == before_count:
        raise RuntimeError(
            f"dynamic pin count did not change for trigger {trigger['field']} before={before_count} after={after_count}"
        )
    return {
        "outcome": "pass",
        "nodeId": node_id,
        "trigger": trigger["field"],
        "beforePinCount": before_count,
        "afterPinCount": after_count,
        "skippedTriggers": skipped_triggers,
    }


def run_case(
    client: McpStdioClient,
    *,
    request_id_base: int,
    case_index: int,
    entry: dict[str, Any],
    database_entry: dict[str, Any],
) -> dict[str, Any]:
    result = {
        "className": entry["className"],
        "displayName": entry["displayName"],
        "family": entry.get("family"),
        "profile": entry["profile"],
        "mode": entry["mode"],
        "status": "skip",
    }
    if entry["status"] != "ready":
        result["reason"] = f"plan_{entry['status']}"
        return result

    fixture = entry.get("fixture")
    if not isinstance(fixture, str) or not fixture:
        result["reason"] = "missing_fixture"
        return result

    asset_path = f"/Game/Codex/PCGPlan/{entry['className']}_{int(time.time() * 1000)}_{case_index}"
    actor_offset = float(case_index * 50)
    actor_path: str | None = None
    try:
        fixture_info = create_pcg_fixture(
            client,
            request_id_base,
            asset_path=asset_path,
            fixture_id=fixture,
            actor_offset=actor_offset,
        )
        actor_path = fixture_info.get("actorPath") if isinstance(fixture_info.get("actorPath"), str) else None
        node_class_path = database_entry.get("nodeClassPath")
        if not isinstance(node_class_path, str) or not node_class_path.startswith("/Script/"):
            raise RuntimeError(f"database missing nodeClassPath for {entry['className']}")

        properties_by_name = {
            prop["name"]: prop
            for prop in database_entry.get("properties", [])
            if isinstance(prop, dict) and isinstance(prop.get("name"), str)
        }
        focus = entry.get("focus")
        if not isinstance(focus, dict):
            focus = {}

        if entry["profile"] in {"construct_only", "construct_and_query"} or entry["mode"] == "recipe_case" and entry["profile"] == "construct_and_query":
            details = execute_construct_case(
                client,
                request_id_base + 100,
                asset_path=asset_path,
                node_class_path=node_class_path,
            )
            result["status"] = "pass"
            result["details"] = details
            return result

        if entry["profile"] == "read_write_roundtrip":
            details = execute_roundtrip_case(
                client,
                request_id_base + 100,
                asset_path=asset_path,
                node_class_path=node_class_path,
                properties_by_name=properties_by_name,
                focus_fields=[field for field in focus.get("fields", []) if isinstance(field, str)],
            )
            if details.get("outcome") == "skip":
                result["status"] = "skip"
                result["reason"] = details.get("reason", "unsupported_roundtrip")
                result["details"] = details
                return result
            if details.get("outcome") == "fail":
                result["status"] = "fail"
                result["reason"] = details.get("reason", "roundtrip_failure")
                result["details"] = details
                return result
            result["status"] = "pass"
            result["details"] = details
            return result

        if entry["profile"] == "dynamic_pin_probe":
            details = execute_dynamic_case(
                client,
                request_id_base + 100,
                asset_path=asset_path,
                node_class_path=node_class_path,
                properties_by_name=properties_by_name,
                triggers=[field for field in focus.get("dynamicTriggers", []) if isinstance(field, str)],
            )
            if details.get("outcome") == "skip":
                result["status"] = "skip"
                result["reason"] = details.get("reason", "unsupported_dynamic_trigger")
                result["details"] = details
                return result
            result["status"] = "pass"
            result["details"] = details
            return result

        result["reason"] = f"unsupported_profile_{entry['profile']}"
        return result
    except SystemExit as exc:
        result["status"] = "fail"
        result["reason"] = f"system_exit:{exc.code}"
        return result
    except Exception as exc:
        result["status"] = "fail"
        result["reason"] = str(exc)
        return result
    finally:
        try:
            cleanup_pcg_fixture(client, request_id_base + 900, asset_path=asset_path, actor_path=actor_path)
        except BaseException:
            pass


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    counter = Counter(result["status"] for result in results)
    summary: dict[str, Any] = {
        "totalCases": len(results),
        "passed": counter.get("pass", 0),
        "failed": counter.get("fail", 0),
        "skipped": counter.get("skip", 0),
    }

    query_total = 0
    query_pin_found = 0
    query_surfaced = 0
    query_matched = 0
    query_truth_failed_cases = 0
    query_gap_field_totals = Counter()
    failure_reasons = Counter()
    for result in results:
        reason = result.get("reason")
        if isinstance(reason, str) and reason:
            failure_reasons[reason] += 1
        details = result.get("details")
        if not isinstance(details, dict):
            continue
        query_audit = details.get("queryAudit")
        if not isinstance(query_audit, dict):
            continue
        counts = query_audit.get("counts")
        if not isinstance(counts, dict):
            continue
        query_total += int(counts.get("totalFields", 0) or 0)
        query_pin_found += int(counts.get("pinFoundFields", 0) or 0)
        query_surfaced += int(counts.get("surfacedFields", 0) or 0)
        query_matched += int(counts.get("matchedFields", 0) or 0)
        if result.get("status") == "fail" and result.get("reason") == "query_truth_gap":
            query_truth_failed_cases += 1
            gaps = details.get("queryTruthGaps")
            if isinstance(gaps, dict):
                for key in ("missingPins", "unsurfacedFields", "mismatchedFields"):
                    values = gaps.get(key)
                    if isinstance(values, list):
                        query_gap_field_totals[key] += len([value for value in values if isinstance(value, str)])

    if query_total:
        summary["queryAudit"] = {
            "totalFields": query_total,
            "pinFoundFields": query_pin_found,
            "surfacedFields": query_surfaced,
            "matchedFields": query_matched,
        }
        summary["queryTruthFailedCases"] = query_truth_failed_cases
        summary["queryTruthGapFields"] = {
            "missingPins": query_gap_field_totals.get("missingPins", 0),
            "unsurfacedFields": query_gap_field_totals.get("unsurfacedFields", 0),
            "mismatchedFields": query_gap_field_totals.get("mismatchedFields", 0),
        }

    if failure_reasons:
        summary["failureReasons"] = dict(sorted(failure_reasons.items()))

    return summary


def execute_case_with_fresh_client(
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    entry: dict[str, Any],
    database_entry: dict[str, Any],
) -> dict[str, Any]:
    client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=timeout_s)
    transcript = io.StringIO()
    try:
        with contextlib.redirect_stdout(transcript):
            _ = client.request(1, "initialize", {})
            wait_for_bridge_ready(client)
            result = run_case(
                client,
                request_id_base=request_id_base,
                case_index=case_index,
                entry=entry,
                database_entry=database_entry,
            )
    except Exception as exc:
        result = {
            "className": entry.get("className"),
            "displayName": entry.get("displayName"),
            "profile": entry.get("profile"),
            "mode": entry.get("mode"),
            "status": "fail",
            "reason": str(exc),
        }
    finally:
        client.close()
    log_text = transcript.getvalue().strip()
    if log_text:
        result["logs"] = log_text.splitlines()[-10:]
        if result.get("status") == "fail" and isinstance(result.get("reason"), str) and result["reason"].startswith("system_exit:"):
            fail_lines = [line for line in result["logs"] if "[FAIL]" in line]
            if fail_lines:
                result["reason"] = fail_lines[-1]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Run first-version PCG graph test plan cases against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the project-local loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--plan-path", default="", help="Optional pre-generated PCG plan JSON path")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)

    if args.plan_path:
        with open(args.plan_path, encoding="utf-8") as handle:
            plan = json.load(handle)
    else:
        plan = build_plan("pcg", REPO_ROOT / "workspace" / "Loomle")

    with open(REPO_ROOT / "workspace" / "Loomle" / "pcg" / "catalogs" / "node-database.json", encoding="utf-8") as handle:
        database = json.load(handle)
    nodes_by_class = {node["className"]: node for node in database["nodes"] if isinstance(node, dict) and isinstance(node.get("className"), str)}

    ready_entries = [entry for entry in plan["entries"] if isinstance(entry, dict) and entry.get("status") == "ready"]
    if args.max_cases > 0:
        ready_entries = ready_entries[: args.max_cases]

    results: list[dict[str, Any]] = []
    for index, entry in enumerate(ready_entries, start=1):
        database_entry = nodes_by_class.get(entry["className"])
        if not isinstance(database_entry, dict):
            results.append(
                {
                    "className": entry.get("className"),
                    "displayName": entry.get("displayName"),
                    "profile": entry.get("profile"),
                    "mode": entry.get("mode"),
                    "status": "fail",
                    "reason": "missing_database_entry",
                }
            )
            continue
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=50000 + index * 1000,
            case_index=index,
            entry=entry,
            database_entry=database_entry,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {entry['className']} ({entry['profile']})")

    report = {
        "graphType": "pcg",
        "generatedPlanSummary": plan.get("summary", {}),
        "executedEntries": len(ready_entries),
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
