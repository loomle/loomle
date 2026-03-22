#!/usr/bin/env python3
import argparse
import contextlib
import io
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e"))

from test_bridge_smoke import (  # noqa: E402
    McpStdioClient,
    call_execute_exec_with_retry,
    resolve_default_loomle_binary,
    resolve_project_root,
)

sys.path.insert(0, str(REPO_ROOT / "tools"))
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
from run_pcg_workflow_truth_suite import find_node, verify_graph  # noqa: E402
from run_pcg_selector_truth_suite import (  # noqa: E402
    _assert_expected_subset,
    _canonical_pascal_enum,
    _read_get_actor_property_selector_truth,
    _read_static_mesh_spawner_selector_truth,
    safe_parse_execute_json,
)


class EffectiveSettingsSuiteError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind


EFFECTIVE_SETTINGS_CASES = [
    {
        "id": "get_actor_property_effective_settings_truth",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source"],
        "assertionKind": "truth",
        "nodeClassPath": "/Script/PCG.PCGGetActorPropertySettings",
        "effectiveSettingsGroups": ["actorSelector", "outputAttributeName", "componentSelector"],
        "summary": "GetActorProperty should expose structured actor/component selector truth through effectiveSettings.",
        "setupKind": "get_actor_property",
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
    },
    {
        "id": "get_spline_effective_settings_truth",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source"],
        "assertionKind": "truth",
        "nodeClassPath": "/Script/PCG.PCGGetSplineSettings",
        "effectiveSettingsGroups": ["actorSelector", "componentSelector"],
        "summary": "GetSpline should expose structured actor and component selector truth through effectiveSettings.",
        "setupKind": "get_spline",
        "expectedQuery": {
            "actorSelector": {
                "actorFilter": "AllWorldActors",
                "actorSelection": "ByClass",
                "actorSelectionClassPath": "/Script/Engine.Actor",
            },
            "componentSelector": {
                "componentSelection": "ByClass",
                "componentSelectionClassPath": "/Script/Engine.SplineComponent",
            },
            "dataFilter": "PolyLine",
        },
        "expectedEngine": {
            "actorSelector": {
                "actorFilter": "AllWorldActors",
                "actorSelection": "ByClass",
                "actorSelectionClassPath": "/Script/Engine.Actor",
            },
            "componentSelector": {
                "componentSelection": "ByClass",
                "componentSelectionClassPath": "/Script/Engine.SplineComponent",
            },
            "dataFilter": "PolyLine",
        },
    },
    {
        "id": "static_mesh_spawner_effective_settings_truth",
        "fixture": "pcg_graph",
        "families": ["spawn"],
        "assertionKind": "truth",
        "nodeClassPath": "/Script/PCG.PCGStaticMeshSpawnerSettings",
        "effectiveSettingsGroups": ["meshSelector"],
        "summary": "StaticMeshSpawner should expose mesh selector truth through effectiveSettings.",
        "setupKind": "static_mesh_spawner",
        "expectedQuery": {
            "meshSelector": {
                "kind": "byAttribute",
                "attributeName": "Mesh",
            },
            "outAttributeName": "ChosenMesh",
        },
        "expectedEngine": {
            "meshSelectorTypeClassPath": "/Script/PCG.PCGMeshSelectorByAttribute",
            "meshSelectorParametersClassPath": "/Script/PCG.PCGMeshSelectorByAttribute",
            "meshSelector": {
                "kind": "byAttribute",
                "attributeName": "Mesh",
            },
            "outAttributeName": "ChosenMesh",
        },
    },
    {
        "id": "data_from_actor_effective_settings_presence",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["source"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGDataFromActorSettings",
        "effectiveSettingsGroups": ["actorSelector", "componentSelector"],
        "summary": "Get Actor Data should expose actor and component selector groups through effectiveSettings.",
        "setupKind": "bare_node",
    },
    {
        "id": "apply_on_actor_effective_settings_presence",
        "fixture": "pcg_graph_with_world_actor",
        "families": ["meta"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGApplyOnActorSettings",
        "effectiveSettingsGroups": ["actorSelector", "propertyOverrides", "applyBehavior"],
        "summary": "Apply On Object should expose actor targeting and override behavior groups through effectiveSettings.",
        "setupKind": "bare_node",
    },
    {
        "id": "spawn_actor_effective_settings_presence",
        "fixture": "pcg_graph",
        "families": ["meta"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGSpawnActorSettings",
        "effectiveSettingsGroups": ["templateIdentity", "spawnBehavior", "propertyOverrides", "dataLayerSettings", "hlodSettings"],
        "summary": "Spawn Actor should expose grouped spawn identity and override settings through effectiveSettings.",
        "setupKind": "bare_node",
    },
    {
        "id": "spawn_spline_effective_settings_presence",
        "fixture": "pcg_graph",
        "families": ["meta"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGSpawnSplineSettings",
        "effectiveSettingsGroups": ["templateIdentity", "spawnBehavior"],
        "summary": "Spawn Spline should expose grouped template identity and spawn behavior through effectiveSettings.",
        "setupKind": "bare_node",
    },
    {
        "id": "spawn_spline_mesh_effective_settings_presence",
        "fixture": "pcg_graph",
        "families": ["meta"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGSpawnSplineMeshSettings",
        "effectiveSettingsGroups": ["templateIdentity", "spawnBehavior", "meshSelector"],
        "summary": "Spawn Spline Mesh should expose grouped template identity, behavior, and mesh selection through effectiveSettings.",
        "setupKind": "bare_node",
    },
    {
        "id": "skinned_mesh_spawner_effective_settings_presence",
        "fixture": "pcg_graph",
        "families": ["spawn"],
        "assertionKind": "presence_shape",
        "nodeClassPath": "/Script/PCG.PCGSkinnedMeshSpawnerSettings",
        "effectiveSettingsGroups": ["templateIdentity", "spawnBehavior", "meshSelector"],
        "summary": "Skinned Mesh Spawner should expose grouped template identity, behavior, and mesh selection through effectiveSettings.",
        "setupKind": "bare_node",
    },
]


def list_cases_payload() -> dict[str, Any]:
    families = sorted(
        {
            family
            for case in EFFECTIVE_SETTINGS_CASES
            for family in case.get("families", [])
            if isinstance(family, str) and family
        }
    )
    return {
        "version": "1",
        "graphType": "pcg",
        "suite": "effective_settings",
        "summary": {
            "totalCases": len(EFFECTIVE_SETTINGS_CASES),
            "truthCases": sum(1 for case in EFFECTIVE_SETTINGS_CASES if case["assertionKind"] == "truth"),
            "presenceShapeCases": sum(1 for case in EFFECTIVE_SETTINGS_CASES if case["assertionKind"] == "presence_shape"),
            "worldContextCases": sum(1 for case in EFFECTIVE_SETTINGS_CASES if case["fixture"] == "pcg_graph_with_world_actor"),
            "families": families,
        },
        "cases": [
            {
                "id": case["id"],
                "fixture": case["fixture"],
                "families": case.get("families", []),
                "assertionKind": case["assertionKind"],
                "querySurfaceKind": "effective_settings",
                "effectiveSettingsGroups": case["effectiveSettingsGroups"],
                "summary": case["summary"],
            }
            for case in EFFECTIVE_SETTINGS_CASES
        ],
    }


def _query_effective_settings(node: dict[str, Any]) -> dict[str, Any]:
    effective_settings = node.get("effectiveSettings")
    if not isinstance(effective_settings, dict):
        raise EffectiveSettingsSuiteError(
            "effective_settings_unsurfaced",
            f"graph.query missing effectiveSettings: {compact_json(node)}",
        )
    return effective_settings


def _assert_groups_present(effective_settings: dict[str, Any], groups: list[str]) -> dict[str, Any]:
    group_values: dict[str, Any] = {}
    missing: list[str] = []
    for group in groups:
        if group not in effective_settings:
            missing.append(group)
            continue
        group_values[group] = effective_settings.get(group)
    if missing:
        raise EffectiveSettingsSuiteError(
            "effective_settings_group_missing",
            f"effectiveSettings missing groups {missing!r}: {compact_json(effective_settings)}",
        )
    return group_values


def _read_get_spline_truth(client: McpStdioClient, request_id: int, *, node_id: str) -> dict[str, Any]:
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "def get_prop(obj, names):\n"
            "    errors = []\n"
            "    for name in names:\n"
            "        try:\n"
            "            return obj.get_editor_property(name)\n"
            "        except Exception as exc:\n"
            "            errors.append(f'{name}: {exc}')\n"
            "    raise RuntimeError('; '.join(errors))\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "actor_selector = settings.get_editor_property('actor_selector')\n"
            "component_selector = settings.get_editor_property('component_selector')\n"
            "actor_selection_class = get_prop(actor_selector, ['actor_selection_class'])\n"
            "component_selection_class = get_prop(component_selector, ['component_selection_class'])\n"
            "print(json.dumps({\n"
            "    'ok': True,\n"
            "    'actorSelector': {\n"
            "        'actorFilter': str(get_prop(actor_selector, ['actor_filter'])).split('.')[-1],\n"
            "        'actorSelection': str(get_prop(actor_selector, ['actor_selection'])).split('.')[-1],\n"
            "        'actorSelectionClassPath': actor_selection_class.get_path_name() if actor_selection_class is not None else '',\n"
            "    },\n"
            "    'componentSelector': {\n"
            "        'componentSelection': str(get_prop(component_selector, ['component_selection'])).split('.')[-1],\n"
            "        'componentSelectionClassPath': component_selection_class.get_path_name() if component_selection_class is not None else '',\n"
            "    },\n"
            "    'dataFilter': 'PolyLine',\n"
            "}, ensure_ascii=False))\n"
        ),
    )
    truth = safe_parse_execute_json(payload)
    if truth.get("ok") is not True:
        raise EffectiveSettingsSuiteError(
            "engine_truth_gap",
            f"GetSpline execute readback failed: {compact_json(truth)}",
        )
    actor_selector_truth = truth.get("actorSelector")
    if isinstance(actor_selector_truth, dict):
        actor_selector_truth["actorFilter"] = _canonical_pascal_enum(actor_selector_truth.get("actorFilter"))
        actor_selector_truth["actorSelection"] = _canonical_pascal_enum(actor_selector_truth.get("actorSelection"))
    component_selector_truth = truth.get("componentSelector")
    if isinstance(component_selector_truth, dict):
        component_selector_truth["componentSelection"] = _canonical_pascal_enum(component_selector_truth.get("componentSelection"))
    return truth


def _setup_bare_node_case(
    client: McpStdioClient, request_id_base: int, *, asset_path: str, node_class_path: str
) -> tuple[str, dict[str, Any]]:
    node_id = add_node(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path=node_class_path,
    )
    return node_id, {}


def _setup_get_spline_case(client: McpStdioClient, request_id_base: int, *, asset_path: str) -> tuple[str, dict[str, Any]]:
    node_id = add_node(
        client,
        request_id_base + 1,
        asset_path=asset_path,
        node_class_path="/Script/PCG.PCGGetSplineSettings",
    )
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base + 10,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "all_world_actors = getattr(unreal.PCGActorFilter, 'ALL_WORLD_ACTORS', None)\n"
            "by_class = getattr(unreal.PCGActorSelection, 'BY_CLASS', None)\n"
            "component_by_class = getattr(unreal.PCGComponentSelection, 'BY_CLASS', None)\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "def set_prop(obj, names, value):\n"
            "    errors = []\n"
            "    for name in names:\n"
            "        try:\n"
            "            obj.set_editor_property(name, value)\n"
            "            return\n"
            "        except Exception as exc:\n"
            "            errors.append(f'{name}: {exc}')\n"
            "    raise RuntimeError('; '.join(errors))\n"
            "actor_selector = settings.get_editor_property('actor_selector')\n"
            "if all_world_actors is not None:\n"
            "    set_prop(actor_selector, ['actor_filter'], all_world_actors)\n"
            "if by_class is not None:\n"
            "    set_prop(actor_selector, ['actor_selection'], by_class)\n"
            "    set_prop(actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
            "set_prop(settings, ['actor_selector'], actor_selector)\n"
            "component_selector = settings.get_editor_property('component_selector')\n"
            "if component_by_class is not None:\n"
            "    set_prop(component_selector, ['component_selection'], component_by_class)\n"
            "    set_prop(component_selector, ['component_selection_class'], unreal.SplineComponent.static_class())\n"
            "set_prop(settings, ['component_selector'], component_selector)\n"
            "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
        ),
    )
    result = safe_parse_execute_json(payload)
    if result.get("ok") is not True:
        raise EffectiveSettingsSuiteError("mutate_gap", f"GetSpline setup failed: {compact_json(result)}")
    return node_id, {}


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
            ("bSelectComponent", True),
            ("ComponentClass", "/Script/Engine.SplineComponent"),
            ("bProcessAllComponents", True),
        ],
        start=2,
    ):
        set_pin_default(client, request_id_base + index, asset_path=asset_path, node_id=node_id, pin=pin, value=value)
    payload = call_execute_exec_with_retry(
        client=client,
        req_id_base=request_id_base + 10,
        code=(
            "import json\n"
            "import unreal\n"
            f"node_path={json.dumps(node_id, ensure_ascii=False)}\n"
            "all_world_actors = getattr(unreal.PCGActorFilter, 'ALL_WORLD_ACTORS', None)\n"
            "by_class = getattr(unreal.PCGActorSelection, 'BY_CLASS', None)\n"
            "node = unreal.load_object(None, node_path)\n"
            "if node is None:\n"
            "    raise RuntimeError(f'failed to load node: {node_path}')\n"
            "settings = node.get_settings()\n"
            "if settings is None:\n"
            "    raise RuntimeError(f'node has no settings: {node_path}')\n"
            "def set_prop(obj, names, value):\n"
            "    errors = []\n"
            "    for name in names:\n"
            "        try:\n"
            "            obj.set_editor_property(name, value)\n"
            "            return\n"
            "        except Exception as exc:\n"
            "            errors.append(f'{name}: {exc}')\n"
            "    raise RuntimeError('; '.join(errors))\n"
            "actor_selector = settings.get_editor_property('actor_selector')\n"
            "if all_world_actors is not None:\n"
            "    set_prop(actor_selector, ['actor_filter'], all_world_actors)\n"
            "if by_class is not None:\n"
            "    set_prop(actor_selector, ['actor_selection'], by_class)\n"
            "    set_prop(actor_selector, ['actor_selection_class'], unreal.Actor.static_class())\n"
            "set_prop(actor_selector, ['b_select_multiple', 'select_multiple'], True)\n"
            "set_prop(settings, ['actor_selector'], actor_selector)\n"
            "set_prop(settings, ['property_name'], unreal.Name('Tags'))\n"
            "set_prop(settings, ['b_select_component', 'select_component'], True)\n"
            "set_prop(settings, ['component_class'], unreal.SplineComponent.static_class())\n"
            "set_prop(settings, ['b_process_all_components', 'process_all_components'], True)\n"
            "print(json.dumps({'ok': True}, ensure_ascii=False))\n"
        ),
    )
    result = safe_parse_execute_json(payload)
    if result.get("ok") is not True:
        raise EffectiveSettingsSuiteError("mutate_gap", f"GetActorProperty setup failed: {compact_json(result)}")
    return node_id, {}


def _setup_static_mesh_spawner_case(
    client: McpStdioClient, request_id_base: int, *, asset_path: str
) -> tuple[str, dict[str, Any]]:
    from run_pcg_selector_truth_suite import _setup_static_mesh_spawner_case as selector_setup

    return selector_setup(client, request_id_base, asset_path=asset_path)


def _run_case(
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
        "assertionKind": case["assertionKind"],
        "querySurfaceKind": "effective_settings",
        "effectiveSettingsGroups": case["effectiveSettingsGroups"],
        "status": "fail",
    }
    surface_matrix = blank_surface_matrix()
    asset_path = f"/Game/Codex/PCGEffectiveSettings/{case['id']}_{case_index}"
    actor_path: str | None = None
    try:
        fixture_info = create_pcg_fixture(
            client,
            request_id_base,
            asset_path=asset_path,
            fixture_id=case["fixture"],
            actor_offset=float(case_index * 175),
        )
        actor_path = fixture_info.get("actorPath") if isinstance(fixture_info.get("actorPath"), str) else None

        if case["setupKind"] == "bare_node":
            node_id, setup_details = _setup_bare_node_case(
                client, request_id_base + 10, asset_path=asset_path, node_class_path=case["nodeClassPath"]
            )
        elif case["setupKind"] == "get_actor_property":
            node_id, setup_details = _setup_get_actor_property_case(client, request_id_base + 10, asset_path=asset_path)
        elif case["setupKind"] == "get_spline":
            node_id, setup_details = _setup_get_spline_case(client, request_id_base + 10, asset_path=asset_path)
        elif case["setupKind"] == "static_mesh_spawner":
            node_id, setup_details = _setup_static_mesh_spawner_case(client, request_id_base + 10, asset_path=asset_path)
        else:
            raise EffectiveSettingsSuiteError("runner_error", f"unsupported setup kind {case['setupKind']}")

        surface_matrix["mutate"] = "pass"
        snapshot = query_pcg_snapshot(client, request_id_base + 100, asset_path)
        node = find_node(snapshot, node_id)
        if not isinstance(node, dict):
            raise EffectiveSettingsSuiteError("query_structure_gap", f"graph.query missing node {node_id}")
        surface_matrix["queryStructure"] = "pass"

        verify_details = verify_graph(client, request_id_base + 110, asset_path)
        surface_matrix["verify"] = "pass"
        surface_matrix["diagnostics"] = "pass"

        effective_settings = _query_effective_settings(node)
        grouped_query = _assert_groups_present(effective_settings, case["effectiveSettingsGroups"])

        query_details: dict[str, Any] = {"effectiveSettings": grouped_query}
        engine_truth: dict[str, Any] | None = None
        if case["assertionKind"] == "truth":
            _assert_expected_subset(effective_settings, case["expectedQuery"], kind="query_truth_mismatch", prefix="effectiveSettings")
            surface_matrix["queryTruth"] = "pass"
            if case["setupKind"] == "get_actor_property":
                engine_truth = _read_get_actor_property_selector_truth(client, request_id_base + 120, node_id=node_id)
            elif case["setupKind"] == "get_spline":
                engine_truth = _read_get_spline_truth(client, request_id_base + 120, node_id=node_id)
            elif case["setupKind"] == "static_mesh_spawner":
                engine_truth = _read_static_mesh_spawner_selector_truth(client, request_id_base + 120, node_id=node_id)
            else:
                raise EffectiveSettingsSuiteError("runner_error", f"truth case missing engine reader for {case['id']}")
            _assert_expected_subset(engine_truth, case["expectedEngine"], kind="engine_truth_mismatch", prefix="engineTruth")
            surface_matrix["engineTruth"] = "pass"
        else:
            surface_matrix["queryTruth"] = "pass"

        result["status"] = "pass"
        result["details"] = {
            "surfaceMatrix": surface_matrix,
            "setup": setup_details,
            "query": query_details,
            "engineTruth": engine_truth,
            "verify": verify_details,
        }
        return result
    except EffectiveSettingsSuiteError as exc:
        if exc.kind in {"effective_settings_unsurfaced", "effective_settings_group_missing"}:
            surface_matrix["queryTruth"] = "fail"
            if surface_matrix["queryStructure"] == "not_run":
                surface_matrix["queryStructure"] = "pass"
        elif exc.kind == "query_structure_gap":
            surface_matrix["queryStructure"] = "fail"
        elif exc.kind == "query_truth_mismatch":
            surface_matrix["queryTruth"] = "fail"
        elif exc.kind == "engine_truth_mismatch":
            surface_matrix["engineTruth"] = "fail"
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
            result = _run_case(
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
            "assertionKind": case["assertionKind"],
            "querySurfaceKind": "effective_settings",
            "effectiveSettingsGroups": case["effectiveSettingsGroups"],
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
    assertion_kinds = Counter(
        result["assertionKind"]
        for result in results
        if isinstance(result.get("assertionKind"), str)
    )
    family_rows: dict[str, dict[str, Any]] = {}
    surface_totals: dict[str, Counter[str]] = {
        surface: Counter()
        for surface in ("mutate", "queryStructure", "queryTruth", "engineTruth", "verify", "diagnostics")
    }
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
        "assertionKinds": dict(sorted(assertion_kinds.items())),
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
    parser = argparse.ArgumentParser(description="Run PCG effective-settings regressions against the current LOOMLE bridge.")
    parser.add_argument("--project-root", default="", help="UE project root containing the host .uproject")
    parser.add_argument("--dev-config", default="", help="Optional dev config path for project_root lookup")
    parser.add_argument("--loomle-bin", default="", help="Optional override path to the project-local loomle client")
    parser.add_argument("--timeout", type=float, default=45.0, help="Per-request timeout in seconds")
    parser.add_argument("--output", default="", help="Optional path to write a JSON execution report")
    parser.add_argument("--list-cases", action="store_true", help="Print the effective-settings case registry and exit")
    parser.add_argument("--max-cases", type=int, default=0, help="Optional limit for debugging")
    args = parser.parse_args()

    if args.list_cases:
        print(json.dumps(list_cases_payload(), indent=2, ensure_ascii=False))
        return 0

    project_root = resolve_project_root(args.project_root, args.dev_config)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else resolve_default_loomle_binary(project_root)
    cases = EFFECTIVE_SETTINGS_CASES[: args.max_cases] if args.max_cases > 0 else EFFECTIVE_SETTINGS_CASES

    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, start=1):
        result = execute_case_with_fresh_client(
            project_root=project_root,
            loomle_binary=loomle_binary,
            timeout_s=args.timeout,
            request_id_base=97000 + index * 1000,
            case_index=index,
            case=case,
        )
        results.append(result)
        print(f"[{result['status'].upper()}] {case['id']}")

    report = {
        "version": "1",
        "graphType": "pcg",
        "suite": "effective_settings",
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
