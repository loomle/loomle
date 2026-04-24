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
from run_pcg_workflow_truth_suite import (  # noqa: E402
    find_node,
    node_pin_surface_values,
    normalize_surface_value,
    verify_graph,
)


class SelectorSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


def safe_parse_execute_json(payload: dict[str, Any]) -> dict[str, Any]:
    result = payload.get("result")
    candidates: list[str] = []
    if isinstance(result, str) and result.strip():
        candidates.append(result.strip())

    logs = payload.get("logs")
    if isinstance(logs, list):
        for entry in reversed(logs):
            if not isinstance(entry, dict):
                continue
            output = entry.get("output")
            if isinstance(output, str) and output.strip():
                candidates.append(output.strip())

    for candidate in candidates:
        try:
            parsed = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(parsed, dict):
            return parsed

    raise SelectorSuiteError(
        "runner_error",
        f"execute payload did not contain a JSON object result: {compact_json(payload)}",
    )


SELECTOR_CASES = [
    {
        "id": "filter_by_attribute_attribute_selector",
        "fixture": "pcg_graph",
        "families": ["filter", "route"],
        "selectorFields": ["TargetAttribute"],
        "querySurfaceKind": "pin_default",
        "summary": "FilterByAttribute should surface plain attribute selectors through pcg.query and engine truth.",
        "setupKind": "filter_target_attribute",
        "selectorValue": "Desert_Cactus",
        "expectedEngine": {
            "selection": "Attribute",
            "attributeName": "Desert_Cactus",
            "propertyName": "",
            "extraNames": [],
        },
        "expectedQuery": {
            "pin": "TargetAttribute",
            "surfacedValues": ["Desert_Cactus"],
        },
    },
    {
        "id": "filter_by_attribute_property_selector",
        "fixture": "pcg_graph",
        "families": ["filter", "route"],
        "selectorFields": ["TargetAttribute"],
        "querySurfaceKind": "pin_default",
        "summary": "FilterByAttribute should preserve property-accessor selectors such as Position.Z.",
        "setupKind": "filter_target_attribute",
        "selectorValue": "Position.Z",
        "expectedEngine": {
            "selection": "Attribute",
            "attributeName": "Position",
            "propertyName": "",
            "extraNames": ["Z"],
        },
        "expectedQuery": {
            "pin": "TargetAttribute",
            "surfacedValues": ["Position.Z"],
        },
    },
    {
        "id": "get_actor_property_selector_surface",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source"],
        "selectorFields": ["ActorSelector", "OutputAttributeName"],
        "querySurfaceKind": "effective_settings",
        "summary": "GetActorProperty should surface actor selector and output selector structure through effectiveSettings.",
        "setupKind": "get_actor_property",
        "expectedEngine": {
            "actorSelector": {
                "actorFilter": "AllWorldActors",
                "actorSelection": "ByClass",
                "actorSelectionClassPath": "/Script/Engine.Actor",
                "selectMultiple": True,
            },
            "outputAttributeName": {
                "selection": "Attribute",
                "attributeName": "SurfaceTag",
                "propertyName": "",
                "extraNames": [],
            },
            "propertyName": "Tags",
        },
        "expectedQuery": {
            "actorSelector": {
                "actorFilter": "AllWorldActors",
                "actorSelection": "ByClass",
                "actorSelectionClassPath": "/Script/Engine.Actor",
                "selectMultiple": True,
            },
            "outputAttributeName": {
                "selection": "Attribute",
                "name": "SurfaceTag",
                "attributeOrProperty": "SurfaceTag",
                "text": "SurfaceTag",
                "accessors": [],
            },
            "propertyName": "Tags",
        },
    },
    {
        "id": "static_mesh_spawner_mesh_selector_surface",
        "fixture": "pcg_graph",
        "families": ["spawn"],
        "selectorFields": ["MeshSelectorParameters"],
        "querySurfaceKind": "effective_settings",
        "summary": "StaticMeshSpawner should surface mesh selector structure through effectiveSettings.",
        "setupKind": "static_mesh_spawner",
        "expectedEngine": {
            "meshSelectorTypeClassPath": "/Script/PCG.PCGMeshSelectorByAttribute",
            "meshSelectorParametersClassPath": "/Script/PCG.PCGMeshSelectorByAttribute",
            "meshSelector": {
                "kind": "byAttribute",
                "attributeName": "Mesh",
            },
            "outAttributeName": "ChosenMesh",
        },
        "expectedQuery": {
            "meshSelector": {
                "kind": "byAttribute",
                "attributeName": "Mesh",
            },
            "outAttributeName": "ChosenMesh",
        },
    },
]


def list_cases_payload() -> dict[str, Any]:
    selector_fields = sorted(
        {
            field
            for case in SELECTOR_CASES
            for field in case.get("selectorFields", [])
            if isinstance(field, str) and field
        }
    )
    query_surface_kinds = sorted(
        {
            case["querySurfaceKind"]
            for case in SELECTOR_CASES
            if isinstance(case.get("querySurfaceKind"), str) and case["querySurfaceKind"]
        }
    )
    return {
        "version": "1",
        "graphType": "pcg",
        "suite": "selector_truth",
        "summary": {
            "totalCases": len(SELECTOR_CASES),
            "worldContextCases": sum(1 for case in SELECTOR_CASES if case["fixture"] == "pcg_graph_with_world_actor"),
            "selectorFields": selector_fields,
            "querySurfaceKinds": query_surface_kinds,
        },
        "cases": [
            {
                "id": case["id"],
                "fixture": case["fixture"],
                "families": case.get("families", []),
                "selectorFields": case.get("selectorFields", []),
                "querySurfaceKind": case["querySurfaceKind"],
                "summary": case["summary"],
            }
            for case in SELECTOR_CASES
        ],
    }


def _normalize_empty_name(value: Any) -> str:
    text = "" if value is None else str(value)
    if text in {"None", "@None"}:
        return ""
    return text


def _canonical_selector_selection(value: Any) -> str:
    text = "" if value is None else str(value)
    lowered = text.lower()
    if "property" in lowered and "extra" not in lowered:
        return "Property"
    if "extra" in lowered:
        return "ExtraProperty"
    return "Attribute"


def _canonical_pascal_enum(value: Any) -> str:
    text = "" if value is None else str(value)
    token = text.split(".")[-1].split(":")[0].strip("<> ")
    if not token:
        return ""
    if "_" in token:
        return "".join(part.capitalize() for part in token.lower().split("_") if part)
    if token.isupper():
        return token.title().replace("_", "")
    return token


def _assert_expected_subset(actual: dict[str, Any], expected: dict[str, Any], *, kind: str, prefix: str) -> None:
    for key, expected_value in expected.items():
        actual_value = actual.get(key)
        if isinstance(expected_value, dict):
            if not isinstance(actual_value, dict):
                raise SelectorSuiteError(kind, f"{prefix}.{key} missing structured value: {compact_json(actual)}")
            _assert_expected_subset(actual_value, expected_value, kind=kind, prefix=f"{prefix}.{key}")
            continue
        if isinstance(expected_value, list):
            if actual_value != expected_value:
                raise SelectorSuiteError(
                    kind,
                    f"{prefix}.{key} mismatch: expected={expected_value!r} actual={actual_value!r}",
                )
            continue
        if actual_value != expected_value:
            raise SelectorSuiteError(
                kind,
                f"{prefix}.{key} mismatch: expected={expected_value!r} actual={actual_value!r}",
            )


def _read_filter_selector_truth(client: McpStdioClient, request_id: int, *, node_id: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "helpers = unreal.PCGAttributePropertySelectorBlueprintHelpers\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "selector = settings.get_editor_property('target_attribute')\n"
            "print(json.dumps({\n"
            "    'ok': True,\n"
            "    'selection': str(helpers.get_selection(selector)).split('.')[-1],\n"
            "    'attributeName': str(helpers.get_attribute_name(selector)),\n"
            "    'propertyName': str(helpers.get_property_name(selector)),\n"
            "    'extraNames': list(helpers.get_extra_names(selector)),\n"
            "    'text': str(selector),\n"
            "}, ensure_ascii=False))\n"
        ),
    )
    truth = safe_parse_execute_json(payload)
    if truth.get("ok") is not True:
        raise SelectorSuiteError("engine_selector_gap", f"filter selector execute readback failed: {compact_json(truth)}")
    truth["selection"] = _canonical_selector_selection(truth.get("selection"))
    truth["attributeName"] = _normalize_empty_name(truth.get("attributeName"))
    truth["propertyName"] = _normalize_empty_name(truth.get("propertyName"))
    return truth


def _read_get_actor_property_selector_truth(client: McpStdioClient, request_id: int, *, node_id: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "helpers = unreal.PCGAttributePropertySelectorBlueprintHelpers\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "def get_prop(obj, names):\n"
            "    errors = []\n"
            "    for name in names:\n"
            "        try:\n"
            "            return obj.get_editor_property(name)\n"
            "        except Exception as exc:\n"
            "            errors.append(f'{name}: {exc}')\n"
            "    raise RuntimeError('; '.join(errors))\n"
            "actor_selector = settings.get_editor_property('actor_selector')\n"
            "output_selector = settings.get_editor_property('output_attribute_name')\n"
            "actor_selection_class = get_prop(actor_selector, ['actor_selection_class'])\n"
            "print(json.dumps({\n"
            "    'ok': True,\n"
            "    'actorSelector': {\n"
            "        'actorFilter': str(get_prop(actor_selector, ['actor_filter'])).split('.')[-1],\n"
            "        'actorSelection': str(get_prop(actor_selector, ['actor_selection'])).split('.')[-1],\n"
            "        'actorSelectionClassPath': actor_selection_class.get_path_name() if actor_selection_class is not None else '',\n"
            "        'selectMultiple': bool(get_prop(actor_selector, ['b_select_multiple', 'select_multiple'])),\n"
            "    },\n"
            "    'outputAttributeName': {\n"
            "        'selection': str(helpers.get_selection(output_selector)).split('.')[-1],\n"
            "        'attributeName': str(helpers.get_attribute_name(output_selector)),\n"
            "        'propertyName': str(helpers.get_property_name(output_selector)),\n"
            "        'extraNames': list(helpers.get_extra_names(output_selector)),\n"
            "        'text': str(output_selector),\n"
            "    },\n"
            "    'propertyName': str(settings.get_editor_property('property_name')),\n"
            "}, ensure_ascii=False))\n"
        ),
    )
    truth = safe_parse_execute_json(payload)
    if truth.get("ok") is not True:
        raise SelectorSuiteError("engine_selector_gap", f"GetActorProperty execute readback failed: {compact_json(truth)}")
    actor_selector_truth = truth.get("actorSelector")
    if isinstance(actor_selector_truth, dict):
        actor_selector_truth["actorFilter"] = _canonical_pascal_enum(actor_selector_truth.get("actorFilter"))
        actor_selector_truth["actorSelection"] = _canonical_pascal_enum(actor_selector_truth.get("actorSelection"))
    output_truth = truth.get("outputAttributeName")
    if isinstance(output_truth, dict):
        output_truth["selection"] = _canonical_selector_selection(output_truth.get("selection"))
        output_truth["attributeName"] = _normalize_empty_name(output_truth.get("attributeName"))
        output_truth["propertyName"] = _normalize_empty_name(output_truth.get("propertyName"))
    return truth


def _read_static_mesh_spawner_selector_truth(client: McpStdioClient, request_id: int, *, node_id: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "mesh_selector_type = settings.get_editor_property('mesh_selector_type')\n"
            "mesh_selector = settings.get_editor_property('mesh_selector_parameters')\n"
            "mesh_selector_class = mesh_selector.get_class() if mesh_selector is not None else None\n"
            "print(json.dumps({\n"
            "    'ok': True,\n"
            "    'meshSelectorTypeClassPath': mesh_selector_type.get_path_name() if mesh_selector_type is not None else '',\n"
            "    'meshSelectorParametersClassPath': mesh_selector_class.get_path_name() if mesh_selector_class is not None else '',\n"
            "    'meshSelector': {\n"
            "        'kind': 'byAttribute' if mesh_selector is not None else '',\n"
            "        'attributeName': str(mesh_selector.get_editor_property('attribute_name')) if mesh_selector is not None else '',\n"
            "    },\n"
            "    'outAttributeName': str(settings.get_editor_property('out_attribute_name')),\n"
            "}, ensure_ascii=False))\n"
        ),
    )
    truth = safe_parse_execute_json(payload)
    if truth.get("ok") is not True:
        raise SelectorSuiteError("engine_selector_gap", f"StaticMeshSpawner execute readback failed: {compact_json(truth)}")
    return truth


def _setup_filter_selector_case(
    client: McpStdioClient, request_id_base: int, *, asset_path: str, selector_value: str
) -> tuple[str, dict[str, Any]]:
    node_id = add_node(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGFilterByAttributeSettings",
    )
    set_pin_default(client, request_id_base + 2, asset_path=asset_path, node_id=node_id, pin="FilterMode", value="FilterByValue")
    set_pin_default(client, request_id_base + 3, asset_path=asset_path, node_id=node_id, pin="TargetAttribute", value=selector_value)
    return node_id, {"selectorValue": selector_value}


def _setup_get_actor_property_case(
    client: McpStdioClient, request_id_base: int, *, asset_path: str
) -> tuple[str, dict[str, Any]]:
    node_id = add_node(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGGetActorPropertySettings",
    )
    for index, (pin, value) in enumerate(
        [
            ("ActorFilter", "AllWorldActors"),
            ("ActorSelection", "ByClass"),
            ("ActorSelectionClass", "/Script/Engine.Actor"),
            ("bSelectMultiple", True),
            ("PropertyName", "Tags"),
            ("OutputAttributeName", "SurfaceTag"),
        ],
        start=2,
    ):
        set_pin_default(client, request_id_base + index, asset_path=asset_path, node_id=node_id, pin=pin, value=value)
    return node_id, {}


def _setup_static_mesh_spawner_case(
    client: McpStdioClient, request_id_base: int, *, asset_path: str
) -> tuple[str, dict[str, Any]]:
    node_id = add_node(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGStaticMeshSpawnerSettings",
    )
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base + 10,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "settings.set_mesh_selector_type(unreal.PCGMeshSelectorByAttribute.static_class())\n"
            "mesh_selector = settings.get_editor_property('mesh_selector_parameters')\n"
            "mesh_selector.set_editor_property('attribute_name', unreal.Name('Mesh'))\n"
            "settings.set_editor_property('out_attribute_name', unreal.Name('ChosenMesh'))\n"
            "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
        ),
    )
    result = safe_parse_execute_json(payload)
    if result.get("ok") is not True:
        raise SelectorSuiteError("mutate_gap", f"StaticMeshSpawner selector setup failed: {compact_json(result)}")
    return node_id, {}


def _query_filter_selector_truth(node: dict[str, Any], expected_query: dict[str, Any]) -> dict[str, Any]:
    pin_name = expected_query["pin"]
    surface_values = node_pin_surface_values(node, pin_name)
    if not surface_values:
        raise SelectorSuiteError(
            "query_selector_unsurfaced",
            f"selector query surface missing default for {pin_name}: {compact_json(node)}",
        )
    expected_values = expected_query.get("surfacedValues", [])
    normalized_values = [normalize_surface_value(value) for value in surface_values]
    normalized_expected = [normalize_surface_value(value) for value in expected_values]
    for expected in normalized_expected:
        if expected not in normalized_values:
            raise SelectorSuiteError(
                "query_selector_mismatch",
                f"selector query surface mismatch for {pin_name}: expected={expected_values!r} surfaced={surface_values!r}",
            )
    return {"pin": pin_name, "surfacedValues": surface_values}


def _query_effective_settings(node: dict[str, Any]) -> dict[str, Any]:
    effective_settings = node.get("effectiveSettings")
    if not isinstance(effective_settings, dict):
        raise SelectorSuiteError("query_selector_unsurfaced", f"selector query missing effectiveSettings: {compact_json(node)}")
    return effective_settings


def _run_selector_case(
    client: McpStdioClient,
    *,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    result = {
        "caseId": case["id"],
        "fixture": case["fixture"],
        "families": case.get("families", []),
        "selectorFields": case.get("selectorFields", []),
        "querySurfaceKind": case["querySurfaceKind"],
        "status": "fail",
    }
    surface_matrix = blank_surface_matrix()
    asset_path = f"/Game/Codex/PCGSelectorTruth/{case['id']}_{case_index}"
    actor_path: str | None = None
    try:
        fixture_info = create_pcg_fixture(
            client,
            request_id_base,
            asset_path=asset_path,
            fixture_id=case["fixture"],
            actor_offset=float(case_index * 125),
        )
        actor_path = fixture_info.get("actorPath") if isinstance(fixture_info.get("actorPath"), str) else None

        if case["setupKind"] == "filter_target_attribute":
            node_id, setup_details = _setup_filter_selector_case(
                client,
                request_id_base + 10,
                asset_path=asset_path,
                selector_value=case["selectorValue"],
            )
        elif case["setupKind"] == "get_actor_property":
            node_id, setup_details = _setup_get_actor_property_case(
                client,
                request_id_base + 10,
                asset_path=asset_path,
            )
        elif case["setupKind"] == "static_mesh_spawner":
            node_id, setup_details = _setup_static_mesh_spawner_case(
                client,
                request_id_base + 10,
                asset_path=asset_path,
            )
        else:
            raise SelectorSuiteError("runner_error", f"unsupported selector setup kind {case['setupKind']}")

        surface_matrix["mutate"] = "pass"
        snapshot = query_pcg_snapshot(client, request_id_base + 100, asset_path)
        node = find_node(snapshot, node_id)
        if not isinstance(node, dict):
            raise SelectorSuiteError("query_selector_unsurfaced", f"selector case missing node in pcg.query: {node_id}")
        surface_matrix["queryStructure"] = "pass"
        verify_details = verify_graph(client, request_id_base + 110, asset_path)
        surface_matrix["verify"] = "pass"
        surface_matrix["diagnostics"] = "pass"

        if case["setupKind"] == "filter_target_attribute":
            query_truth = _query_filter_selector_truth(node, case["expectedQuery"])
            surface_matrix["queryTruth"] = "pass"
            engine_truth = _read_filter_selector_truth(client, request_id_base + 120, node_id=node_id)
        elif case["setupKind"] == "get_actor_property":
            effective_settings = _query_effective_settings(node)
            _assert_expected_subset(effective_settings, case["expectedQuery"], kind="query_selector_mismatch", prefix="effectiveSettings")
            query_truth = {
                "effectiveSettings": {
                    "actorSelector": effective_settings.get("actorSelector"),
                    "outputAttributeName": effective_settings.get("outputAttributeName"),
                    "propertyName": effective_settings.get("propertyName"),
                }
            }
            surface_matrix["queryTruth"] = "pass"
            engine_truth = _read_get_actor_property_selector_truth(client, request_id_base + 120, node_id=node_id)
        elif case["setupKind"] == "static_mesh_spawner":
            effective_settings = _query_effective_settings(node)
            _assert_expected_subset(effective_settings, case["expectedQuery"], kind="query_selector_mismatch", prefix="effectiveSettings")
            query_truth = {
                "effectiveSettings": {
                    "meshSelector": effective_settings.get("meshSelector"),
                    "outAttributeName": effective_settings.get("outAttributeName"),
                }
            }
            surface_matrix["queryTruth"] = "pass"
            engine_truth = _read_static_mesh_spawner_selector_truth(client, request_id_base + 120, node_id=node_id)
        else:
            raise SelectorSuiteError("runner_error", f"unsupported selector setup kind {case['setupKind']}")

        _assert_expected_subset(engine_truth, case["expectedEngine"], kind="engine_selector_mismatch", prefix="engineTruth")
        surface_matrix["engineTruth"] = "pass"
        result["status"] = "pass"
        result["details"] = {
            "surfaceMatrix": surface_matrix,
            "setup": setup_details,
            "queryTruth": query_truth,
            "engineTruth": engine_truth,
            "verify": verify_details,
        }
        return result
    except SelectorSuiteError as exc:
        if exc.kind in {"query_selector_unsurfaced", "query_selector_mismatch"}:
            surface_matrix["queryTruth"] = "fail"
            if surface_matrix["queryStructure"] == "not_run":
                surface_matrix["queryStructure"] = "pass"
        elif exc.kind == "engine_selector_mismatch":
            surface_matrix["engineTruth"] = "fail"
        elif exc.kind == "verify_gap":
            surface_matrix["verify"] = "fail"
            surface_matrix["diagnostics"] = "fail"
        elif exc.kind == "mutate_gap":
            surface_matrix["mutate"] = "fail"
        result["failureKind"] = exc.kind
        result["reason"] = str(exc)
        result["details"] = {"surfaceMatrix": surface_matrix}
        return result
    except Exception as exc:
        result["failureKind"] = "runner_error"
        result["reason"] = str(exc)
        result["details"] = {"surfaceMatrix": surface_matrix}
        return result
    finally:
        try:
            cleanup_pcg_fixture(client, request_id_base + 900, asset_path=asset_path, actor_path=actor_path)
        except BaseException:
            pass


def execute_case_with_fresh_client(
    *,
    project_root: Path,
    loomle_binary: Path,
    timeout_s: float,
    request_id_base: int,
    case_index: int,
    case: dict[str, Any],
) -> dict[str, Any]:
    client = McpStdioClient(project_root=project_root, server_binary=loomle_binary, timeout_s=timeout_s)
    transcript = io.StringIO()
    try:
        with contextlib.redirect_stdout(transcript):
            _ = client.request(1, "initialize", {})
            wait_for_bridge_ready(client)
            result = _run_selector_case(
                client,
                request_id_base=request_id_base,
                case_index=case_index,
                case=case,
            )
    except BaseException as exc:
        result = {
            "caseId": case["id"],
            "fixture": case["fixture"],
            "families": case.get("families", []),
            "selectorFields": case.get("selectorFields", []),
            "querySurfaceKind": case["querySurfaceKind"],
            "status": "fail",
            "failureKind": "runner_error",
            "reason": str(exc),
        }
    finally:
        client.close()

    log_text = transcript.getvalue().strip()
    if log_text:
        result["logs"] = log_text.splitlines()[-10:]
    return result


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    status_counter = Counter(result["status"] for result in results)
    failure_kinds = Counter(
        result["failureKind"]
        for result in results
        if result.get("status") == "fail" and isinstance(result.get("failureKind"), str)
    )
    query_surface_kinds = Counter(
        result["querySurfaceKind"]
        for result in results
        if isinstance(result.get("querySurfaceKind"), str)
    )
    selector_field_counts = Counter(
        field
        for result in results
        for field in result.get("selectorFields", [])
        if isinstance(field, str) and field
    )
    surface_totals: dict[str, Counter[str]] = {
        surface: Counter()
        for surface in ("mutate", "queryStructure", "queryTruth", "engineTruth", "verify", "diagnostics")
    }
    family_rows: dict[str, dict[str, Any]] = {}
    for result in results:
        details = result.get("details")
        if isinstance(details, dict):
            surface_matrix = details.get("surfaceMatrix")
            if isinstance(surface_matrix, dict):
                for surface, counter in surface_totals.items():
                    value = surface_matrix.get(surface)
                    if isinstance(value, str) and value:
                        counter[value] += 1
        for family in result.get("families", []):
            if not isinstance(family, str) or not family:
                continue
            row = family_rows.setdefault(
                family,
                {"family": family, "totalCases": 0, "passed": 0, "failed": 0, "failureKinds": Counter()},
            )
            row["totalCases"] += 1
            if result.get("status") == "pass":
                row["passed"] += 1
            elif result.get("status") == "fail":
                row["failed"] += 1
                if isinstance(result.get("failureKind"), str):
                    row["failureKinds"][result["failureKind"]] += 1

    return {
        "totalCases": len(results),
        "passed": status_counter.get("pass", 0),
        "failed": status_counter.get("fail", 0),
        "querySurfaceKinds": dict(sorted(query_surface_kinds.items())),
        "selectorFields": dict(sorted(selector_field_counts.items())),
        "failureKinds": dict(sorted(failure_kinds.items())),
        "surfaceMatrix": {
            surface: dict(sorted(counter.items()))
            for surface, counter in surface_totals.items()
            if counter
        },
        "familySummary": [
            {
                "family": row["family"],
                "totalCases": row["totalCases"],
                "passed": row["passed"],
                "failed": row["failed"],
                "failureKinds": dict(sorted(row["failureKinds"].items())),
            }
            for row in sorted(family_rows.values(), key=lambda item: item["family"])
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PCG selector truth regressions against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--list-cases", action="store_true", help="Print the selector case registry and exit")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)
    cases = SELECTOR_CASES[: args.max_cases] if args.max_cases > 0 else SELECTOR_CASES

    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=92000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "selector_truth",
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
