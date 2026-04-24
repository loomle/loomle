#!/usr/bin/env python3
import argparse
import json
import queue
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
TEST_TOOLS_DIR = REPO_ROOT / "tests" / "tools"

REQUIRED_TOOLS = {
    "loomle",
    "blueprint.asset.inspect",
    "blueprint.asset.edit",
    "blueprint.member.inspect",
    "blueprint.member.edit",
    "blueprint.graph.list",
    "blueprint.graph.inspect",
    "blueprint.graph.edit",
    "blueprint.graph.refactor",
    "blueprint.graph.generate",
    "blueprint.graph.recipe.list",
    "blueprint.graph.recipe.inspect",
    "blueprint.graph.recipe.validate",
    "blueprint.compile",
    "blueprint.validate",
    "material.list",
    "material.query",
    "material.mutate",
    "material.verify",
    "material.describe",
    "pcg.list",
    "pcg.query",
    "pcg.mutate",
    "pcg.verify",
    "pcg.describe",
    "diag.tail",
    "context",
    "jobs",
    "editor.open",
    "editor.focus",
    "editor.screenshot",
    "execute",
    "widget.query",
    "widget.mutate",
    "widget.verify",
}

# Kept temporarily so regression imports keep loading while that suite is migrated
# away from the removed graph.* tool family.
EXPECTED_GRAPH_MUTATE_OPS = {
    "addNode.byClass",
    "connectPins",
    "disconnectPins",
    "breakPinLinks",
    "setPinDefault",
    "removeNode",
    "moveNode",
    "moveNodeBy",
    "moveNodes",
    "layoutGraph",
    "compile",
}

EXPECTED_WORKSPACE_EXAMPLES = {
    "blueprint/examples/executable/branch-local-subgraph.json",
    "blueprint/examples/executable/delay-local-chain.json",
    "blueprint/examples/executable/sequence-local-fanout.json",
    "blueprint/examples/illustrative/branch-then-layout.json",
    "blueprint/examples/illustrative/delay-then-print.json",
    "blueprint/examples/illustrative/do-once-then-print.json",
    "blueprint/examples/illustrative/replace-delay-with-do-once.json",
    "blueprint/examples/illustrative/replace-branch-with-sequence.json",
    "blueprint/examples/illustrative/insert-not-before-branch-condition.json",
    "blueprint/examples/illustrative/insert-delay-on-true-branch.json",
    "blueprint/examples/illustrative/set-variable-then-print.json",
    "blueprint/examples/illustrative/sequence-fanout.json",
    "material/examples/root-sink-then-layout.json",
    "material/examples/insert-multiply-before-base-color-root.json",
    "material/examples/insert-one-minus-before-multiply-b.json",
    "material/examples/replace-saturate-with-one-minus.json",
    "material/examples/replace-multiply-with-lerp.json",
    "material/examples/scalar-one-minus-to-roughness.json",
    "material/examples/texture-sample-to-base-color.json",
    "material/examples/texture-times-scalar-to-base-color.json",
    "material/examples/scalar-to-roughness.json",
    "pcg/examples/pipeline-then-layout.json",
    "pcg/examples/actor-data-tag-route.json",
    "pcg/examples/surface-sample-to-static-mesh.json",
    "pcg/examples/attribute-filter-elements.json",
    "pcg/examples/points-ratio-to-tag.json",
    "pcg/examples/project-surface-from-actor-data.json",
    "pcg/examples/replace-tag-with-points-ratio.json",
    "pcg/examples/replace-tag-route-with-attribute-route.json",
    "pcg/examples/insert-density-filter-before-static-mesh.json",
    "pcg/examples/insert-points-ratio-on-inside-filter.json",
}

EXPECTED_WORKSPACE_CATALOGS = {
    "blueprint/catalogs/node-database.json",
    "blueprint/catalogs/node-index.json",
    "material/catalogs/node-database.json",
    "material/catalogs/node-index.json",
    "pcg/catalogs/node-database.json",
    "pcg/catalogs/node-index.json",
}

EXPECTED_PCG_PLAN_SUMMARY = {
    "totalNodes": 178,
    "readyAutoCases": 158,
    "readyRecipeCases": 9,
    "workflowOnly": 6,
    "inventoryOnly": 1,
    "blocked": 4,
}

EXPECTED_BLUEPRINT_PLAN_SUMMARY = {
    "totalNodes": 101,
    "readyAutoCases": 83,
    "readyRecipeCases": 11,
    "workflowOnly": 1,
    "inventoryOnly": 5,
    "blocked": 1,
}

EXPECTED_BLUEPRINT_COVERAGE_SUMMARY = {
    "totalNodes": 101,
    "readyNodes": 94,
    "blockedNodes": 1,
    "workflowOnlyNodes": 1,
    "inventoryOnlyNodes": 5,
    "coverageDimensions": {
        "construct": 94,
        "inventory": 101,
        "query_structure": 83,
        "recipe_context": 11,
        "workflow": 1,
    },
}

EXPECTED_BLUEPRINT_WORKFLOW_SUITE_SUMMARY = {
    "totalCases": 5,
    "families": ["branch", "function_call", "struct", "utility"],
    "exampleBackedCases": 5,
}

EXPECTED_BLUEPRINT_NEGATIVE_SUITE_SUMMARY = {
    "totalCases": 4,
    "operations": ["addNode.byClass", "batch_partial_apply", "setPinDefault"],
}

EXPECTED_BLUEPRINT_STABILITY_SUITE_SUMMARY = {
    "totalCases": 3,
    "freshSessionCases": 1,
    "families": ["branch", "function_call", "struct", "utility"],
}

EXPECTED_BLUEPRINT_RESIDUAL_GAP_SUITE_SUMMARY = {
    "totalCases": 0,
    "documentedCases": 0,
    "missingFallback": 0,
    "missingReason": 0,
    "fallbackKinds": [],
}

EXPECTED_BLUEPRINT_EMBEDDED_TEMPLATE_SUITE_SUMMARY = {
    "totalCases": 2,
    "presenceShapeCases": 2,
    "families": ["utility"],
    "querySurfaceKinds": ["embedded_template"],
    "recipes": ["blueprint_component_template_context", "blueprint_timeline_graph"],
}

EXPECTED_MATERIAL_PLAN_SUMMARY = {
    "totalNodes": 317,
    "readyAutoCases": 316,
    "readyRecipeCases": 1,
    "workflowOnly": 0,
    "inventoryOnly": 0,
    "blocked": 0,
}

EXPECTED_MATERIAL_COVERAGE_SUMMARY = {
    "totalNodes": 317,
    "readyNodes": 317,
    "blockedNodes": 0,
    "workflowOnlyNodes": 0,
    "inventoryOnlyNodes": 0,
    "coverageDimensions": {
        "construct": 317,
        "engine_truth": 9,
        "inventory": 317,
        "query_structure": 300,
        "recipe_context": 1,
    },
}

EXPECTED_MATERIAL_WORKFLOW_SUITE_SUMMARY = {
    "totalCases": 5,
    "exampleBackedCases": 5,
    "families": ["expression", "parameter", "texture"],
}

EXPECTED_MATERIAL_NEGATIVE_SUITE_SUMMARY = {
    "totalCases": 5,
    "operations": ["addNode.byClass", "connectPins", "disconnectPins", "setPinDefault"],
}

EXPECTED_MATERIAL_STABILITY_SUITE_SUMMARY = {
    "totalCases": 3,
    "freshSessionCases": 1,
    "families": ["expression", "parameter", "texture"],
}

EXPECTED_MATERIAL_CHILD_GRAPH_REF_SUITE_SUMMARY = {
    "totalCases": 1,
    "families": ["expression"],
    "querySurfaceKinds": ["child_graph_ref"],
}

EXPECTED_PCG_COVERAGE_SUMMARY = {
    "totalNodes": 178,
    "readyNodes": 167,
    "blockedNodes": 4,
    "workflowOnlyNodes": 6,
    "inventoryOnlyNodes": 1,
    "coverageDimensions": {
        "construct": 167,
        "dynamic_shape": 2,
        "engine_truth": 100,
        "inventory": 178,
        "query_structure": 67,
        "recipe_context": 9,
        "workflow": 6,
    },
}

EXPECTED_PCG_WORKFLOW_SUITE_SUMMARY = {
    "totalCases": 10,
    "worldContextCases": 3,
    "families": ["branch", "create", "filter", "meta", "predicate", "route", "sample", "select", "source", "spawn", "struct", "transform"],
}

EXPECTED_PCG_NEGATIVE_SUITE_SUMMARY = {
    "totalCases": 8,
    "operations": ["connectPins", "disconnectPins", "removeNode", "setPinDefault"],
}

EXPECTED_PCG_STABILITY_SUITE_SUMMARY = {
    "totalCases": 3,
    "freshSessionCases": 1,
    "families": ["create", "meta", "route", "sample", "source", "spawn", "transform"],
}

EXPECTED_PCG_SELECTOR_SUITE_SUMMARY = {
    "totalCases": 4,
    "worldContextCases": 1,
    "selectorFields": ["ActorSelector", "MeshSelectorParameters", "OutputAttributeName", "TargetAttribute"],
    "querySurfaceKinds": ["effective_settings", "pin_default"],
}

EXPECTED_PCG_EFFECTIVE_SETTINGS_SUITE_SUMMARY = {
    "totalCases": 9,
    "truthCases": 3,
    "presenceShapeCases": 6,
    "worldContextCases": 4,
    "families": ["meta", "source", "spawn"],
}

EXPECTED_PCG_CHILD_GRAPH_REF_SUITE_SUMMARY = {
    "totalCases": 2,
    "families": ["struct"],
    "querySurfaceKinds": ["child_graph_ref"],
}

EXPECTED_PCG_RESIDUAL_GAP_SUITE_SUMMARY = {
    "totalCases": 0,
    "documentedCases": 0,
    "missingFallback": 0,
    "missingReason": 0,
    "fallbackKinds": [],
}


def _require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def _load_json_file(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"failed to parse JSON example {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"example root must be an object: {path}")
    return value


def _find_ops(payload: dict[str, Any], op_name: str) -> list[dict[str, Any]]:
    ops = payload.get("ops")
    if not isinstance(ops, list):
        fail(f"example missing ops[]: {payload}")
    return [op for op in ops if isinstance(op, dict) and op.get("op") == op_name]


def _extract_endpoint_pin(endpoint: dict[str, Any]) -> Any:
    if "pinName" in endpoint:
        return endpoint.get("pinName")
    if "pin" in endpoint:
        return endpoint.get("pin")
    return ""


def _has_connection(payload: dict[str, Any], from_node: str, from_pin: str, to_node: str, to_pin: str) -> bool:
    for op in _find_ops(payload, "connectPins"):
        args = op.get("args")
        if not isinstance(args, dict):
            continue
        source = args.get("from")
        target = args.get("to")
        if not isinstance(source, dict) or not isinstance(target, dict):
            continue
        source_node = source.get("nodeRef") or source.get("nodeId")
        target_node = target.get("nodeRef") or target.get("nodeId")
        source_pin = _extract_endpoint_pin(source)
        target_pin = _extract_endpoint_pin(target)
        if source_node == from_node and source_pin == from_pin and target_node == to_node and target_pin == to_pin:
            return True
    return False


def _has_add_node(payload: dict[str, Any], *, client_ref: str, class_path: str) -> bool:
    for op in _find_ops(payload, "addNode.byClass"):
        args = op.get("args")
        if not isinstance(args, dict):
            continue
        if op.get("clientRef") == client_ref and args.get("nodeClassPath") == class_path:
            return True
    return False


def _has_set_default(payload: dict[str, Any], *, node_ref: str, pin: str, value: Any) -> bool:
    for op in _find_ops(payload, "setPinDefault"):
        args = op.get("args")
        if not isinstance(args, dict):
            continue
        target = args.get("target")
        if not isinstance(target, dict):
            continue
        target_node = target.get("nodeRef") or target.get("nodeId")
        target_pin = _extract_endpoint_pin(target)
        if target_node == node_ref and target_pin == pin and args.get("value") == value:
            return True
    return False


def _has_remove_node(payload: dict[str, Any], *, node_ref: str) -> bool:
    for op in _find_ops(payload, "removeNode"):
        args = op.get("args")
        if not isinstance(args, dict):
            continue
        target = args.get("target")
        if isinstance(target, dict) and target.get("nodeRef") == node_ref:
            return True
    return False


def _payload_uses_node_id(payload: dict[str, Any]) -> bool:
    ops = payload.get("ops")
    if not isinstance(ops, list):
        return False

    def _walk(value: Any) -> bool:
        if isinstance(value, dict):
            if "nodeId" in value:
                return True
            return any(_walk(v) for v in value.values())
        if isinstance(value, list):
            return any(_walk(v) for v in value)
        return False

    return any(_walk(op) for op in ops if isinstance(op, dict))


def validate_workspace_examples() -> None:
    workspace_root = REPO_ROOT / "workspace" / "Loomle"
    example_paths = sorted(workspace_root.glob("*/examples/**/*.json"))
    actual_relpaths = {str(path.relative_to(workspace_root)).replace("\\", "/") for path in example_paths}
    _require(
        actual_relpaths == EXPECTED_WORKSPACE_EXAMPLES,
        f"workspace example set mismatch expected={sorted(EXPECTED_WORKSPACE_EXAMPLES)} actual={sorted(actual_relpaths)}",
    )

    for path in example_paths:
        payload = _load_json_file(path)
        relpath = str(path.relative_to(workspace_root)).replace("\\", "/")
        graph_dir = path.relative_to(workspace_root).parts[0]
        _require(payload.get("tool") == "graph.mutate", f"example must target graph.mutate: {relpath}")
        _require(payload.get("graphType") == graph_dir, f"example graphType mismatch for {relpath}: {payload}")
        ops = payload.get("ops")
        _require(isinstance(ops, list) and ops, f"example must contain ops[]: {relpath}")
        _require(
            all(isinstance(op, dict) and isinstance(op.get("op"), str) and op.get("op") != "runScript" for op in ops),
            f"example contains unsupported mutate op: {relpath}",
        )
        _require(
            any(isinstance(op, dict) and op.get("op") == "layoutGraph" for op in ops),
            f"example should end in a touched layout pass: {relpath}",
        )
        if relpath.startswith("blueprint/examples/executable/"):
            _require(
                not _payload_uses_node_id(payload),
                f"blueprint executable example should not depend on live nodeId addressing: {relpath}",
            )
        if relpath.startswith("blueprint/examples/illustrative/"):
            _require(
                _payload_uses_node_id(payload),
                f"blueprint illustrative example should visibly require live graph substitution: {relpath}",
            )

        if relpath == "blueprint/examples/executable/branch-local-subgraph.json":
            _require(
                _has_add_node(payload, client_ref="branch_main", class_path="/Script/BlueprintGraph.K2Node_IfThenElse"),
                f"blueprint executable branch example missing branch node: {relpath}",
            )
            _require(
                _has_connection(payload, "branch_main", "Then", "true_print", "execute"),
                f"blueprint executable branch example missing Then connection: {relpath}",
            )
            _require(
                _has_connection(payload, "branch_main", "Else", "false_print", "execute"),
                f"blueprint executable branch example missing Else connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="branch_main", pin="Condition", value=True),
                f"blueprint executable branch example missing Condition=true default: {relpath}",
            )
        elif relpath == "blueprint/examples/executable/delay-local-chain.json":
            _require(
                _has_connection(payload, "delay_main", "then", "print_after_delay", "execute"),
                f"blueprint executable delay example missing delay -> print connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="delay_main", pin="Duration", value=0.2),
                f"blueprint executable delay example missing Duration default: {relpath}",
            )
        elif relpath == "blueprint/examples/executable/sequence-local-fanout.json":
            _require(
                _has_connection(payload, "sequence_main", "Then_0", "print_first", "execute"),
                f"blueprint executable sequence example missing Then_0 branch: {relpath}",
            )
            _require(
                _has_connection(payload, "sequence_main", "Then_1", "print_second", "execute"),
                f"blueprint executable sequence example missing Then_1 branch: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/branch-then-layout.json":
            _require(
                _has_add_node(payload, client_ref="branch_a", class_path="/Script/BlueprintGraph.K2Node_IfThenElse"),
                f"branch example missing branch node: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "branch_a", "Execute"),
                f"branch example missing EventBeginPlay -> Branch connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="branch_a", pin="Condition", value=True),
                f"branch example missing Condition=true default: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/set-variable-then-print.json":
            _require(
                _has_add_node(payload, client_ref="set_hidden", class_path="/Script/BlueprintGraph.K2Node_VariableSet"),
                f"set-variable example missing variable set node: {relpath}",
            )
            _require(
                _has_add_node(payload, client_ref="print_result", class_path="/Script/BlueprintGraph.K2Node_CallFunction"),
                f"set-variable example missing PrintString node: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "set_hidden", "execute"),
                f"set-variable example missing BeginPlay -> set connection: {relpath}",
            )
            _require(
                _has_connection(payload, "set_hidden", "then", "print_result", "execute"),
                f"set-variable example missing set -> print connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="set_hidden", pin="ActorHiddenInGame", value=True),
                f"set-variable example missing ActorHiddenInGame=true default: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="print_result", pin="InString", value="ActorHiddenInGame set to true"),
                f"set-variable example missing print string default: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/sequence-fanout.json":
            _require(
                _has_add_node(payload, client_ref="sequence_main", class_path="/Script/BlueprintGraph.K2Node_ExecutionSequence"),
                f"sequence example missing sequence node: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "sequence_main", "execute"),
                f"sequence example missing BeginPlay -> sequence connection: {relpath}",
            )
            _require(
                _has_connection(payload, "sequence_main", "Then_0", "print_first", "execute"),
                f"sequence example missing Then_0 branch: {relpath}",
            )
            _require(
                _has_connection(payload, "sequence_main", "Then_1", "print_second", "execute"),
                f"sequence example missing Then_1 branch: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/delay-then-print.json":
            _require(
                _has_add_node(payload, client_ref="delay_main", class_path="/Script/BlueprintGraph.K2Node_CallFunction"),
                f"delay example missing delay node: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "delay_main", "execute"),
                f"delay example missing BeginPlay -> delay connection: {relpath}",
            )
            _require(
                _has_connection(payload, "delay_main", "then", "print_after_delay", "execute"),
                f"delay example missing delay -> print connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="delay_main", pin="Duration", value=0.2),
                f"delay example missing Duration default: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/do-once-then-print.json":
            _require(
                _has_add_node(payload, client_ref="do_once_main", class_path="/Script/BlueprintGraph.K2Node_MacroInstance"),
                f"do-once example missing DoOnce node: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "do_once_main", "Execute"),
                f"do-once example missing BeginPlay -> DoOnce connection: {relpath}",
            )
            _require(
                _has_connection(payload, "do_once_main", "Completed", "print_completed", "execute"),
                f"do-once example missing Completed -> print connection: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/replace-delay-with-do-once.json":
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "old_delay", "execute"),
                f"blueprint replacement example missing initial BeginPlay -> delay connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_delay", "then", "terminal_print", "execute"),
                f"blueprint replacement example missing initial delay -> print connection: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "replacement_gate", "Execute"),
                f"blueprint replacement example missing preserved upstream -> replacement connection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_gate", "Completed", "terminal_print", "execute"),
                f"blueprint replacement example missing replacement -> preserved downstream connection: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_delay"),
                f"blueprint replacement example missing removeNode for old delay: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/replace-branch-with-sequence.json":
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "old_branch", "Execute"),
                f"blueprint multi-branch replacement missing initial BeginPlay -> Branch connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_branch", "Then", "true_print", "execute"),
                f"blueprint multi-branch replacement missing old Branch Then connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_branch", "Else", "false_print", "execute"),
                f"blueprint multi-branch replacement missing old Branch Else connection: {relpath}",
            )
            _require(
                _has_connection(payload, "EventBeginPlay", "Then", "replacement_sequence", "execute"),
                f"blueprint multi-branch replacement missing preserved upstream -> Sequence connection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_sequence", "Then_0", "true_print", "execute"),
                f"blueprint multi-branch replacement missing Sequence Then_0 reconnection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_sequence", "Then_1", "false_print", "execute"),
                f"blueprint multi-branch replacement missing Sequence Then_1 reconnection: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_branch"),
                f"blueprint multi-branch replacement missing removeNode for old branch: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/insert-not-before-branch-condition.json":
            _require(
                _has_connection(payload, "condition_get", "ActorHiddenInGame", "branch_main", "Condition"),
                f"blueprint data-rewrite example missing initial get -> branch condition connection: {relpath}",
            )
            _require(
                _has_connection(payload, "condition_get", "ActorHiddenInGame", "invert_condition", "A"),
                f"blueprint data-rewrite example missing get -> Not input connection: {relpath}",
            )
            _require(
                _has_connection(payload, "invert_condition", "ReturnValue", "branch_main", "Condition"),
                f"blueprint data-rewrite example missing Not output -> branch condition connection: {relpath}",
            )
        elif relpath == "blueprint/examples/illustrative/insert-delay-on-true-branch.json":
            _require(
                _has_connection(payload, "branch_main", "Then", "true_print", "execute"),
                f"blueprint branch insertion example missing initial Then -> true print connection: {relpath}",
            )
            _require(
                _has_connection(payload, "branch_main", "Else", "false_print", "execute"),
                f"blueprint branch insertion example missing preserved Else branch connection: {relpath}",
            )
            _require(
                _has_connection(payload, "branch_main", "Then", "delay_true_branch", "execute"),
                f"blueprint branch insertion example missing Then -> Delay connection: {relpath}",
            )
            _require(
                _has_connection(payload, "delay_true_branch", "then", "true_print", "execute"),
                f"blueprint branch insertion example missing Delay -> true print connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="delay_true_branch", pin="Duration", value=0.15),
                f"blueprint branch insertion example missing Delay duration default: {relpath}",
            )
        elif relpath == "material/examples/root-sink-then-layout.json":
            _require(
                _has_connection(payload, "multiply_ab", "", "__material_root__", "Base Color"),
                f"material root example missing root sink connection: {relpath}",
            )
        elif relpath == "material/examples/texture-sample-to-base-color.json":
            _require(
                _has_add_node(payload, client_ref="albedo_tex", class_path="/Script/Engine.MaterialExpressionTextureSampleParameter2D"),
                f"texture sample example missing texture parameter node: {relpath}",
            )
            _require(
                _has_connection(payload, "albedo_tex", "", "__material_root__", "Base Color"),
                f"texture sample example missing Base Color root sink: {relpath}",
            )
        elif relpath == "material/examples/scalar-to-roughness.json":
            _require(
                _has_add_node(payload, client_ref="roughness_scalar", class_path="/Script/Engine.MaterialExpressionScalarParameter"),
                f"scalar roughness example missing scalar parameter node: {relpath}",
            )
            _require(
                _has_connection(payload, "roughness_scalar", "", "__material_root__", "Roughness"),
                f"scalar roughness example missing Roughness root sink: {relpath}",
            )
        elif relpath == "material/examples/scalar-one-minus-to-roughness.json":
            _require(
                _has_add_node(payload, client_ref="one_minus_roughness", class_path="/Script/Engine.MaterialExpressionOneMinus"),
                f"one-minus example missing OneMinus node: {relpath}",
            )
            _require(
                _has_connection(payload, "roughness_scalar", "", "one_minus_roughness", "Input"),
                f"one-minus example missing scalar -> one-minus connection: {relpath}",
            )
            _require(
                _has_connection(payload, "one_minus_roughness", "", "__material_root__", "Roughness"),
                f"one-minus example missing Roughness root sink: {relpath}",
            )
        elif relpath == "material/examples/texture-times-scalar-to-base-color.json":
            _require(
                _has_add_node(payload, client_ref="multiply_tint", class_path="/Script/Engine.MaterialExpressionMultiply"),
                f"texture multiply example missing Multiply node: {relpath}",
            )
            _require(
                _has_connection(payload, "albedo_tex", "", "multiply_tint", "A"),
                f"texture multiply example missing texture -> A connection: {relpath}",
            )
            _require(
                _has_connection(payload, "tint_scalar", "", "multiply_tint", "B"),
                f"texture multiply example missing scalar -> B connection: {relpath}",
            )
            _require(
                _has_connection(payload, "multiply_tint", "", "__material_root__", "Base Color"),
                f"texture multiply example missing Base Color root sink: {relpath}",
            )
        elif relpath == "material/examples/replace-saturate-with-one-minus.json":
            _require(
                _has_connection(payload, "roughness_scalar", "", "old_saturate", "Input"),
                f"material replacement example missing initial scalar -> saturate connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_saturate", "", "__material_root__", "Roughness"),
                f"material replacement example missing initial saturate -> root connection: {relpath}",
            )
            _require(
                _has_connection(payload, "roughness_scalar", "", "replacement_invert", "Input"),
                f"material replacement example missing preserved upstream -> replacement connection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_invert", "", "__material_root__", "Roughness"),
                f"material replacement example missing replacement -> root connection: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_saturate"),
                f"material replacement example missing removeNode for old saturate: {relpath}",
            )
        elif relpath == "material/examples/replace-multiply-with-lerp.json":
            _require(
                _has_connection(payload, "color_a", "", "old_multiply", "A"),
                f"material multi-input replacement missing initial color_a -> Multiply.A connection: {relpath}",
            )
            _require(
                _has_connection(payload, "color_b", "", "old_multiply", "B"),
                f"material multi-input replacement missing initial color_b -> Multiply.B connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_multiply", "", "__material_root__", "Base Color"),
                f"material multi-input replacement missing initial Multiply -> root connection: {relpath}",
            )
            _require(
                _has_connection(payload, "color_a", "", "replacement_lerp", "A"),
                f"material multi-input replacement missing preserved color_a -> Lerp.A connection: {relpath}",
            )
            _require(
                _has_connection(payload, "color_b", "", "replacement_lerp", "B"),
                f"material multi-input replacement missing preserved color_b -> Lerp.B connection: {relpath}",
            )
            _require(
                _has_connection(payload, "alpha_control", "", "replacement_lerp", "Alpha"),
                f"material multi-input replacement missing Alpha connection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_lerp", "", "__material_root__", "Base Color"),
                f"material multi-input replacement missing Lerp -> root connection: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_multiply"),
                f"material multi-input replacement missing removeNode for old multiply: {relpath}",
            )
        elif relpath == "material/examples/insert-multiply-before-base-color-root.json":
            _require(
                _has_connection(payload, "albedo_tex", "", "__material_root__", "Base Color"),
                f"material insertion example missing initial texture -> root connection: {relpath}",
            )
            _require(
                _has_connection(payload, "albedo_tex", "", "multiply_tint", "A"),
                f"material insertion example missing preserved upstream -> multiply A connection: {relpath}",
            )
            _require(
                _has_connection(payload, "tint_scalar", "", "multiply_tint", "B"),
                f"material insertion example missing scalar -> multiply B connection: {relpath}",
            )
            _require(
                _has_connection(payload, "multiply_tint", "", "__material_root__", "Base Color"),
                f"material insertion example missing multiply -> root reconnection: {relpath}",
            )
        elif relpath == "material/examples/insert-one-minus-before-multiply-b.json":
            _require(
                _has_connection(payload, "albedo_tex", "", "multiply_tint", "A"),
                f"material leg insertion example missing preserved texture -> multiply.A connection: {relpath}",
            )
            _require(
                _has_connection(payload, "tint_scalar", "", "multiply_tint", "B"),
                f"material leg insertion example missing initial scalar -> multiply.B connection: {relpath}",
            )
            _require(
                _has_connection(payload, "tint_scalar", "", "invert_tint", "Input"),
                f"material leg insertion example missing scalar -> one-minus connection: {relpath}",
            )
            _require(
                _has_connection(payload, "invert_tint", "", "multiply_tint", "B"),
                f"material leg insertion example missing one-minus -> multiply.B connection: {relpath}",
            )
        elif relpath == "pcg/examples/pipeline-then-layout.json":
            _require(
                _has_connection(payload, "create_points", "Out", "add_tag", "In"),
                f"pcg pipeline example missing create -> tag connection: {relpath}",
            )
            _require(
                _has_connection(payload, "add_tag", "Out", "filter_tag", "In"),
                f"pcg pipeline example missing tag -> filter connection: {relpath}",
            )
        elif relpath == "pcg/examples/actor-data-tag-route.json":
            _require(
                _has_connection(payload, "actor_data", "Out", "filter_by_tag", "In"),
                f"pcg actor route example missing actor -> filter connection: {relpath}",
            )
            _require(
                _has_connection(payload, "filter_by_tag", "InsideFilter", "tag_matched_branch", "In"),
                f"pcg actor route example missing InsideFilter branch: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="filter_by_tag", pin="SelectedTags", value="Gameplay.Spawnable"),
                f"pcg actor route example missing SelectedTags default: {relpath}",
            )
        elif relpath == "pcg/examples/surface-sample-to-static-mesh.json":
            _require(
                _has_connection(payload, "actor_surface", "Out", "surface_sampler", "Surface"),
                f"pcg surface sampler example missing source -> Surface connection: {relpath}",
            )
            _require(
                _has_connection(payload, "surface_sampler", "Out", "static_mesh_spawner", "In"),
                f"pcg surface sampler example missing sampler -> spawner connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="surface_sampler", pin="PointsPerSquaredMeter", value=0.2),
                f"pcg surface sampler example missing density default: {relpath}",
            )
        elif relpath == "pcg/examples/attribute-filter-elements.json":
            _require(
                _has_add_node(payload, client_ref="filter_dense_points", class_path="/Script/PCG.PCGAttributeFilteringSettings"),
                f"pcg attribute filter example missing filter node: {relpath}",
            )
            _require(
                _has_connection(payload, "create_points", "Out", "filter_dense_points", "In"),
                f"pcg attribute filter example missing create -> filter connection: {relpath}",
            )
            _require(
                _has_connection(payload, "filter_dense_points", "InsideFilter", "tag_dense_points", "In"),
                f"pcg attribute filter example missing InsideFilter -> tag connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="filter_dense_points", pin="TargetAttribute", value="Density"),
                f"pcg attribute filter example missing TargetAttribute default: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="filter_dense_points", pin="AttributeTypes/double_value", value=0.5),
                f"pcg attribute filter example missing threshold default: {relpath}",
            )
        elif relpath == "pcg/examples/points-ratio-to-tag.json":
            _require(
                _has_connection(payload, "create_points", "Out", "sample_ratio", "In"),
                f"pcg points ratio example missing create -> sample connection: {relpath}",
            )
            _require(
                _has_connection(payload, "sample_ratio", "Out", "tag_sampled", "In"),
                f"pcg points ratio example missing sample -> tag connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="sample_ratio", pin="Ratio", value=0.1),
                f"pcg points ratio example missing ratio default: {relpath}",
            )
        elif relpath == "pcg/examples/project-surface-from-actor-data.json":
            _require(
                _has_connection(payload, "source_points", "Out", "project_surface", "In"),
                f"pcg project surface example missing source -> project connection: {relpath}",
            )
            _require(
                _has_connection(payload, "projection_target", "Out", "project_surface", "Projection Target"),
                f"pcg project surface example missing target -> projection target connection: {relpath}",
            )
            _require(
                _has_connection(payload, "project_surface", "Out", "tag_projected", "In"),
                f"pcg project surface example missing projection -> tag connection: {relpath}",
            )
        elif relpath == "pcg/examples/replace-tag-with-points-ratio.json":
            _require(
                _has_connection(payload, "create_points", "Out", "old_add_tag", "In"),
                f"pcg replacement example missing initial create -> tag connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_add_tag", "Out", "filter_tag", "In"),
                f"pcg replacement example missing initial tag -> filter connection: {relpath}",
            )
            _require(
                _has_connection(payload, "create_points", "Out", "sample_ratio", "In"),
                f"pcg replacement example missing preserved upstream -> replacement connection: {relpath}",
            )
            _require(
                _has_connection(payload, "sample_ratio", "Out", "filter_tag", "In"),
                f"pcg replacement example missing replacement -> preserved downstream connection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="sample_ratio", pin="Ratio", value=0.25),
                f"pcg replacement example missing ratio default: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_add_tag"),
                f"pcg replacement example missing removeNode for old tag stage: {relpath}",
            )
        elif relpath == "pcg/examples/replace-tag-route-with-attribute-route.json":
            _require(
                _has_connection(payload, "create_points", "Out", "old_filter_by_tag", "In"),
                f"pcg multi-route replacement missing initial create -> FilterByTag connection: {relpath}",
            )
            _require(
                _has_connection(payload, "old_filter_by_tag", "InsideFilter", "matched_branch", "In"),
                f"pcg multi-route replacement missing initial InsideFilter branch: {relpath}",
            )
            _require(
                _has_connection(payload, "old_filter_by_tag", "OutsideFilter", "unmatched_branch", "In"),
                f"pcg multi-route replacement missing initial OutsideFilter branch: {relpath}",
            )
            _require(
                _has_connection(payload, "create_points", "Out", "replacement_filter_by_attribute", "In"),
                f"pcg multi-route replacement missing preserved upstream -> replacement route connection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_filter_by_attribute", "InsideFilter", "matched_branch", "In"),
                f"pcg multi-route replacement missing replacement InsideFilter reconnection: {relpath}",
            )
            _require(
                _has_connection(payload, "replacement_filter_by_attribute", "OutsideFilter", "unmatched_branch", "In"),
                f"pcg multi-route replacement missing replacement OutsideFilter reconnection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="replacement_filter_by_attribute", pin="FilterMode", value="FilterByExistence"),
                f"pcg multi-route replacement missing FilterMode default: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="replacement_filter_by_attribute", pin="Attribute", value="Density"),
                f"pcg multi-route replacement missing Attribute default: {relpath}",
            )
            _require(
                _has_remove_node(payload, node_ref="old_filter_by_tag"),
                f"pcg multi-route replacement missing removeNode for old route node: {relpath}",
            )
        elif relpath == "pcg/examples/insert-density-filter-before-static-mesh.json":
            _require(
                _has_connection(payload, "create_points", "Out", "static_mesh_spawner", "In"),
                f"pcg insertion example missing initial create -> spawner connection: {relpath}",
            )
            _require(
                _has_connection(payload, "create_points", "Out", "density_filter", "In"),
                f"pcg insertion example missing preserved upstream -> density filter connection: {relpath}",
            )
            _require(
                _has_connection(payload, "density_filter", "Out", "static_mesh_spawner", "In"),
                f"pcg insertion example missing density filter -> spawner reconnection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="density_filter", pin="LowerBound", value=0.2),
                f"pcg insertion example missing density lower bound: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="density_filter", pin="UpperBound", value=0.8),
                f"pcg insertion example missing density upper bound: {relpath}",
            )
        elif relpath == "pcg/examples/insert-points-ratio-on-inside-filter.json":
            _require(
                _has_connection(payload, "filter_by_tag", "InsideFilter", "matched_branch", "In"),
                f"pcg branch-local insertion example missing initial InsideFilter -> matched branch connection: {relpath}",
            )
            _require(
                _has_connection(payload, "filter_by_tag", "OutsideFilter", "unmatched_branch", "In"),
                f"pcg branch-local insertion example missing preserved OutsideFilter branch: {relpath}",
            )
            _require(
                _has_connection(payload, "filter_by_tag", "InsideFilter", "sample_inside_branch", "In"),
                f"pcg branch-local insertion example missing InsideFilter -> sample insertion connection: {relpath}",
            )
            _require(
                _has_connection(payload, "sample_inside_branch", "Out", "matched_branch", "In"),
                f"pcg branch-local insertion example missing sample -> matched branch reconnection: {relpath}",
            )
            _require(
                _has_set_default(payload, node_ref="sample_inside_branch", pin="Ratio", value=0.2),
                f"pcg branch-local insertion example missing ratio default: {relpath}",
            )

    print("[PASS] workspace graph examples validated")


def validate_workspace_catalogs() -> None:
    workspace_root = REPO_ROOT / "workspace" / "Loomle"
    catalog_paths = sorted(workspace_root.glob("*/catalogs/*.json"))
    actual_relpaths = {str(path.relative_to(workspace_root)).replace("\\", "/") for path in catalog_paths}
    _require(
        actual_relpaths == EXPECTED_WORKSPACE_CATALOGS,
        f"workspace catalog set mismatch expected={sorted(EXPECTED_WORKSPACE_CATALOGS)} actual={sorted(actual_relpaths)}",
    )

    blueprint_index = _load_json_file(workspace_root / "blueprint" / "catalogs" / "node-index.json")
    _require(blueprint_index.get("graphType") == "blueprint", "blueprint index graphType mismatch")
    _require(blueprint_index.get("coverage") == "curated", "blueprint index coverage mismatch")
    blueprint_nodes = blueprint_index.get("nodes")
    _require(isinstance(blueprint_nodes, list) and blueprint_nodes, "blueprint index missing nodes[]")
    blueprint_names = {node.get("displayName") for node in blueprint_nodes if isinstance(node, dict)}
    _require(
        {"Branch", "Sequence", "Delay", "DoOnce", "Print String", "Variable Get", "Variable Set"}.issubset(blueprint_names),
        f"blueprint index missing expected nodes: {blueprint_names}",
    )
    for node in blueprint_nodes:
        _require(isinstance(node, dict), f"blueprint index node is not an object: {node}")
        _require(isinstance(node.get("displayName"), str) and node["displayName"], f"blueprint index node missing displayName: {node}")
        _require(
            isinstance(node.get("nodeClassPath"), str) and node["nodeClassPath"].startswith("/Script/"),
            f"blueprint index node missing nodeClassPath: {node}",
        )
        example_files = node.get("exampleFiles", [])
        _require(isinstance(example_files, list), f"blueprint index exampleFiles must be a list: {node}")
        for example_file in example_files:
            _require(
                isinstance(example_file, str) and (workspace_root / "blueprint" / example_file).exists(),
                f"blueprint index example reference missing: {example_file}",
            )

    blueprint_database = _load_json_file(workspace_root / "blueprint" / "catalogs" / "node-database.json")
    _require(blueprint_database.get("graphType") == "blueprint", "blueprint database graphType mismatch")
    _require(blueprint_database.get("coverage") == "source-derived", "blueprint database coverage mismatch")
    blueprint_database_nodes = blueprint_database.get("nodes")
    _require(
        isinstance(blueprint_database_nodes, list) and len(blueprint_database_nodes) >= 100,
        "blueprint database missing full source-derived node set",
    )
    _require(
        any(node.get("className") == "UK2Node_AddComponent" for node in blueprint_database_nodes if isinstance(node, dict)),
        "blueprint database missing UK2Node_AddComponent",
    )
    branch_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_IfThenElse"),
        None,
    )
    _require(isinstance(branch_node, dict), "blueprint database missing UK2Node_IfThenElse")
    _require(branch_node.get("family") == "branch", f"blueprint branch family mismatch: {branch_node}")
    branch_testing = branch_node.get("testing")
    _require(
        isinstance(branch_testing, dict)
        and branch_testing.get("profile") == "semantic_family_represented"
        and isinstance(branch_testing.get("focus"), dict)
        and branch_testing["focus"].get("workflowFamilies") == ["blueprint_local_control_flow"],
        f"blueprint branch testing mismatch: {branch_node}",
    )
    function_call_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_CallFunction"),
        None,
    )
    _require(isinstance(function_call_node, dict), "blueprint database missing UK2Node_CallFunction")
    function_call_testing = function_call_node.get("testing")
    _require(
        isinstance(function_call_testing, dict)
        and function_call_testing.get("profile") == "context_recipe_required"
        and function_call_testing.get("recipe") == "blueprint_function_call",
        f"blueprint function-call testing mismatch: {function_call_node}",
    )
    variable_get = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_VariableGet"),
        None,
    )
    _require(isinstance(variable_get, dict), "blueprint database missing UK2Node_VariableGet")
    variable_get_testing = variable_get.get("testing")
    _require(
        isinstance(variable_get_testing, dict)
        and variable_get_testing.get("profile") == "context_recipe_required"
        and variable_get_testing.get("recipe") == "blueprint_variable_access",
        f"blueprint variable-get testing mismatch: {variable_get}",
    )
    summary = blueprint_database.get("summary")
    _require(isinstance(summary, dict), "blueprint database missing summary")
    _require(
        summary.get("familyCounts") == {
            "branch": 1,
            "delegate": 8,
            "function_call": 1,
            "struct": 6,
            "utility": 71,
            "variable": 14,
        },
        f"blueprint database familyCounts mismatch: {summary}",
    )
    _require(
        summary.get("testingProfileCounts") == {
            "construct_and_query": 83,
            "context_recipe_required": 12,
            "inventory_only": 5,
            "semantic_family_represented": 1,
        },
        f"blueprint database testingProfileCounts mismatch: {summary}",
    )
    macro_instance_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_MacroInstance"),
        None,
    )
    _require(isinstance(macro_instance_node, dict), "blueprint database missing UK2Node_MacroInstance")
    macro_testing = macro_instance_node.get("testing")
    _require(
        isinstance(macro_testing, dict)
        and macro_testing.get("profile") == "context_recipe_required"
        and macro_testing.get("recipe") == "blueprint_actor_execution_graph"
        and isinstance(macro_testing.get("querySurface"), dict)
        and macro_testing["querySurface"].get("kind") == "graph_boundary_summary",
        f"blueprint macro-instance querySurface mismatch: {macro_instance_node}",
    )
    timeline_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_Timeline"),
        None,
    )
    _require(isinstance(timeline_node, dict), "blueprint database missing UK2Node_Timeline")
    timeline_testing = timeline_node.get("testing")
    _require(
        isinstance(timeline_testing, dict)
        and timeline_testing.get("profile") == "context_recipe_required"
        and timeline_testing.get("recipe") == "blueprint_timeline_graph"
        and isinstance(timeline_testing.get("querySurface"), dict)
        and timeline_testing["querySurface"].get("kind") == "embedded_template",
        f"blueprint timeline testing mismatch: {timeline_node}",
    )
    add_component_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_AddComponent"),
        None,
    )
    _require(isinstance(add_component_node, dict), "blueprint database missing UK2Node_AddComponent")
    add_component_testing = add_component_node.get("testing")
    _require(
        isinstance(add_component_testing, dict)
        and add_component_testing.get("profile") == "context_recipe_required"
        and add_component_testing.get("recipe") == "blueprint_component_template_context"
        and isinstance(add_component_testing.get("querySurface"), dict)
        and add_component_testing["querySurface"].get("kind") == "embedded_template",
        f"blueprint add-component testing mismatch: {add_component_node}",
    )
    add_component_by_class_node = next(
        (node for node in blueprint_database_nodes if isinstance(node, dict) and node.get("className") == "UK2Node_AddComponentByClass"),
        None,
    )
    _require(isinstance(add_component_by_class_node, dict), "blueprint database missing UK2Node_AddComponentByClass")
    add_component_by_class_testing = add_component_by_class_node.get("testing")
    _require(
        isinstance(add_component_by_class_testing, dict)
        and add_component_by_class_testing.get("profile") == "context_recipe_required"
        and add_component_by_class_testing.get("recipe") == "blueprint_actor_execution_graph"
        and isinstance(add_component_by_class_testing.get("querySurface"), dict)
        and add_component_by_class_testing["querySurface"].get("kind") == "context_sensitive_construct",
        f"blueprint add-component-by-class testing mismatch: {add_component_by_class_node}",
    )

    material_index = _load_json_file(workspace_root / "material" / "catalogs" / "node-index.json")
    _require(material_index.get("graphType") == "material", "material index graphType mismatch")
    _require(material_index.get("coverage") == "curated", "material index coverage mismatch")
    material_nodes = material_index.get("nodes")
    _require(isinstance(material_nodes, list) and material_nodes, "material index missing nodes[]")
    material_names = {node.get("displayName") for node in material_nodes if isinstance(node, dict)}
    _require(
        {"Scalar Parameter", "Texture Parameter 2D", "Multiply", "OneMinus", "Material Function Call"}.issubset(material_names),
        f"material index missing expected nodes: {material_names}",
    )
    special_nodes = material_index.get("specialNodes")
    _require(isinstance(special_nodes, list) and special_nodes, "material index missing specialNodes[]")
    root_node = next((node for node in special_nodes if isinstance(node, dict) and node.get("nodeId") == "__material_root__"), None)
    _require(isinstance(root_node, dict), "material index missing __material_root__")
    for node in material_nodes:
        _require(isinstance(node, dict), f"material index node is not an object: {node}")
        _require(isinstance(node.get("displayName"), str) and node["displayName"], f"material index node missing displayName: {node}")
        _require(
            isinstance(node.get("nodeClassPath"), str) and node["nodeClassPath"].startswith("/Script/"),
            f"material index node missing nodeClassPath: {node}",
        )
        example_files = node.get("exampleFiles", [])
        _require(isinstance(example_files, list), f"material index exampleFiles must be a list: {node}")
        for example_file in example_files:
            _require(
                isinstance(example_file, str) and (workspace_root / "material" / example_file).exists(),
                f"material index example reference missing: {example_file}",
            )

    material_database = _load_json_file(workspace_root / "material" / "catalogs" / "node-database.json")
    _require(material_database.get("graphType") == "material", "material database graphType mismatch")
    _require(material_database.get("coverage") == "source-derived", "material database coverage mismatch")
    material_database_nodes = material_database.get("nodes")
    _require(
        isinstance(material_database_nodes, list) and len(material_database_nodes) >= 250,
        "material database missing full source-derived node set",
    )
    _require(
        any(node.get("className") == "UMaterialExpressionMultiply" for node in material_database_nodes if isinstance(node, dict)),
        "material database missing UMaterialExpressionMultiply",
    )
    scalar_parameter = next(
        (node for node in material_database_nodes if isinstance(node, dict) and node.get("className") == "UMaterialExpressionScalarParameter"),
        None,
    )
    _require(isinstance(scalar_parameter, dict), "material database missing UMaterialExpressionScalarParameter")
    scalar_testing = scalar_parameter.get("testing")
    _require(
        isinstance(scalar_testing, dict) and scalar_testing.get("profile") == "read_write_roundtrip",
        f"material scalar parameter testing missing: {scalar_parameter}",
    )
    multiply_node = next(
        (node for node in material_database_nodes if isinstance(node, dict) and node.get("className") == "UMaterialExpressionMultiply"),
        None,
    )
    _require(isinstance(multiply_node, dict), "material database missing UMaterialExpressionMultiply node object")
    multiply_testing = multiply_node.get("testing")
    _require(
        isinstance(multiply_testing, dict) and multiply_testing.get("profile") == "construct_and_query",
        f"material multiply testing missing: {multiply_node}",
    )
    function_call = next(
        (node for node in material_database_nodes if isinstance(node, dict) and node.get("className") == "UMaterialExpressionMaterialFunctionCall"),
        None,
    )
    _require(isinstance(function_call, dict), "material database missing UMaterialExpressionMaterialFunctionCall")
    function_testing = function_call.get("testing")
    _require(
        isinstance(function_testing, dict)
        and function_testing.get("profile") == "context_recipe_required"
        and function_testing.get("recipe") == "material_function_call"
        and isinstance(function_testing.get("querySurface"), dict)
        and function_testing["querySurface"].get("kind") == "child_graph_ref",
        f"material function call testing missing: {function_call}",
    )

    pcg_index = _load_json_file(workspace_root / "pcg" / "catalogs" / "node-index.json")
    _require(pcg_index.get("graphType") == "pcg", f"pcg index graphType mismatch: {pcg_index}")
    pcg_index_nodes = pcg_index.get("nodes")
    _require(isinstance(pcg_index_nodes, list) and pcg_index_nodes, "pcg index missing nodes[]")
    for node in pcg_index_nodes:
        _require(isinstance(node, dict), f"pcg index node is not an object: {node}")
        _require(isinstance(node.get("displayName"), str) and node["displayName"], f"pcg index node missing displayName: {node}")
        _require(
            isinstance(node.get("nodeClassPath"), str) and node["nodeClassPath"].startswith("/Script/"),
            f"pcg index node missing nodeClassPath: {node}",
        )
        example_files = node.get("exampleFiles", [])
        _require(isinstance(example_files, list), f"pcg index exampleFiles must be a list: {node}")
        for example_file in example_files:
            _require(
                isinstance(example_file, str) and (workspace_root / "pcg" / example_file).exists(),
                f"pcg index example reference missing: {example_file}",
            )

    pcg_database = _load_json_file(workspace_root / "pcg" / "catalogs" / "node-database.json")
    _require(pcg_database.get("graphType") == "pcg", "pcg database graphType mismatch")
    _require(pcg_database.get("coverage") == "source-derived", "pcg database coverage mismatch")
    pcg_database_nodes = pcg_database.get("nodes")
    _require(
        isinstance(pcg_database_nodes, list) and len(pcg_database_nodes) >= 170,
        "pcg database missing full source-derived node set",
    )
    transform_points = next(
        (node for node in pcg_database_nodes if isinstance(node, dict) and node.get("className") == "UPCGTransformPointsSettings"),
        None,
    )
    _require(isinstance(transform_points, dict), "pcg database missing UPCGTransformPointsSettings")
    _require(transform_points.get("family") == "transform", f"pcg transform family mismatch: {transform_points}")
    testing = transform_points.get("testing")
    _require(isinstance(testing, dict) and testing.get("profile") == "read_write_roundtrip", f"pcg transform testing missing: {transform_points}")
    _require(
        isinstance(testing.get("querySurface"), dict) and testing["querySurface"].get("kind") == "pin_default",
        f"pcg transform querySurface mismatch: {transform_points}",
    )

    get_actor_property = next(
        (node for node in pcg_database_nodes if isinstance(node, dict) and node.get("className") == "UPCGGetActorPropertySettings"),
        None,
    )
    _require(isinstance(get_actor_property, dict), "pcg database missing UPCGGetActorPropertySettings")
    get_actor_property_testing = get_actor_property.get("testing")
    _require(
        isinstance(get_actor_property_testing, dict)
        and isinstance(get_actor_property_testing.get("querySurface"), dict)
        and get_actor_property_testing["querySurface"].get("kind") == "effective_settings"
        and get_actor_property_testing["querySurface"].get("groups") == ["actorSelector", "outputAttributeName", "componentSelector"],
        f"pcg GetActorProperty querySurface mismatch: {get_actor_property}",
    )

    spawn_actor = next(
        (node for node in pcg_database_nodes if isinstance(node, dict) and node.get("className") == "UPCGSpawnActorSettings"),
        None,
    )
    _require(isinstance(spawn_actor, dict), "pcg database missing UPCGSpawnActorSettings")
    spawn_actor_testing = spawn_actor.get("testing")
    _require(
        isinstance(spawn_actor_testing, dict)
        and isinstance(spawn_actor_testing.get("querySurface"), dict)
        and spawn_actor_testing["querySurface"].get("kind") == "effective_settings"
        and spawn_actor_testing["querySurface"].get("groups") == ["templateIdentity", "spawnBehavior", "propertyOverrides", "dataLayerSettings", "hlodSettings"],
        f"pcg SpawnActor querySurface mismatch: {spawn_actor}",
    )

    print("[PASS] workspace graph catalogs validated")


def validate_generated_blueprint_test_plan() -> None:
    with tempfile.TemporaryDirectory(prefix="loomle-blueprint-plan-") as tmpdir:
        output_path = Path(tmpdir) / "blueprint_test_plan.json"
        subprocess.run(
            [
                sys.executable,
                str(TEST_TOOLS_DIR / "generate_graph_test_plan.py"),
                "--graph-type",
                "blueprint",
                "--output",
                str(output_path),
            ],
            check=True,
            cwd=str(REPO_ROOT),
        )
        plan = _load_json_file(output_path)
        _require(plan.get("version") == "1", f"blueprint test plan version mismatch: {plan}")
        _require(plan.get("graphType") == "blueprint", f"blueprint test plan graphType mismatch: {plan}")
        source_catalog = plan.get("sourceCatalog")
        _require(isinstance(source_catalog, dict), f"blueprint test plan sourceCatalog missing: {plan}")
        _require(
            source_catalog.get("path") == str(REPO_ROOT / "workspace" / "Loomle" / "blueprint" / "catalogs" / "node-database.json"),
            f"blueprint test plan sourceCatalog path mismatch: {source_catalog}",
        )
        summary = plan.get("summary")
        _require(summary == EXPECTED_BLUEPRINT_PLAN_SUMMARY, f"blueprint test plan summary mismatch: {summary}")
        entries = plan.get("entries")
        _require(isinstance(entries, list) and len(entries) == EXPECTED_BLUEPRINT_PLAN_SUMMARY["totalNodes"], "blueprint test plan entries mismatch")

        entry_by_class = {
            entry.get("className"): entry
            for entry in entries
            if isinstance(entry, dict) and isinstance(entry.get("className"), str)
        }

        branch = entry_by_class.get("UK2Node_IfThenElse")
        _require(isinstance(branch, dict), "blueprint plan missing UK2Node_IfThenElse")
        _require(branch.get("profile") == "semantic_family_represented", f"blueprint branch profile mismatch: {branch}")
        _require(branch.get("mode") == "workflow_map", f"blueprint branch mode mismatch: {branch}")
        _require(branch.get("status") == "workflow_only", f"blueprint branch status mismatch: {branch}")
        _require(
            isinstance(branch.get("focus"), dict) and branch["focus"].get("workflowFamilies") == ["blueprint_local_control_flow"],
            f"blueprint branch focus mismatch: {branch}",
        )

        function_call = entry_by_class.get("UK2Node_CallFunction")
        _require(isinstance(function_call, dict), "blueprint plan missing UK2Node_CallFunction")
        _require(function_call.get("mode") == "recipe_case", f"blueprint function-call mode mismatch: {function_call}")
        _require(function_call.get("recipe") == "blueprint_function_call", f"blueprint function-call recipe mismatch: {function_call}")
        _require(function_call.get("fixture") == "blueprint_function_graph", f"blueprint function-call fixture mismatch: {function_call}")
        _require(function_call.get("status") == "ready", f"blueprint function-call status mismatch: {function_call}")

        variable_get = entry_by_class.get("UK2Node_VariableGet")
        _require(isinstance(variable_get, dict), "blueprint plan missing UK2Node_VariableGet")
        _require(variable_get.get("mode") == "recipe_case", f"blueprint variable-get mode mismatch: {variable_get}")
        _require(variable_get.get("recipe") == "blueprint_variable_access", f"blueprint variable-get recipe mismatch: {variable_get}")
        _require(variable_get.get("fixture") == "blueprint_function_graph", f"blueprint variable-get fixture mismatch: {variable_get}")
        _require(variable_get.get("status") == "ready", f"blueprint variable-get status mismatch: {variable_get}")

        add_component = entry_by_class.get("UK2Node_AddComponent")
        _require(isinstance(add_component, dict), "blueprint plan missing UK2Node_AddComponent")
        _require(add_component.get("profile") == "context_recipe_required", f"blueprint add-component profile mismatch: {add_component}")
        _require(add_component.get("mode") == "recipe_case", f"blueprint add-component mode mismatch: {add_component}")
        _require(add_component.get("recipe") == "blueprint_component_template_context", f"blueprint add-component recipe mismatch: {add_component}")
        _require(add_component.get("fixture") == "blueprint_component_template_context", f"blueprint add-component fixture mismatch: {add_component}")
        _require(
            isinstance(add_component.get("querySurface"), dict)
            and add_component["querySurface"].get("kind") == "embedded_template",
            f"blueprint add-component querySurface mismatch: {add_component}",
        )

        timeline = entry_by_class.get("UK2Node_Timeline")
        _require(isinstance(timeline, dict), "blueprint plan missing UK2Node_Timeline")
        _require(timeline.get("profile") == "context_recipe_required", f"blueprint timeline profile mismatch: {timeline}")
        _require(timeline.get("mode") == "recipe_case", f"blueprint timeline mode mismatch: {timeline}")
        _require(timeline.get("recipe") == "blueprint_timeline_graph", f"blueprint timeline recipe mismatch: {timeline}")
        _require(timeline.get("fixture") == "blueprint_timeline_graph", f"blueprint timeline fixture mismatch: {timeline}")
        _require(
            isinstance(timeline.get("querySurface"), dict)
            and timeline["querySurface"].get("kind") == "embedded_template",
            f"blueprint timeline querySurface mismatch: {timeline}",
        )

        add_component_by_class = entry_by_class.get("UK2Node_AddComponentByClass")
        _require(isinstance(add_component_by_class, dict), "blueprint plan missing UK2Node_AddComponentByClass")
        _require(add_component_by_class.get("profile") == "context_recipe_required", f"blueprint add-component-by-class profile mismatch: {add_component_by_class}")
        _require(add_component_by_class.get("mode") == "recipe_case", f"blueprint add-component-by-class mode mismatch: {add_component_by_class}")
        _require(add_component_by_class.get("recipe") == "blueprint_actor_execution_graph", f"blueprint add-component-by-class recipe mismatch: {add_component_by_class}")
        _require(add_component_by_class.get("fixture") == "blueprint_actor_execution_graph", f"blueprint add-component-by-class fixture mismatch: {add_component_by_class}")
        _require(
            isinstance(add_component_by_class.get("querySurface"), dict)
            and add_component_by_class["querySurface"].get("kind") == "context_sensitive_construct",
            f"blueprint add-component-by-class querySurface mismatch: {add_component_by_class}",
        )

        macro_instance = entry_by_class.get("UK2Node_MacroInstance")
        _require(isinstance(macro_instance, dict), "blueprint plan missing UK2Node_MacroInstance")
        _require(macro_instance.get("mode") == "recipe_case", f"blueprint macro-instance mode mismatch: {macro_instance}")
        _require(macro_instance.get("status") == "ready", f"blueprint macro-instance status mismatch: {macro_instance}")
        _require(macro_instance.get("recipe") == "blueprint_actor_execution_graph", f"blueprint macro-instance recipe mismatch: {macro_instance}")
        _require(macro_instance.get("fixture") == "blueprint_actor_execution_graph", f"blueprint macro-instance fixture mismatch: {macro_instance}")
        _require(
            isinstance(macro_instance.get("querySurface"), dict)
            and macro_instance["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint macro-instance querySurface mismatch: {macro_instance}",
        )

        composite = entry_by_class.get("UK2Node_Composite")
        _require(isinstance(composite, dict), "blueprint plan missing UK2Node_Composite")
        _require(composite.get("mode") == "recipe_case", f"blueprint composite mode mismatch: {composite}")
        _require(composite.get("status") == "ready", f"blueprint composite status mismatch: {composite}")
        _require(composite.get("recipe") == "blueprint_actor_execution_graph", f"blueprint composite recipe mismatch: {composite}")
        _require(composite.get("fixture") == "blueprint_actor_execution_graph", f"blueprint composite fixture mismatch: {composite}")
        _require(
            isinstance(composite.get("querySurface"), dict)
            and composite["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint composite querySurface mismatch: {composite}",
        )

        function_entry = entry_by_class.get("UK2Node_FunctionEntry")
        _require(isinstance(function_entry, dict), "blueprint plan missing UK2Node_FunctionEntry")
        _require(function_entry.get("mode") == "recipe_case", f"blueprint function-entry mode mismatch: {function_entry}")
        _require(function_entry.get("status") == "ready", f"blueprint function-entry status mismatch: {function_entry}")
        _require(function_entry.get("recipe") == "blueprint_function_graph", f"blueprint function-entry recipe mismatch: {function_entry}")
        _require(function_entry.get("fixture") == "blueprint_function_graph", f"blueprint function-entry fixture mismatch: {function_entry}")
        _require(
            isinstance(function_entry.get("querySurface"), dict)
            and function_entry["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint function-entry querySurface mismatch: {function_entry}",
        )

        function_result = entry_by_class.get("UK2Node_FunctionResult")
        _require(isinstance(function_result, dict), "blueprint plan missing UK2Node_FunctionResult")
        _require(function_result.get("mode") == "recipe_case", f"blueprint function-result mode mismatch: {function_result}")
        _require(function_result.get("status") == "ready", f"blueprint function-result status mismatch: {function_result}")
        _require(function_result.get("recipe") == "blueprint_function_graph", f"blueprint function-result recipe mismatch: {function_result}")
        _require(function_result.get("fixture") == "blueprint_function_graph", f"blueprint function-result fixture mismatch: {function_result}")
        _require(
            isinstance(function_result.get("querySurface"), dict)
            and function_result["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint function-result querySurface mismatch: {function_result}",
        )

        tunnel = entry_by_class.get("UK2Node_Tunnel")
        _require(isinstance(tunnel, dict), "blueprint plan missing UK2Node_Tunnel")
        _require(tunnel.get("mode") == "recipe_case", f"blueprint tunnel mode mismatch: {tunnel}")
        _require(tunnel.get("status") == "ready", f"blueprint tunnel status mismatch: {tunnel}")
        _require(tunnel.get("recipe") == "blueprint_actor_execution_graph", f"blueprint tunnel recipe mismatch: {tunnel}")
        _require(tunnel.get("fixture") == "blueprint_actor_execution_graph", f"blueprint tunnel fixture mismatch: {tunnel}")
        _require(
            isinstance(tunnel.get("querySurface"), dict)
            and tunnel["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint tunnel querySurface mismatch: {tunnel}",
        )

        tunnel_boundary = entry_by_class.get("UK2Node_TunnelBoundary")
        _require(isinstance(tunnel_boundary, dict), "blueprint plan missing UK2Node_TunnelBoundary")
        _require(tunnel_boundary.get("mode") == "blocked", f"blueprint tunnel-boundary mode mismatch: {tunnel_boundary}")
        _require(tunnel_boundary.get("status") == "blocked", f"blueprint tunnel-boundary status mismatch: {tunnel_boundary}")
        _require(
            isinstance(tunnel_boundary.get("querySurface"), dict)
            and tunnel_boundary["querySurface"].get("kind") == "graph_boundary_summary",
            f"blueprint tunnel-boundary querySurface mismatch: {tunnel_boundary}",
        )
        _require("missing recipe" in str(tunnel_boundary.get("reason")), f"blueprint tunnel-boundary reason mismatch: {tunnel_boundary}")

        variable = entry_by_class.get("UK2Node_Variable")
        _require(isinstance(variable, dict), "blueprint plan missing UK2Node_Variable")
        _require(variable.get("mode") == "inventory", f"blueprint variable mode mismatch: {variable}")
        _require(variable.get("status") == "inventory_only", f"blueprint variable status mismatch: {variable}")

        print("[PASS] generated Blueprint test plan validated")


def validate_generated_pcg_test_plan() -> None:
    with tempfile.TemporaryDirectory(prefix="loomle-pcg-plan-") as tmpdir:
        output_path = Path(tmpdir) / "pcg_test_plan.json"
        subprocess.run(
            [
                sys.executable,
                str(TEST_TOOLS_DIR / "generate_graph_test_plan.py"),
                "--graph-type",
                "pcg",
                "--output",
                str(output_path),
            ],
            check=True,
            cwd=str(REPO_ROOT),
        )
        plan = _load_json_file(output_path)
        _require(plan.get("version") == "1", f"pcg test plan version mismatch: {plan}")
        _require(plan.get("graphType") == "pcg", f"pcg test plan graphType mismatch: {plan}")
        source_catalog = plan.get("sourceCatalog")
        _require(isinstance(source_catalog, dict), f"pcg test plan sourceCatalog missing: {plan}")
        _require(
            source_catalog.get("path") == str(REPO_ROOT / "workspace" / "Loomle" / "pcg" / "catalogs" / "node-database.json"),
            f"pcg test plan sourceCatalog path mismatch: {source_catalog}",
        )
        summary = plan.get("summary")
        _require(summary == EXPECTED_PCG_PLAN_SUMMARY, f"pcg test plan summary mismatch: {summary}")
        entries = plan.get("entries")
        _require(isinstance(entries, list) and len(entries) == EXPECTED_PCG_PLAN_SUMMARY["totalNodes"], "pcg test plan entries mismatch")

        entry_by_class = {
            entry.get("className"): entry
            for entry in entries
            if isinstance(entry, dict) and isinstance(entry.get("className"), str)
        }

        transform_points = entry_by_class.get("UPCGTransformPointsSettings")
        _require(isinstance(transform_points, dict), "pcg plan missing UPCGTransformPointsSettings")
        _require(transform_points.get("profile") == "read_write_roundtrip", f"pcg transform plan profile mismatch: {transform_points}")
        _require(transform_points.get("mode") == "auto_case", f"pcg transform plan mode mismatch: {transform_points}")
        _require(transform_points.get("fixture") == "pcg_graph", f"pcg transform plan fixture mismatch: {transform_points}")
        _require(transform_points.get("status") == "ready", f"pcg transform plan status mismatch: {transform_points}")
        _require(
            isinstance(transform_points.get("querySurface"), dict)
            and transform_points["querySurface"].get("kind") == "pin_default",
            f"pcg transform querySurface mismatch: {transform_points}",
        )

        actor_source = entry_by_class.get("UPCGDataFromActorSettings")
        _require(isinstance(actor_source, dict), "pcg plan missing UPCGDataFromActorSettings")
        _require(actor_source.get("mode") == "recipe_case", f"pcg actor-source mode mismatch: {actor_source}")
        _require(actor_source.get("recipe") == "pcg_actor_source_context", f"pcg actor-source recipe mismatch: {actor_source}")
        _require(actor_source.get("fixture") == "pcg_graph_with_world_actor", f"pcg actor-source fixture mismatch: {actor_source}")
        _require(actor_source.get("status") == "ready", f"pcg actor-source status mismatch: {actor_source}")
        _require(
            isinstance(actor_source.get("querySurface"), dict)
            and actor_source["querySurface"].get("kind") == "effective_settings"
            and actor_source["querySurface"].get("groups") == ["actorSelector", "componentSelector"],
            f"pcg actor-source querySurface mismatch: {actor_source}",
        )

        console_variable = entry_by_class.get("UPCGGetConsoleVariableSettings")
        _require(isinstance(console_variable, dict), "pcg plan missing UPCGGetConsoleVariableSettings")
        _require(console_variable.get("mode") == "auto_case", f"pcg console-variable mode mismatch: {console_variable}")
        _require(console_variable.get("fixture") == "pcg_graph", f"pcg console-variable fixture mismatch: {console_variable}")
        _require(console_variable.get("recipe") is None, f"pcg console-variable recipe mismatch: {console_variable}")
        _require(console_variable.get("status") == "ready", f"pcg console-variable status mismatch: {console_variable}")

        filter_by_attribute = entry_by_class.get("UPCGFilterByAttributeSettings")
        _require(isinstance(filter_by_attribute, dict), "pcg plan missing UPCGFilterByAttributeSettings")
        _require(
            isinstance(filter_by_attribute.get("focus"), dict)
            and filter_by_attribute["focus"].get("selectorFields") == ["TargetAttribute"],
            f"pcg FilterByAttribute selectorFields mismatch: {filter_by_attribute}",
        )

        get_actor_property = entry_by_class.get("UPCGGetActorPropertySettings")
        _require(isinstance(get_actor_property, dict), "pcg plan missing UPCGGetActorPropertySettings")
        _require(
            isinstance(get_actor_property.get("focus"), dict)
            and get_actor_property["focus"].get("selectorFields") == ["ActorSelector", "OutputAttributeName"],
            f"pcg GetActorProperty selectorFields mismatch: {get_actor_property}",
        )
        _require(
            isinstance(get_actor_property.get("querySurface"), dict)
            and get_actor_property["querySurface"].get("kind") == "effective_settings"
            and get_actor_property["querySurface"].get("groups") == ["actorSelector", "outputAttributeName", "componentSelector"],
            f"pcg GetActorProperty querySurface mismatch: {get_actor_property}",
        )
        _require(
            isinstance(get_actor_property.get("focus"), dict)
            and get_actor_property["focus"].get("effectiveSettingsGroups") == ["actorSelector", "outputAttributeName", "componentSelector"],
            f"pcg GetActorProperty effectiveSettingsGroups mismatch: {get_actor_property}",
        )

        static_mesh_spawner = entry_by_class.get("UPCGStaticMeshSpawnerSettings")
        _require(isinstance(static_mesh_spawner, dict), "pcg plan missing UPCGStaticMeshSpawnerSettings")
        _require(
            isinstance(static_mesh_spawner.get("focus"), dict)
            and static_mesh_spawner["focus"].get("selectorFields") == ["MeshSelectorParameters"],
            f"pcg StaticMeshSpawner selectorFields mismatch: {static_mesh_spawner}",
        )
        _require(
            isinstance(static_mesh_spawner.get("querySurface"), dict)
            and static_mesh_spawner["querySurface"].get("kind") == "effective_settings"
            and static_mesh_spawner["querySurface"].get("groups") == ["meshSelector"],
            f"pcg StaticMeshSpawner querySurface mismatch: {static_mesh_spawner}",
        )
        _require(
            isinstance(static_mesh_spawner.get("focus"), dict)
            and static_mesh_spawner["focus"].get("effectiveSettingsGroups") == ["meshSelector"],
            f"pcg StaticMeshSpawner effectiveSettingsGroups mismatch: {static_mesh_spawner}",
        )

        spawn_actor = entry_by_class.get("UPCGSpawnActorSettings")
        _require(isinstance(spawn_actor, dict), "pcg plan missing UPCGSpawnActorSettings")
        _require(
            isinstance(spawn_actor.get("querySurface"), dict)
            and spawn_actor["querySurface"].get("kind") == "effective_settings"
            and spawn_actor["querySurface"].get("groups") == ["templateIdentity", "spawnBehavior", "propertyOverrides", "dataLayerSettings", "hlodSettings"],
            f"pcg SpawnActor querySurface mismatch: {spawn_actor}",
        )
        _require(
            isinstance(spawn_actor.get("focus"), dict)
            and spawn_actor["focus"].get("effectiveSettingsGroups") == ["templateIdentity", "spawnBehavior", "propertyOverrides", "dataLayerSettings", "hlodSettings"],
            f"pcg SpawnActor effectiveSettingsGroups mismatch: {spawn_actor}",
        )

        branch = entry_by_class.get("UPCGBranchSettings")
        _require(isinstance(branch, dict), "pcg plan missing UPCGBranchSettings")
        _require(branch.get("mode") == "workflow_map", f"pcg branch mode mismatch: {branch}")
        _require(branch.get("status") == "workflow_only", f"pcg branch status mismatch: {branch}")
        _require(
            isinstance(branch.get("focus"), dict) and branch["focus"].get("workflowFamilies") == ["pcg_control_flow"],
            f"pcg branch focus mismatch: {branch}",
        )

        subgraph = entry_by_class.get("UPCGSubgraphSettings")
        _require(isinstance(subgraph, dict), "pcg plan missing UPCGSubgraphSettings")
        _require(subgraph.get("mode") == "blocked", f"pcg subgraph mode mismatch: {subgraph}")
        _require(subgraph.get("status") == "blocked", f"pcg subgraph status mismatch: {subgraph}")
        _require("missing recipe" in str(subgraph.get("reason")), f"pcg subgraph reason mismatch: {subgraph}")
        _require(
            isinstance(subgraph.get("querySurface"), dict)
            and subgraph["querySurface"].get("kind") == "child_graph_ref",
            f"pcg subgraph querySurface mismatch: {subgraph}",
        )

        deprecated_grass = entry_by_class.get("UDEPRECATED_PCGGenerateGrassMapsSettings")
        _require(isinstance(deprecated_grass, dict), "pcg plan missing UDEPRECATED_PCGGenerateGrassMapsSettings")
        _require(deprecated_grass.get("mode") == "inventory", f"pcg deprecated grass mode mismatch: {deprecated_grass}")
        _require(deprecated_grass.get("status") == "inventory_only", f"pcg deprecated grass status mismatch: {deprecated_grass}")

        print("[PASS] generated PCG test plan validated")


def validate_generated_blueprint_coverage_report() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "generate_graph_test_coverage_report.py"),
            "--graph-type",
            "blueprint",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"blueprint coverage report version mismatch: {report}")
    _require(report.get("graphType") == "blueprint", f"blueprint coverage report graphType mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_COVERAGE_SUMMARY, f"blueprint coverage report summary mismatch: {summary}")

    blocked_reasons = report.get("blockedReasons")
    _require(blocked_reasons == {"missing recipe": 1}, f"blueprint coverage blockedReasons mismatch: {blocked_reasons}")

    family_rows = report.get("familySummary")
    _require(isinstance(family_rows, list), f"blueprint coverage familySummary missing: {report}")
    family_by_name = {
        row.get("family"): row
        for row in family_rows
        if isinstance(row, dict) and isinstance(row.get("family"), str)
    }

    utility_family = family_by_name.get("utility")
    _require(isinstance(utility_family, dict), "blueprint coverage missing utility family")
    _require(
        utility_family.get("coverageDimensions") == {"construct": 69, "inventory": 71, "query_structure": 66, "recipe_context": 3},
        f"blueprint coverage utility dimensions mismatch: {utility_family}",
    )

    variable_family = family_by_name.get("variable")
    _require(isinstance(variable_family, dict), "blueprint coverage missing variable family")
    _require(
        variable_family.get("coverageDimensions") == {"construct": 11, "inventory": 14, "query_structure": 9, "recipe_context": 2},
        f"blueprint coverage variable dimensions mismatch: {variable_family}",
    )

    struct_family = family_by_name.get("struct")
    _require(isinstance(struct_family, dict), "blueprint coverage missing struct family")
    _require(
        struct_family.get("coverageDimensions") == {"construct": 5, "inventory": 6, "recipe_context": 5},
        f"blueprint coverage struct dimensions mismatch: {struct_family}",
    )

    print("[PASS] generated Blueprint coverage report validated")


def validate_generated_blueprint_workflow_truth_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_blueprint_workflow_truth_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"blueprint workflow suite version mismatch: {suite}")
    _require(suite.get("suite") == "workflow_truth", f"blueprint workflow suite id mismatch: {suite}")
    _require(suite.get("graphType") == "blueprint", f"blueprint workflow suite graphType mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_WORKFLOW_SUITE_SUMMARY, f"blueprint workflow suite summary mismatch: {summary}")

    cases = suite.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_BLUEPRINT_WORKFLOW_SUITE_SUMMARY["totalCases"], f"blueprint workflow suite cases mismatch: {suite}")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    branch_case = case_by_id.get("branch_local_subgraph")
    _require(isinstance(branch_case, dict), "blueprint workflow suite missing branch_local_subgraph")
    _require(branch_case.get("expectedNodes") == 3, f"blueprint branch workflow expectedNodes mismatch: {branch_case}")
    _require(branch_case.get("expectedEdges") == 2, f"blueprint branch workflow expectedEdges mismatch: {branch_case}")

    replace_branch_case = case_by_id.get("replace_branch_with_sequence")
    _require(isinstance(replace_branch_case, dict), "blueprint workflow suite missing replace_branch_with_sequence")
    _require(replace_branch_case.get("expectedNodes") == 4, f"blueprint replace-branch expectedNodes mismatch: {replace_branch_case}")
    _require(replace_branch_case.get("expectedEdges") == 3, f"blueprint replace-branch expectedEdges mismatch: {replace_branch_case}")

    replace_delay_case = case_by_id.get("replace_delay_with_do_once")
    _require(isinstance(replace_delay_case, dict), "blueprint workflow suite missing replace_delay_with_do_once")
    _require(
        replace_delay_case.get("families") == ["function_call", "struct", "utility"],
        f"blueprint replace-delay workflow families mismatch: {replace_delay_case}",
    )

    print("[PASS] generated Blueprint workflow truth suite validated")


def validate_generated_blueprint_negative_boundary_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_blueprint_negative_boundary_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"blueprint negative suite version mismatch: {suite}")
    _require(suite.get("suite") == "negative_boundary", f"blueprint negative suite id mismatch: {suite}")
    _require(suite.get("graphType") == "blueprint", f"blueprint negative suite graphType mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_NEGATIVE_SUITE_SUMMARY, f"blueprint negative suite summary mismatch: {summary}")

    cases = suite.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_BLUEPRINT_NEGATIVE_SUITE_SUMMARY["totalCases"], f"blueprint negative suite cases mismatch: {suite}")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    stale_case = case_by_id.get("stale_expected_revision_conflict")
    _require(isinstance(stale_case, dict), "blueprint negative suite missing stale_expected_revision_conflict")
    _require(stale_case.get("fixture") == "blueprint_event_graph", f"blueprint stale revision fixture mismatch: {stale_case}")
    _require(stale_case.get("families") == ["branch", "utility"], f"blueprint stale revision families mismatch: {stale_case}")

    bad_default_case = case_by_id.get("set_pin_default_bad_target_diagnostics")
    _require(isinstance(bad_default_case, dict), "blueprint negative suite missing set_pin_default_bad_target_diagnostics")
    _require(bad_default_case.get("operation") == "setPinDefault", f"blueprint bad setPinDefault operation mismatch: {bad_default_case}")
    _require(bad_default_case.get("families") == ["branch", "variable"], f"blueprint bad setPinDefault families mismatch: {bad_default_case}")

    partial_case = case_by_id.get("partial_apply_unsupported_op")
    _require(isinstance(partial_case, dict), "blueprint negative suite missing partial_apply_unsupported_op")
    _require(partial_case.get("operation") == "batch_partial_apply", f"blueprint partial-apply operation mismatch: {partial_case}")
    _require(
        partial_case.get("families") == ["branch", "struct", "utility"],
        f"blueprint partial-apply families mismatch: {partial_case}",
    )

    print("[PASS] generated Blueprint negative boundary suite validated")


def validate_generated_blueprint_stability_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_blueprint_stability_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"blueprint stability suite version mismatch: {suite}")
    _require(suite.get("graphType") == "blueprint", f"blueprint stability suite graphType mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_STABILITY_SUITE_SUMMARY, f"blueprint stability suite summary mismatch: {summary}")

    cases = suite.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_BLUEPRINT_STABILITY_SUITE_SUMMARY["totalCases"], f"blueprint stability suite cases mismatch: {suite}")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    query_case = case_by_id.get("query_snapshot_repeatability_roundtrip")
    _require(isinstance(query_case, dict), "blueprint stability suite missing query_snapshot_repeatability_roundtrip")
    _require(query_case.get("fixture") == "blueprint_event_graph", f"blueprint query repeatability fixture mismatch: {query_case}")

    verify_case = case_by_id.get("verify_repeatability_workflow")
    _require(isinstance(verify_case, dict), "blueprint stability suite missing verify_repeatability_workflow")
    _require(verify_case.get("workflowCaseId") == "replace_branch_with_sequence", f"blueprint verify repeatability workflow mismatch: {verify_case}")

    fresh_case = case_by_id.get("workflow_repeatability_fresh_session")
    _require(isinstance(fresh_case, dict), "blueprint stability suite missing workflow_repeatability_fresh_session")
    _require(fresh_case.get("workflowCaseId") == "replace_delay_with_do_once", f"blueprint fresh-session workflow mismatch: {fresh_case}")

    print("[PASS] generated Blueprint stability suite validated")


def validate_generated_blueprint_residual_gap_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_blueprint_residual_gap_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"blueprint residual-gap suite version mismatch: {suite}")
    _require(suite.get("graphType") == "blueprint", f"blueprint residual-gap suite graphType mismatch: {suite}")
    _require(suite.get("suite") == "residual_gap", f"blueprint residual-gap suite id mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_RESIDUAL_GAP_SUITE_SUMMARY, f"blueprint residual-gap suite summary mismatch: {summary}")
    cases = suite.get("cases")
    _require(
        isinstance(cases, list) and len(cases) == EXPECTED_BLUEPRINT_RESIDUAL_GAP_SUITE_SUMMARY["totalCases"],
        "blueprint residual-gap suite cases mismatch",
    )
    _require(cases == [], f"blueprint residual-gap suite should now be empty: {cases}")

    print("[PASS] generated Blueprint residual-gap suite validated")


def validate_generated_blueprint_embedded_template_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_blueprint_embedded_template_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"blueprint embedded-template suite version mismatch: {suite}")
    _require(suite.get("graphType") == "blueprint", f"blueprint embedded-template suite graphType mismatch: {suite}")
    _require(suite.get("suite") == "embedded_template", f"blueprint embedded-template suite id mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_BLUEPRINT_EMBEDDED_TEMPLATE_SUITE_SUMMARY, f"blueprint embedded-template suite summary mismatch: {summary}")
    cases = suite.get("cases")
    _require(
        isinstance(cases, list) and len(cases) == EXPECTED_BLUEPRINT_EMBEDDED_TEMPLATE_SUITE_SUMMARY["totalCases"],
        "blueprint embedded-template suite cases mismatch",
    )
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    add_component_case = case_by_id.get("add_component_embedded_template_surface")
    _require(isinstance(add_component_case, dict), "blueprint embedded-template suite missing add_component_embedded_template_surface")
    _require(add_component_case.get("className") == "UK2Node_AddComponent", f"blueprint embedded-template add-component class mismatch: {add_component_case}")
    _require(add_component_case.get("recipe") == "blueprint_component_template_context", f"blueprint embedded-template add-component recipe mismatch: {add_component_case}")
    _require(add_component_case.get("querySurfaceKind") == "embedded_template", f"blueprint embedded-template add-component surface mismatch: {add_component_case}")
    _require(
        add_component_case.get("requiredStringFields") == ["templateName", "templateObjectPath"],
        f"blueprint embedded-template add-component required fields mismatch: {add_component_case}",
    )

    timeline_case = case_by_id.get("timeline_embedded_template_surface")
    _require(isinstance(timeline_case, dict), "blueprint embedded-template suite missing timeline_embedded_template_surface")
    _require(timeline_case.get("className") == "UK2Node_Timeline", f"blueprint embedded-template timeline class mismatch: {timeline_case}")
    _require(timeline_case.get("recipe") == "blueprint_timeline_graph", f"blueprint embedded-template timeline recipe mismatch: {timeline_case}")
    _require(timeline_case.get("querySurfaceKind") == "embedded_template", f"blueprint embedded-template timeline surface mismatch: {timeline_case}")
    _require(
        timeline_case.get("requiredStringFields") == ["templateName", "templatePath", "updateFunctionName", "finishedFunctionName", "timelineGuid"],
        f"blueprint embedded-template timeline required fields mismatch: {timeline_case}",
    )

    print("[PASS] generated Blueprint embedded-template suite validated")


def validate_generated_material_test_plan() -> None:
    with tempfile.TemporaryDirectory(prefix="loomle-material-plan-") as tmpdir:
        output_path = Path(tmpdir) / "material_test_plan.json"
        subprocess.run(
            [
                sys.executable,
                str(TEST_TOOLS_DIR / "generate_graph_test_plan.py"),
                "--graph-type",
                "material",
                "--output",
                str(output_path),
            ],
            check=True,
            cwd=str(REPO_ROOT),
        )
        plan = _load_json_file(output_path)
        _require(plan.get("version") == "1", f"material test plan version mismatch: {plan}")
        _require(plan.get("graphType") == "material", f"material test plan graphType mismatch: {plan}")
        source_catalog = plan.get("sourceCatalog")
        _require(isinstance(source_catalog, dict), f"material test plan sourceCatalog missing: {plan}")
        _require(
            source_catalog.get("path") == str(REPO_ROOT / "workspace" / "Loomle" / "material" / "catalogs" / "node-database.json"),
            f"material test plan sourceCatalog path mismatch: {source_catalog}",
        )
        summary = plan.get("summary")
        _require(summary == EXPECTED_MATERIAL_PLAN_SUMMARY, f"material test plan summary mismatch: {summary}")
        entries = plan.get("entries")
        _require(isinstance(entries, list) and len(entries) == EXPECTED_MATERIAL_PLAN_SUMMARY["totalNodes"], "material test plan entries mismatch")

        entry_by_class = {
            entry.get("className"): entry
            for entry in entries
            if isinstance(entry, dict) and isinstance(entry.get("className"), str)
        }

        scalar_parameter = entry_by_class.get("UMaterialExpressionScalarParameter")
        _require(isinstance(scalar_parameter, dict), "material plan missing UMaterialExpressionScalarParameter")
        _require(scalar_parameter.get("profile") == "read_write_roundtrip", f"material scalar parameter profile mismatch: {scalar_parameter}")
        _require(scalar_parameter.get("mode") == "auto_case", f"material scalar parameter mode mismatch: {scalar_parameter}")
        _require(scalar_parameter.get("fixture") == "material_graph", f"material scalar parameter fixture mismatch: {scalar_parameter}")
        _require(
            isinstance(scalar_parameter.get("focus"), dict)
            and scalar_parameter["focus"].get("fields") == ["ParameterName", "DefaultValue"],
            f"material scalar parameter focus mismatch: {scalar_parameter}",
        )

        multiply = entry_by_class.get("UMaterialExpressionMultiply")
        _require(isinstance(multiply, dict), "material plan missing UMaterialExpressionMultiply")
        _require(multiply.get("profile") == "construct_and_query", f"material multiply profile mismatch: {multiply}")
        _require(multiply.get("mode") == "auto_case", f"material multiply mode mismatch: {multiply}")
        _require(multiply.get("fixture") == "material_graph", f"material multiply fixture mismatch: {multiply}")
        _require(
            isinstance(multiply.get("focus"), dict)
            and multiply["focus"].get("workflowFamilies") == ["material_root_chain"],
            f"material multiply workflowFamilies mismatch: {multiply}",
        )

        function_call = entry_by_class.get("UMaterialExpressionMaterialFunctionCall")
        _require(isinstance(function_call, dict), "material plan missing UMaterialExpressionMaterialFunctionCall")
        _require(function_call.get("mode") == "recipe_case", f"material function call mode mismatch: {function_call}")
        _require(function_call.get("recipe") == "material_function_call", f"material function call recipe mismatch: {function_call}")
        _require(function_call.get("fixture") == "material_graph", f"material function call fixture mismatch: {function_call}")
        _require(function_call.get("status") == "ready", f"material function call status mismatch: {function_call}")
        _require(
            isinstance(function_call.get("querySurface"), dict)
            and function_call["querySurface"].get("kind") == "child_graph_ref",
            f"material function call querySurface mismatch: {function_call}",
        )

        comment = entry_by_class.get("UMaterialExpressionComment")
        _require(isinstance(comment, dict), "material plan missing UMaterialExpressionComment")
        _require(comment.get("profile") == "construct_only", f"material comment profile mismatch: {comment}")
        _require(comment.get("mode") == "auto_case", f"material comment mode mismatch: {comment}")
        _require(comment.get("fixture") == "material_graph", f"material comment fixture mismatch: {comment}")

        print("[PASS] generated Material test plan validated")


def validate_generated_material_coverage_report() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "generate_graph_test_coverage_report.py"),
            "--graph-type",
            "material",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"material coverage report version mismatch: {report}")
    _require(report.get("graphType") == "material", f"material coverage report graphType mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_MATERIAL_COVERAGE_SUMMARY, f"material coverage report summary mismatch: {summary}")

    blocked_reasons = report.get("blockedReasons")
    _require(blocked_reasons == {}, f"material coverage blockedReasons mismatch: {blocked_reasons}")

    family_rows = report.get("familySummary")
    _require(isinstance(family_rows, list), f"material coverage familySummary missing: {report}")
    family_by_name = {
        row.get("family"): row
        for row in family_rows
        if isinstance(row, dict) and isinstance(row.get("family"), str)
    }

    parameter_family = family_by_name.get("parameter")
    _require(isinstance(parameter_family, dict), "material coverage missing parameter family")
    _require(
        parameter_family.get("coverageDimensions") == {"construct": 24, "engine_truth": 9, "inventory": 24, "query_structure": 15},
        f"material coverage parameter dimensions mismatch: {parameter_family}",
    )

    expression_family = family_by_name.get("expression")
    _require(isinstance(expression_family, dict), "material coverage missing expression family")
    _require(
        expression_family.get("coverageDimensions") == {"construct": 244, "inventory": 244, "query_structure": 243, "recipe_context": 1},
        f"material coverage expression dimensions mismatch: {expression_family}",
    )

    constant_family = family_by_name.get("constant")
    _require(isinstance(constant_family, dict), "material coverage missing constant family")
    _require(
        constant_family.get("coverageDimensions") == {"construct": 6, "inventory": 6},
        f"material coverage constant dimensions mismatch: {constant_family}",
    )

    print("[PASS] generated Material coverage report validated")


def validate_generated_material_workflow_truth_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_material_workflow_truth_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("suite") == "workflow_truth", f"material workflow suite id mismatch: {suite}")
    _require(suite.get("graphType") == "material", f"material workflow suite graphType mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_MATERIAL_WORKFLOW_SUITE_SUMMARY, f"material workflow suite summary mismatch: {summary}")

    cases = suite.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_MATERIAL_WORKFLOW_SUITE_SUMMARY["totalCases"], f"material workflow suite cases mismatch: {suite}")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    replace_lerp = case_by_id.get("replace_multiply_with_lerp")
    _require(isinstance(replace_lerp, dict), "material workflow suite missing replace_multiply_with_lerp")
    _require(replace_lerp.get("expectedNodes") == 4, f"material replace_multiply_with_lerp expectedNodes mismatch: {replace_lerp}")
    _require(replace_lerp.get("expectedEdges") == 4, f"material replace_multiply_with_lerp expectedEdges mismatch: {replace_lerp}")

    root_sink = case_by_id.get("root_sink_then_layout")
    _require(isinstance(root_sink, dict), "material workflow suite missing root_sink_then_layout")
    _require(root_sink.get("expectedNodes") == 3, f"material root_sink_then_layout expectedNodes mismatch: {root_sink}")
    _require(root_sink.get("expectedEdges") == 3, f"material root_sink_then_layout expectedEdges mismatch: {root_sink}")

    print("[PASS] generated Material workflow truth suite validated")


def validate_generated_material_negative_boundary_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_material_negative_boundary_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"material negative suite version mismatch: {report}")
    _require(report.get("graphType") == "material", f"material negative suite graphType mismatch: {report}")
    _require(report.get("suite") == "negative_boundary", f"material negative suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_MATERIAL_NEGATIVE_SUITE_SUMMARY, f"material negative suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(
        isinstance(cases, list) and len(cases) == EXPECTED_MATERIAL_NEGATIVE_SUITE_SUMMARY["totalCases"],
        "material negative suite cases mismatch",
    )
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    stale_case = case_by_id.get("stale_expected_revision_conflict")
    _require(isinstance(stale_case, dict), "material negative suite missing stale expectedRevision case")
    _require(stale_case.get("operation") == "addNode.byClass", f"material negative stale operation mismatch: {stale_case}")
    _require(stale_case.get("families") == ["expression", "parameter"], f"material negative stale families mismatch: {stale_case}")

    duplicate_case = case_by_id.get("duplicate_client_ref_rejected")
    _require(isinstance(duplicate_case, dict), "material negative suite missing duplicate clientRef case")
    _require(duplicate_case.get("operation") == "addNode.byClass", f"material negative duplicate operation mismatch: {duplicate_case}")
    _require(duplicate_case.get("families") == ["constant", "parameter"], f"material negative duplicate families mismatch: {duplicate_case}")

    set_default_case = case_by_id.get("set_pin_default_unsupported")
    _require(isinstance(set_default_case, dict), "material negative suite missing setPinDefault unsupported case")
    _require(set_default_case.get("operation") == "setPinDefault", f"material negative setPinDefault operation mismatch: {set_default_case}")
    _require(set_default_case.get("families") == ["parameter"], f"material negative setPinDefault families mismatch: {set_default_case}")

    connect_case = case_by_id.get("connect_pins_bad_output_pin")
    _require(isinstance(connect_case, dict), "material negative suite missing connectPins bad output case")
    _require(connect_case.get("operation") == "connectPins", f"material negative connectPins operation mismatch: {connect_case}")
    _require(connect_case.get("families") == ["expression", "parameter"], f"material negative connectPins families mismatch: {connect_case}")

    disconnect_case = case_by_id.get("disconnect_pins_bad_output_pin")
    _require(isinstance(disconnect_case, dict), "material negative suite missing disconnectPins bad output case")
    _require(disconnect_case.get("operation") == "disconnectPins", f"material negative disconnectPins operation mismatch: {disconnect_case}")
    _require(disconnect_case.get("families") == ["expression", "parameter"], f"material negative disconnectPins families mismatch: {disconnect_case}")

    print("[PASS] generated Material negative boundary suite validated")


def validate_generated_material_stability_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_material_stability_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"material stability suite version mismatch: {report}")
    _require(report.get("graphType") == "material", f"material stability suite graphType mismatch: {report}")
    _require(report.get("suite") == "stability", f"material stability suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_MATERIAL_STABILITY_SUITE_SUMMARY, f"material stability suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_MATERIAL_STABILITY_SUITE_SUMMARY["totalCases"], "material stability suite cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    query_case = case_by_id.get("query_snapshot_repeatability_roundtrip")
    _require(isinstance(query_case, dict), "material stability suite missing query repeatability case")
    _require(query_case.get("families") == ["expression", "parameter"], f"material stability query families mismatch: {query_case}")

    verify_case = case_by_id.get("verify_repeatability_workflow")
    _require(isinstance(verify_case, dict), "material stability suite missing verify repeatability case")
    _require(verify_case.get("workflowCaseId") == "insert_multiply_before_base_color_root", f"material stability verify workflow mismatch: {verify_case}")

    fresh_case = case_by_id.get("workflow_repeatability_fresh_session")
    _require(isinstance(fresh_case, dict), "material stability suite missing fresh-session repeatability case")
    _require(fresh_case.get("workflowCaseId") == "replace_saturate_with_one_minus", f"material stability fresh-session workflow mismatch: {fresh_case}")

    print("[PASS] generated Material stability suite validated")


def validate_generated_material_child_graph_ref_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_material_child_graph_ref_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    suite = json.loads(payload)
    _require(suite.get("version") == "1", f"material childGraphRef suite version mismatch: {suite}")
    _require(suite.get("graphType") == "material", f"material childGraphRef suite graphType mismatch: {suite}")
    _require(suite.get("suite") == "child_graph_ref", f"material childGraphRef suite id mismatch: {suite}")
    summary = suite.get("summary")
    _require(summary == EXPECTED_MATERIAL_CHILD_GRAPH_REF_SUITE_SUMMARY, f"material childGraphRef suite summary mismatch: {summary}")
    cases = suite.get("cases")
    _require(
        isinstance(cases, list) and len(cases) == EXPECTED_MATERIAL_CHILD_GRAPH_REF_SUITE_SUMMARY["totalCases"],
        "material childGraphRef suite cases mismatch",
    )
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    function_case = case_by_id.get("material_function_call_child_graph_ref_traversal")
    _require(isinstance(function_case, dict), "material childGraphRef suite missing function-call case")
    _require(function_case.get("fixture") == "material_graph", f"material childGraphRef fixture mismatch: {function_case}")
    _require(function_case.get("families") == ["expression"], f"material childGraphRef families mismatch: {function_case}")
    _require(function_case.get("querySurfaceKind") == "child_graph_ref", f"material childGraphRef surface mismatch: {function_case}")

    print("[PASS] generated Material childGraphRef suite validated")


def validate_generated_pcg_coverage_report() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "generate_graph_test_coverage_report.py"),
            "--graph-type",
            "pcg",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg coverage report version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg coverage report graphType mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_COVERAGE_SUMMARY, f"pcg coverage report summary mismatch: {summary}")

    blocked_reasons = report.get("blockedReasons")
    _require(blocked_reasons == {"missing recipe": 4}, f"pcg coverage blockedReasons mismatch: {blocked_reasons}")

    family_rows = report.get("familySummary")
    _require(isinstance(family_rows, list), f"pcg coverage familySummary missing: {report}")
    family_by_name = {
        row.get("family"): row
        for row in family_rows
        if isinstance(row, dict) and isinstance(row.get("family"), str)
    }

    source_family = family_by_name.get("source")
    _require(isinstance(source_family, dict), "pcg coverage missing source family")
    _require(source_family.get("readyNodes") == 25, f"pcg coverage source ready mismatch: {source_family}")
    _require(
        source_family.get("coverageDimensions", {}).get("recipe_context") == 9,
        f"pcg coverage source recipe_context mismatch: {source_family}",
    )

    filter_family = family_by_name.get("filter")
    _require(isinstance(filter_family, dict), "pcg coverage missing filter family")
    _require(
        filter_family.get("coverageDimensions", {}).get("dynamic_shape") == 2,
        f"pcg coverage filter dynamic_shape mismatch: {filter_family}",
    )

    struct_family = family_by_name.get("struct")
    _require(isinstance(struct_family, dict), "pcg coverage missing struct family")
    _require(struct_family.get("blockedNodes") == 4, f"pcg coverage struct blocked mismatch: {struct_family}")
    _require(
        struct_family.get("coverageDimensions") == {"inventory": 4},
        f"pcg coverage struct dimensions mismatch: {struct_family}",
    )

    print("[PASS] generated PCG coverage report validated")


def validate_generated_pcg_workflow_truth_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_workflow_truth_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg workflow suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg workflow suite graphType mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_WORKFLOW_SUITE_SUMMARY, f"pcg workflow suite summary mismatch: {summary}")

    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_WORKFLOW_SUITE_SUMMARY["totalCases"], "pcg workflow cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    actor_route = case_by_id.get("actor_data_tag_route")
    _require(isinstance(actor_route, dict), "pcg workflow suite missing actor_data_tag_route")
    _require(actor_route.get("fixture") == "pcg_graph_with_world_actor", f"pcg actor route fixture mismatch: {actor_route}")
    _require(actor_route.get("families") == ["source", "route", "meta"], f"pcg actor route families mismatch: {actor_route}")
    _require(actor_route.get("queryDefaults") == 2, f"pcg actor route queryDefaults mismatch: {actor_route}")

    density_insert = case_by_id.get("insert_density_filter_before_static_mesh")
    _require(isinstance(density_insert, dict), "pcg workflow suite missing insert_density_filter_before_static_mesh")
    _require(density_insert.get("fixture") == "pcg_graph", f"pcg density insert fixture mismatch: {density_insert}")
    _require(density_insert.get("families") == ["create", "filter", "spawn"], f"pcg density insert families mismatch: {density_insert}")
    _require(density_insert.get("expectedEdges") == 2, f"pcg density insert expectedEdges mismatch: {density_insert}")

    attribute_route = case_by_id.get("replace_tag_route_with_attribute_route")
    _require(isinstance(attribute_route, dict), "pcg workflow suite missing replace_tag_route_with_attribute_route")
    _require(
        attribute_route.get("families") == ["create", "filter", "route", "meta"],
        f"pcg attribute route families mismatch: {attribute_route}",
    )
    _require(attribute_route.get("queryDefaults") == 2, f"pcg attribute route queryDefaults mismatch: {attribute_route}")

    transform_tag = case_by_id.get("transform_points_then_tag")
    _require(isinstance(transform_tag, dict), "pcg workflow suite missing transform_points_then_tag")
    _require(transform_tag.get("fixture") == "pcg_graph", f"pcg transform workflow fixture mismatch: {transform_tag}")
    _require(transform_tag.get("families") == ["create", "transform", "meta"], f"pcg transform workflow families mismatch: {transform_tag}")
    _require(transform_tag.get("queryDefaults") == 2, f"pcg transform workflow queryDefaults mismatch: {transform_tag}")

    branch_case = case_by_id.get("branch_to_dual_tags")
    _require(isinstance(branch_case, dict), "pcg workflow suite missing branch_to_dual_tags")
    _require(branch_case.get("fixture") == "pcg_graph", f"pcg branch workflow fixture mismatch: {branch_case}")
    _require(branch_case.get("families") == ["branch", "create", "meta"], f"pcg branch workflow families mismatch: {branch_case}")
    _require(branch_case.get("expectedEdges") == 3, f"pcg branch workflow expectedEdges mismatch: {branch_case}")

    select_case = case_by_id.get("boolean_select_between_two_sources")
    _require(isinstance(select_case, dict), "pcg workflow suite missing boolean_select_between_two_sources")
    _require(select_case.get("fixture") == "pcg_graph", f"pcg select workflow fixture mismatch: {select_case}")
    _require(select_case.get("families") == ["create", "meta", "select"], f"pcg select workflow families mismatch: {select_case}")
    _require(select_case.get("queryDefaults") == 2, f"pcg select workflow queryDefaults mismatch: {select_case}")

    predicate_case = case_by_id.get("predicate_get_index_then_tag")
    _require(isinstance(predicate_case, dict), "pcg workflow suite missing predicate_get_index_then_tag")
    _require(predicate_case.get("fixture") == "pcg_graph", f"pcg predicate workflow fixture mismatch: {predicate_case}")
    _require(predicate_case.get("families") == ["create", "meta", "predicate"], f"pcg predicate workflow families mismatch: {predicate_case}")
    _require(predicate_case.get("queryDefaults") == 2, f"pcg predicate workflow queryDefaults mismatch: {predicate_case}")

    struct_case = case_by_id.get("subgraph_depth_after_subgraph")
    _require(isinstance(struct_case, dict), "pcg workflow suite missing subgraph_depth_after_subgraph")
    _require(struct_case.get("fixture") == "pcg_graph", f"pcg struct workflow fixture mismatch: {struct_case}")
    _require(struct_case.get("families") == ["create", "meta", "struct"], f"pcg struct workflow families mismatch: {struct_case}")
    _require(struct_case.get("queryDefaults") == 2, f"pcg struct workflow queryDefaults mismatch: {struct_case}")

    print("[PASS] generated PCG workflow truth suite validated")


def validate_generated_graph_test_surface_report() -> None:
    with tempfile.TemporaryDirectory(prefix="loomle-surface-report-") as tmpdir:
        run_report_path = Path(tmpdir) / "run_report.json"
        run_report_path.write_text(
            json.dumps(
                {
                    "graphType": "pcg",
                    "suite": "synthetic_surface_matrix",
                    "results": [
                        {
                            "className": "UPCGTransformPointsSettings",
                            "displayName": "Transform Points",
                            "family": "transform",
                            "status": "pass",
                            "details": {
                                "surfaceMatrix": {
                                    "mutate": "pass",
                                    "queryStructure": "pass",
                                    "queryTruth": "pass",
                                    "engineTruth": "pass",
                                    "verify": "not_run",
                                    "diagnostics": "not_run",
                                }
                            },
                        },
                        {
                            "caseId": "surface_sample_to_static_mesh",
                            "families": ["source", "spawn"],
                            "status": "fail",
                            "failureKind": "query_truth_unsurfaced",
                            "reason": "workflow query truth missing surfaced default for surface_sampler.PointsPerSquaredMeter",
                            "details": {
                                "surfaceMatrix": {
                                    "mutate": "pass",
                                    "queryStructure": "pass",
                                    "queryTruth": "fail",
                                    "engineTruth": "not_run",
                                    "verify": "pass",
                                    "diagnostics": "pass",
                                }
                            },
                        },
                    ],
                },
                ensure_ascii=False,
            ),
            encoding="utf-8",
        )
        payload = subprocess.check_output(
            [
                sys.executable,
                str(TEST_TOOLS_DIR / "generate_graph_test_surface_report.py"),
                "--run-report",
                str(run_report_path),
            ],
            cwd=str(REPO_ROOT),
            text=True,
        )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"graph surface report version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"graph surface report graphType mismatch: {report}")
    _require(report.get("suite") == "synthetic_surface_matrix", f"graph surface report suite mismatch: {report}")
    summary = report.get("summary")
    _require(isinstance(summary, dict), f"graph surface report summary missing: {report}")
    _require(summary.get("totalCases") == 2, f"graph surface totalCases mismatch: {summary}")
    _require(summary.get("status") == {"fail": 1, "pass": 1}, f"graph surface status mismatch: {summary}")
    _require(
        summary.get("surfaceMatrix") == {
            "mutate": {"pass": 2},
            "queryStructure": {"pass": 2},
            "queryTruth": {"fail": 1, "pass": 1},
            "engineTruth": {"not_run": 1, "pass": 1},
            "verify": {"not_run": 1, "pass": 1},
            "diagnostics": {"not_run": 1, "pass": 1},
        },
        f"graph surface matrix mismatch: {summary}",
    )
    family_rows = report.get("familySummary")
    _require(isinstance(family_rows, list) and len(family_rows) == 3, f"graph surface familySummary mismatch: {report}")
    family_by_name = {
        row.get("family"): row
        for row in family_rows
        if isinstance(row, dict) and isinstance(row.get("family"), str)
    }
    _require(
        family_by_name.get("transform", {}).get("surfaceMatrix", {}).get("queryTruth") == {"pass": 1},
        f"graph surface transform summary mismatch: {family_by_name.get('transform')}",
    )
    _require(
        family_by_name.get("source", {}).get("surfaceMatrix", {}).get("queryTruth") == {"fail": 1},
        f"graph surface source summary mismatch: {family_by_name.get('source')}",
    )
    weak_cases = report.get("weakCases")
    _require(isinstance(weak_cases, list) and len(weak_cases) == 1, f"graph surface weakCases mismatch: {report}")
    weak_case = weak_cases[0]
    _require(weak_case.get("failedSurfaces") == ["queryTruth"], f"graph surface weakCase failedSurfaces mismatch: {weak_case}")
    _require(weak_case.get("families") == ["source", "spawn"], f"graph surface weakCase families mismatch: {weak_case}")

    print("[PASS] generated graph surface report validated")


def validate_generated_pcg_negative_boundary_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_negative_boundary_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg negative suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg negative suite graphType mismatch: {report}")
    _require(report.get("suite") == "negative_boundary", f"pcg negative suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_NEGATIVE_SUITE_SUMMARY, f"pcg negative suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_NEGATIVE_SUITE_SUMMARY["totalCases"], "pcg negative suite cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    dry_run_case = case_by_id.get("set_pin_default_requires_target_dry_run")
    _require(isinstance(dry_run_case, dict), "pcg negative suite missing dryRun target contract case")
    _require(dry_run_case.get("operation") == "setPinDefault", f"pcg negative dryRun operation mismatch: {dry_run_case}")
    _require(dry_run_case.get("families") == ["meta"], f"pcg negative dryRun families mismatch: {dry_run_case}")

    diagnostics_case = case_by_id.get("set_pin_default_bad_pin_diagnostics")
    _require(isinstance(diagnostics_case, dict), "pcg negative suite missing bad pin diagnostics case")
    _require(diagnostics_case.get("fixture") == "pcg_graph", f"pcg negative diagnostics fixture mismatch: {diagnostics_case}")

    remove_case = case_by_id.get("remove_node_requires_stable_target")
    _require(isinstance(remove_case, dict), "pcg negative suite missing stable target case")
    _require(remove_case.get("operation") == "removeNode", f"pcg negative remove operation mismatch: {remove_case}")
    _require(remove_case.get("families") == ["struct"], f"pcg negative remove families mismatch: {remove_case}")

    filter_case = case_by_id.get("set_pin_default_bad_nested_filter_path")
    _require(isinstance(filter_case, dict), "pcg negative suite missing nested filter path case")
    _require(filter_case.get("operation") == "setPinDefault", f"pcg negative filter operation mismatch: {filter_case}")
    _require(filter_case.get("families") == ["filter"], f"pcg negative filter families mismatch: {filter_case}")

    subgraph_case = case_by_id.get("set_pin_default_missing_subgraph_asset")
    _require(isinstance(subgraph_case, dict), "pcg negative suite missing subgraph asset case")
    _require(subgraph_case.get("operation") == "setPinDefault", f"pcg negative subgraph operation mismatch: {subgraph_case}")
    _require(subgraph_case.get("families") == ["struct"], f"pcg negative subgraph families mismatch: {subgraph_case}")

    connect_case = case_by_id.get("connect_pins_bad_output_pin")
    _require(isinstance(connect_case, dict), "pcg negative suite missing connect bad output case")
    _require(connect_case.get("operation") == "connectPins", f"pcg negative connect operation mismatch: {connect_case}")
    _require(connect_case.get("families") == ["branch", "create"], f"pcg negative connect families mismatch: {connect_case}")

    disconnect_case = case_by_id.get("disconnect_pins_bad_output_pin")
    _require(isinstance(disconnect_case, dict), "pcg negative suite missing disconnect bad output case")
    _require(disconnect_case.get("operation") == "disconnectPins", f"pcg negative disconnect operation mismatch: {disconnect_case}")
    _require(disconnect_case.get("families") == ["branch", "create"], f"pcg negative disconnect families mismatch: {disconnect_case}")

    print("[PASS] generated PCG negative boundary suite validated")


def validate_generated_pcg_stability_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_stability_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg stability suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg stability suite graphType mismatch: {report}")
    _require(report.get("suite") == "stability", f"pcg stability suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_STABILITY_SUITE_SUMMARY, f"pcg stability suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_STABILITY_SUITE_SUMMARY["totalCases"], "pcg stability suite cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    repeat_query = case_by_id.get("query_snapshot_repeatability_roundtrip")
    _require(isinstance(repeat_query, dict), "pcg stability suite missing query repeatability case")
    _require(repeat_query.get("fixture") == "pcg_graph", f"pcg stability query fixture mismatch: {repeat_query}")
    _require(repeat_query.get("families") == ["create", "transform"], f"pcg stability query families mismatch: {repeat_query}")

    verify_workflow = case_by_id.get("verify_repeatability_workflow")
    _require(isinstance(verify_workflow, dict), "pcg stability suite missing verify repeatability case")
    _require(
        verify_workflow.get("workflowCaseId") == "surface_sample_to_static_mesh",
        f"pcg stability verify workflowCaseId mismatch: {verify_workflow}",
    )

    fresh_session = case_by_id.get("workflow_repeatability_fresh_session")
    _require(isinstance(fresh_session, dict), "pcg stability suite missing fresh-session case")
    _require(
        fresh_session.get("workflowCaseId") == "actor_data_tag_route",
        f"pcg stability fresh-session workflowCaseId mismatch: {fresh_session}",
    )
    _require(
        fresh_session.get("families") == ["meta", "route", "source"],
        f"pcg stability fresh-session families mismatch: {fresh_session}",
    )

    print("[PASS] generated PCG stability suite validated")


def validate_generated_pcg_selector_truth_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_selector_truth_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg selector suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg selector suite graphType mismatch: {report}")
    _require(report.get("suite") == "selector_truth", f"pcg selector suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_SELECTOR_SUITE_SUMMARY, f"pcg selector suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_SELECTOR_SUITE_SUMMARY["totalCases"], "pcg selector suite cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    attribute_case = case_by_id.get("filter_by_attribute_attribute_selector")
    _require(isinstance(attribute_case, dict), "pcg selector suite missing attribute selector case")
    _require(attribute_case.get("fixture") == "pcg_graph", f"pcg selector attribute fixture mismatch: {attribute_case}")
    _require(attribute_case.get("families") == ["filter", "route"], f"pcg selector attribute families mismatch: {attribute_case}")
    _require(attribute_case.get("selectorFields") == ["TargetAttribute"], f"pcg selector attribute fields mismatch: {attribute_case}")
    _require(attribute_case.get("querySurfaceKind") == "pin_default", f"pcg selector attribute surface kind mismatch: {attribute_case}")

    property_case = case_by_id.get("filter_by_attribute_property_selector")
    _require(isinstance(property_case, dict), "pcg selector suite missing property selector case")
    _require(property_case.get("selectorFields") == ["TargetAttribute"], f"pcg selector property fields mismatch: {property_case}")
    _require(property_case.get("querySurfaceKind") == "pin_default", f"pcg selector property surface kind mismatch: {property_case}")

    actor_case = case_by_id.get("get_actor_property_selector_surface")
    _require(isinstance(actor_case, dict), "pcg selector suite missing actor selector case")
    _require(actor_case.get("fixture") == "pcg_graph_with_world_actor", f"pcg selector actor fixture mismatch: {actor_case}")
    _require(actor_case.get("families") == ["source"], f"pcg selector actor families mismatch: {actor_case}")
    _require(
        actor_case.get("selectorFields") == ["ActorSelector", "OutputAttributeName"],
        f"pcg selector actor fields mismatch: {actor_case}",
    )
    _require(actor_case.get("querySurfaceKind") == "effective_settings", f"pcg selector actor surface kind mismatch: {actor_case}")

    mesh_case = case_by_id.get("static_mesh_spawner_mesh_selector_surface")
    _require(isinstance(mesh_case, dict), "pcg selector suite missing mesh selector case")
    _require(mesh_case.get("fixture") == "pcg_graph", f"pcg selector mesh fixture mismatch: {mesh_case}")
    _require(mesh_case.get("families") == ["spawn"], f"pcg selector mesh families mismatch: {mesh_case}")
    _require(mesh_case.get("selectorFields") == ["MeshSelectorParameters"], f"pcg selector mesh fields mismatch: {mesh_case}")
    _require(mesh_case.get("querySurfaceKind") == "effective_settings", f"pcg selector mesh surface kind mismatch: {mesh_case}")

    print("[PASS] generated PCG selector truth suite validated")


def validate_generated_pcg_effective_settings_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_effective_settings_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg effectiveSettings suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg effectiveSettings suite graphType mismatch: {report}")
    _require(report.get("suite") == "effective_settings", f"pcg effectiveSettings suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_EFFECTIVE_SETTINGS_SUITE_SUMMARY, f"pcg effectiveSettings suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(
        isinstance(cases, list) and len(cases) == EXPECTED_PCG_EFFECTIVE_SETTINGS_SUITE_SUMMARY["totalCases"],
        "pcg effectiveSettings suite cases mismatch",
    )
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    get_actor_property = case_by_id.get("get_actor_property_effective_settings_truth")
    _require(isinstance(get_actor_property, dict), "pcg effectiveSettings suite missing GetActorProperty truth case")
    _require(get_actor_property.get("fixture") == "pcg_graph_with_world_actor", f"pcg effectiveSettings actor fixture mismatch: {get_actor_property}")
    _require(get_actor_property.get("assertionKind") == "truth", f"pcg effectiveSettings actor assertion kind mismatch: {get_actor_property}")
    _require(
        get_actor_property.get("effectiveSettingsGroups") == ["actorSelector", "outputAttributeName", "componentSelector"],
        f"pcg effectiveSettings actor groups mismatch: {get_actor_property}",
    )

    spawn_actor = case_by_id.get("spawn_actor_effective_settings_presence")
    _require(isinstance(spawn_actor, dict), "pcg effectiveSettings suite missing SpawnActor presence case")
    _require(spawn_actor.get("fixture") == "pcg_graph", f"pcg effectiveSettings spawn actor fixture mismatch: {spawn_actor}")
    _require(spawn_actor.get("assertionKind") == "presence_shape", f"pcg effectiveSettings spawn actor assertion kind mismatch: {spawn_actor}")
    _require(
        spawn_actor.get("effectiveSettingsGroups") == ["templateIdentity", "spawnBehavior", "propertyOverrides", "dataLayerSettings", "hlodSettings"],
        f"pcg effectiveSettings spawn actor groups mismatch: {spawn_actor}",
    )

    skinned_mesh = case_by_id.get("skinned_mesh_spawner_effective_settings_presence")
    _require(isinstance(skinned_mesh, dict), "pcg effectiveSettings suite missing SkinnedMeshSpawner presence case")
    _require(skinned_mesh.get("families") == ["spawn"], f"pcg effectiveSettings skinned mesh family mismatch: {skinned_mesh}")
    _require(
        skinned_mesh.get("effectiveSettingsGroups") == ["templateIdentity", "spawnBehavior", "meshSelector"],
        f"pcg effectiveSettings skinned mesh groups mismatch: {skinned_mesh}",
    )

    print("[PASS] generated PCG effectiveSettings suite validated")


def validate_generated_pcg_child_graph_ref_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_child_graph_ref_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg childGraphRef suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg childGraphRef suite graphType mismatch: {report}")
    _require(report.get("suite") == "child_graph_ref", f"pcg childGraphRef suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_CHILD_GRAPH_REF_SUITE_SUMMARY, f"pcg childGraphRef suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_CHILD_GRAPH_REF_SUITE_SUMMARY["totalCases"], "pcg childGraphRef suite cases mismatch")
    case_by_id = {
        case.get("id"): case
        for case in cases
        if isinstance(case, dict) and isinstance(case.get("id"), str)
    }

    subgraph_case = case_by_id.get("subgraph_child_graph_ref_traversal")
    _require(isinstance(subgraph_case, dict), "pcg childGraphRef suite missing Subgraph case")
    _require(subgraph_case.get("fixture") == "pcg_graph", f"pcg childGraphRef subgraph fixture mismatch: {subgraph_case}")
    _require(subgraph_case.get("families") == ["struct"], f"pcg childGraphRef subgraph families mismatch: {subgraph_case}")
    _require(subgraph_case.get("querySurfaceKind") == "child_graph_ref", f"pcg childGraphRef subgraph surface mismatch: {subgraph_case}")

    loop_case = case_by_id.get("loop_child_graph_ref_traversal")
    _require(isinstance(loop_case, dict), "pcg childGraphRef suite missing Loop case")
    _require(loop_case.get("fixture") == "pcg_graph", f"pcg childGraphRef loop fixture mismatch: {loop_case}")
    _require(loop_case.get("families") == ["struct"], f"pcg childGraphRef loop families mismatch: {loop_case}")
    _require(loop_case.get("querySurfaceKind") == "child_graph_ref", f"pcg childGraphRef loop surface mismatch: {loop_case}")

    print("[PASS] generated PCG childGraphRef suite validated")


def validate_generated_pcg_residual_gap_suite() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TEST_TOOLS_DIR / "run_pcg_residual_gap_suite.py"),
            "--list-cases",
        ],
        cwd=str(REPO_ROOT),
        text=True,
    )
    report = json.loads(payload)
    _require(report.get("version") == "1", f"pcg residual-gap suite version mismatch: {report}")
    _require(report.get("graphType") == "pcg", f"pcg residual-gap suite graphType mismatch: {report}")
    _require(report.get("suite") == "residual_gap", f"pcg residual-gap suite name mismatch: {report}")
    summary = report.get("summary")
    _require(summary == EXPECTED_PCG_RESIDUAL_GAP_SUITE_SUMMARY, f"pcg residual-gap suite summary mismatch: {summary}")
    cases = report.get("cases")
    _require(isinstance(cases, list) and len(cases) == EXPECTED_PCG_RESIDUAL_GAP_SUITE_SUMMARY["totalCases"], "pcg residual-gap suite cases mismatch")
    print("[PASS] generated PCG residual-gap suite validated")


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def _compact_json(value: Any, limit: int = 2000) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def is_tool_error_payload(payload: dict[str, Any]) -> bool:
    if bool(payload.get("isError")):
        return True
    domain_code = payload.get("domainCode")
    if isinstance(domain_code, str) and domain_code.strip():
        return True
    return False


class McpStdioClient:
    def __init__(self, project_root: Path, server_binary: Path, timeout_s: float) -> None:
        if not server_binary.exists():
            fail(f"loomle binary not found: {server_binary}")
        if not server_binary.is_file():
            fail(f"loomle binary path is not a file: {server_binary}")
        if not any(project_root.glob("*.uproject")):
            fail(f"no .uproject found under: {project_root}")

        self.proc = subprocess.Popen(
            [str(server_binary), "--project-root", str(project_root)],
            cwd=str(project_root),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.timeout_s = timeout_s
        self._stdout_queue: queue.Queue[str] = queue.Queue()
        self._reader_error: Exception | None = None
        self._reader_thread = threading.Thread(target=self._stdout_reader, name="loomle-mcp-stdout-reader", daemon=True)
        self._reader_thread.start()
        self._stderr_tail: deque[str] = deque(maxlen=200)
        self._stderr_reader_error: Exception | None = None
        self._stderr_lock = threading.Lock()
        self._stderr_thread = threading.Thread(target=self._stderr_reader, name="loomle-mcp-stderr-reader", daemon=True)
        self._stderr_thread.start()
        self._pending_responses: dict[int, dict[str, Any]] = {}
        self.protocol_version = ""
        self._initialize_session()

    def _stdout_reader(self) -> None:
        try:
            if self.proc.stdout is None:
                return
            for line in self.proc.stdout:
                self._stdout_queue.put(line)
        except Exception as exc:
            self._reader_error = exc

    def _stderr_reader(self) -> None:
        try:
            if self.proc.stderr is None:
                return
            for line in self.proc.stderr:
                text = line.rstrip()
                if not text:
                    continue
                with self._stderr_lock:
                    self._stderr_tail.append(text)
        except Exception as exc:
            self._stderr_reader_error = exc

    def _stderr_snapshot(self) -> str:
        with self._stderr_lock:
            if not self._stderr_tail:
                return ""
            return "\n".join(self._stderr_tail)

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
        if self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1)
        if self._stderr_thread.is_alive():
            self._stderr_thread.join(timeout=1)

    def _initialize_session(self) -> None:
        init_resp = self.request(
            1,
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "loomle-test-client", "version": "0"},
            },
        )
        result = init_resp.get("result")
        if not isinstance(result, dict):
            fail(f"initialize missing result object: {_compact_json(init_resp)}")
        protocol_version = result.get("protocolVersion")
        if not isinstance(protocol_version, str) or not protocol_version:
            fail(f"initialize did not return protocolVersion: {_compact_json(init_resp)}")
        self.protocol_version = protocol_version
        self.notify("notifications/initialized", {})

    def notify(self, method: str, params: dict[str, Any]) -> None:
        if self.proc.stdin is None:
            fail("loomle session stdin is not available")
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
        }
        self.proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()

    def request(self, req_id: int, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self.proc.stdin is None:
            fail("loomle session stdin is not available")

        pending = self._pending_responses.pop(req_id, None)
        if pending is not None:
            if isinstance(pending.get("error"), dict):
                fail(f"session error for {method}: {_compact_json(pending['error'])}")
            return pending

        payload = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params,
        }
        self.proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()

        deadline = time.time() + self.timeout_s
        while time.time() < deadline:
            if self.proc.poll() is not None:
                err = self._stderr_snapshot()
                fail(f"loomle session exited early: {err}")
            if self._reader_error is not None:
                fail(f"loomle session stdout reader failed: {self._reader_error}")
            if self._stderr_reader_error is not None:
                fail(f"loomle session stderr reader failed: {self._stderr_reader_error}")
            wait_s = max(0.0, deadline - time.time())
            try:
                line = self._stdout_queue.get(timeout=min(0.1, wait_s))
            except queue.Empty:
                continue

            line = line.strip()
            if not line:
                continue

            try:
                frame = json.loads(line)
            except json.JSONDecodeError:
                continue

            frame_id = frame.get("id")
            if frame_id is None:
                continue
            if frame_id != req_id:
                if isinstance(frame_id, int):
                    self._pending_responses[frame_id] = frame
                    while len(self._pending_responses) > 128:
                        self._pending_responses.pop(next(iter(self._pending_responses)))
                continue

            if isinstance(frame.get("error"), dict):
                fail(f"session error for {method}: {_compact_json(frame['error'])}")
            return frame

        stderr_tail = self._stderr_snapshot()
        if stderr_tail:
            fail(f"timeout waiting for {method} id={req_id}; recent stderr:\n{stderr_tail}")
        fail(f"timeout waiting for {method} id={req_id}")


def parse_tool_payload(response: dict[str, Any], method: str) -> dict[str, Any]:
    result = response.get("result")
    if not isinstance(result, dict):
        fail(f"Invalid {method} response: missing result object raw={_compact_json(response)}")

    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        payload = dict(structured)
        if "isError" not in payload and isinstance(result.get("isError"), bool):
            payload["isError"] = result["isError"]
        return payload

    content = result.get("content")
    if not isinstance(content, list) or not content:
        fail(f"Invalid {method} response: missing content raw={_compact_json(response)}")

    first = content[0]
    if not isinstance(first, dict):
        fail(f"Invalid {method} response: malformed content item raw={_compact_json(response)}")

    text = first.get("text")
    if not isinstance(text, str):
        fail(f"Invalid {method} response: missing text payload raw={_compact_json(response)}")

    try:
        payload = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"Invalid tool payload JSON for {method}: {exc} raw={_compact_json(response)}")

    return payload


def make_temp_asset_path(prefix: str) -> str:
    suffix = time.strftime("%Y%m%d_%H%M%S")
    return f"{prefix}_{suffix}"


def resolve_project_root(project_root_arg: str, dev_config_path_arg: str) -> Path:
    if project_root_arg:
        return Path(project_root_arg).resolve()

    default_path = TOOLS_DIR / "dev.project-root.local.json"
    config_path = Path(dev_config_path_arg).resolve() if dev_config_path_arg else default_path
    if not config_path.exists():
        fail(
            "missing --project-root and dev config not found. "
            f"expected config at {config_path}. copy tools/dev.project-root.example.json "
            "to tools/dev.project-root.local.json and set project_root."
        )

    try:
        raw = json.loads(config_path.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"failed to read dev config {config_path}: {exc}")

    value = raw.get("project_root") if isinstance(raw, dict) else None
    if not isinstance(value, str) or not value.strip():
        fail(f"invalid dev config {config_path}: missing string field 'project_root'")
    return Path(value).resolve()


def loomle_binary_name() -> str:
    binary_name = "loomle.exe" if sys.platform.startswith("win") else "loomle"
    return binary_name


def resolve_repo_loomle_binary() -> Path:
    return REPO_ROOT / "client" / "target" / "release" / loomle_binary_name()


def resolve_default_loomle_binary(project_root: Path) -> Path:
    _ = project_root
    candidate = resolve_repo_loomle_binary()
    if candidate.is_file():
        return candidate
    fail(
        "checkout loomle release binary not found: "
        f"{candidate}. run `cargo build --release` in client first, "
        "or pass --loomle-bin to override."
    )
    raise RuntimeError("unreachable")


def resolve_default_server_binary(project_root: Path) -> Path:
    return resolve_default_loomle_binary(project_root)


def resolve_default_client_binary(project_root: Path) -> Path:
    return resolve_default_loomle_binary(project_root)


def call_tool(
    client: McpStdioClient,
    req_id: int,
    name: str,
    arguments: dict[str, Any],
    expect_error: bool = False,
) -> dict[str, Any]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    payload = parse_tool_payload(response, f"tools/call.{name}")
    has_error = is_tool_error_payload(payload)
    if expect_error:
        if not has_error:
            fail(f"expected error for {name}, got payload={_compact_json(payload)} raw={_compact_json(response)}")
        return payload
    if has_error:
        fail(f"{name} failed payload={_compact_json(payload)} raw={_compact_json(response)}")
    return payload


def submit_execute_job(
    client: McpStdioClient,
    req_id: int,
    *,
    code: str,
    idempotency_key: str,
    label: str,
    wait_ms: int = 250,
    result_ttl_ms: int = 60000,
) -> dict[str, Any]:
    payload = call_tool(
        client,
        req_id,
        "execute",
        {
            "mode": "exec",
            "code": code,
            "execution": {
                "mode": "job",
                "idempotencyKey": idempotency_key,
                "label": label,
                "waitMs": wait_ms,
                "resultTtlMs": result_ttl_ms,
            },
        },
    )
    job = payload.get("job")
    if not isinstance(job, dict):
        fail(f"execute(job) missing job object: {_compact_json(payload)}")
    job_id = job.get("jobId")
    status = job.get("status")
    if not isinstance(job_id, str) or not job_id:
        fail(f"execute(job) missing jobId: {_compact_json(payload)}")
    if status not in {"queued", "running"}:
        fail(f"execute(job) unexpected initial status: {_compact_json(payload)}")
    if job.get("idempotencyKey") != idempotency_key:
        fail(f"execute(job) idempotencyKey mismatch: {_compact_json(payload)}")
    poll_after_ms = job.get("pollAfterMs")
    if not isinstance(poll_after_ms, int) or poll_after_ms <= 0:
        fail(f"execute(job) invalid pollAfterMs: {_compact_json(payload)}")
    return payload


def call_execute_exec_with_retry(
    client: McpStdioClient,
    req_id_base: int,
    code: str,
    max_attempts: int = 20,
    retry_delay_s: float = 1.0,
) -> dict[str, Any]:
    for attempt in range(1, max_attempts + 1):
        req_id = req_id_base + (attempt - 1)
        response = client.request(
            req_id,
            "tools/call",
            {"name": "execute", "arguments": {"mode": "exec", "code": code}},
        )
        payload = parse_tool_payload(response, "tools/call.execute")
        if not is_tool_error_payload(payload):
            return payload

        message = str(payload.get("message", ""))
        detail = str(payload.get("detail", ""))
        if "Python runtime is not initialized" in (message + " " + detail) and attempt < max_attempts:
            print(f"[WARN] execute waiting for Python runtime (attempt {attempt}/{max_attempts})...")
            time.sleep(retry_delay_s)
            continue

        fail(f"execute failed payload={_compact_json(payload)} raw={_compact_json(response)}")

    fail("execute retry loop ended without success")
    raise RuntimeError("unreachable")


def parse_execute_json(payload: dict[str, Any]) -> dict[str, Any]:
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

    fail(f"execute payload did not contain a JSON object result: {_compact_json(payload)}")
    raise RuntimeError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify LOOMLE bridge through loomle session")
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
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeVerify",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Override path to the loomle client binary. Defaults to client/target/release/loomle(.exe).",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.loomle_bin).resolve()
        if args.loomle_bin
        else resolve_default_loomle_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")

    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)

    try:
        print(f"[PASS] initialize protocol={client.protocol_version}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        tool_names = {
            tool.get("name") for tool in tools if isinstance(tool, dict) and isinstance(tool.get("name"), str)
        }
        missing = sorted(REQUIRED_TOOLS - tool_names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print(f"[PASS] tools/list includes required baseline tools ({len(REQUIRED_TOOLS)})")

        loomle_payload = call_tool(client, 3, "loomle", {})
        if loomle_payload.get("status") not in {"ok", "degraded"}:
            fail(f"loomle unexpected status: {loomle_payload}")
        rpc_health = loomle_payload.get("runtime", {}).get("rpcHealth", {})
        if rpc_health.get("status") not in {"ok", "degraded"}:
            fail(f"loomle rpc health not ready: {loomle_payload}")
        print("[PASS] loomle status query succeeded")

        diag_payload = call_tool(client, 31, "diag.tail", {"fromSeq": 0, "limit": 10})
        items = diag_payload.get("items")
        if not isinstance(items, list):
            fail(f"diag.tail missing items[]: {diag_payload}")
        next_seq = diag_payload.get("nextSeq")
        high_watermark = diag_payload.get("highWatermark")
        if not isinstance(next_seq, int) or next_seq < 0:
            fail(f"diag.tail invalid nextSeq: {diag_payload}")
        if not isinstance(high_watermark, int) or high_watermark < 0:
            fail(f"diag.tail invalid highWatermark: {diag_payload}")
        if not isinstance(diag_payload.get("hasMore"), bool):
            fail(f"diag.tail invalid hasMore: {diag_payload}")
        print("[PASS] diag.tail is available")

        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=4,
            code="import unreal\nunreal.log('loomle execute verify')",
        )
        print("[PASS] execute channel is available")

        jobs_smoke_key = f"jobs-smoke-{int(time.time() * 1000)}"
        jobs_smoke_payload = submit_execute_job(
            client,
            32,
            code=(
                "import time, unreal\n"
                "unreal.log('jobs smoke start')\n"
                "time.sleep(1.0)\n"
                "unreal.log('jobs smoke end')\n"
                "print('jobs-smoke-finished')\n"
            ),
            idempotency_key=jobs_smoke_key,
            label="jobs smoke",
            wait_ms=250,
        )
        jobs_smoke = jobs_smoke_payload["job"]
        jobs_smoke_id = jobs_smoke["jobId"]
        jobs_status = call_tool(client, 33, "jobs", {"action": "status", "jobId": jobs_smoke_id})
        if jobs_status.get("jobId") != jobs_smoke_id or jobs_status.get("tool") != "execute":
            fail(f"jobs.status smoke mismatch: {jobs_status}")
        if jobs_status.get("status") not in {"queued", "running", "succeeded", "failed"}:
            fail(f"jobs.status smoke invalid status: {jobs_status}")
        jobs_result = call_tool(client, 34, "jobs", {"action": "result", "jobId": jobs_smoke_id})
        if jobs_result.get("jobId") != jobs_smoke_id or jobs_result.get("tool") != "execute":
            fail(f"jobs.result smoke mismatch: {jobs_result}")
        if jobs_result.get("status") not in {"queued", "running", "succeeded", "failed"}:
            fail(f"jobs.result smoke invalid status: {jobs_result}")
        jobs_listing = call_tool(client, 35, "jobs", {"action": "list", "limit": 20})
        jobs = jobs_listing.get("jobs")
        if not isinstance(jobs, list):
            fail(f"jobs.list smoke missing jobs[]: {jobs_listing}")
        if not any(isinstance(entry, dict) and entry.get("jobId") == jobs_smoke_id and entry.get("tool") == "execute" for entry in jobs):
            fail(f"jobs.list smoke missing submitted job: {jobs_listing}")
        print("[PASS] jobs runtime smoke submission validated")

        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=50,
            code=(
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
        )
        print(f"[PASS] temporary blueprint created: {temp_asset}")

        blueprint_describe = call_tool(client, 6, "blueprint.asset.inspect", {"assetPath": temp_asset})
        if blueprint_describe.get("assetPath") != temp_asset:
            fail(f"blueprint.asset.inspect assetPath mismatch: {blueprint_describe}")
        if not isinstance(blueprint_describe.get("variables"), list):
            fail(f"blueprint.asset.inspect missing variables[]: {blueprint_describe}")
        if not isinstance(blueprint_describe.get("functions"), list):
            fail(f"blueprint.asset.inspect missing functions[]: {blueprint_describe}")
        if not isinstance(blueprint_describe.get("components"), list):
            fail(f"blueprint.asset.inspect missing components[]: {blueprint_describe}")

        material_describe = call_tool(
            client,
            7,
            "material.describe",
            {"nodeClass": "/Script/Engine.MaterialExpressionConstant"},
        )
        if material_describe.get("mode") != "class":
            fail(f"material.describe class mode mismatch: {material_describe}")
        if not isinstance(material_describe.get("inputPins"), list):
            fail(f"material.describe missing inputPins[]: {material_describe}")
        if not isinstance(material_describe.get("outputPins"), list):
            fail(f"material.describe missing outputPins[]: {material_describe}")
        if not isinstance(material_describe.get("properties"), list):
            fail(f"material.describe missing properties[]: {material_describe}")

        pcg_describe = call_tool(
            client,
            8,
            "pcg.describe",
            {"nodeClass": "/Script/PCG.PCGTransformPointsSettings"},
        )
        if pcg_describe.get("mode") != "class":
            fail(f"pcg.describe class mode mismatch: {pcg_describe}")
        if not isinstance(pcg_describe.get("inputPins"), list):
            fail(f"pcg.describe missing inputPins[]: {pcg_describe}")
        if not isinstance(pcg_describe.get("outputPins"), list):
            fail(f"pcg.describe missing outputPins[]: {pcg_describe}")
        if not isinstance(pcg_describe.get("properties"), list):
            fail(f"pcg.describe missing properties[]: {pcg_describe}")
        print("[PASS] domain describe class-mode smoke validated")

        validate_workspace_catalogs()
        validate_workspace_examples()
        validate_generated_blueprint_test_plan()
        validate_generated_blueprint_coverage_report()
        validate_generated_blueprint_workflow_truth_suite()
        validate_generated_blueprint_negative_boundary_suite()
        validate_generated_blueprint_stability_suite()
        validate_generated_blueprint_residual_gap_suite()
        validate_generated_blueprint_embedded_template_suite()
        validate_generated_material_test_plan()
        validate_generated_material_coverage_report()
        validate_generated_material_workflow_truth_suite()
        validate_generated_material_negative_boundary_suite()
        validate_generated_material_stability_suite()
        validate_generated_material_child_graph_ref_suite()
        validate_generated_pcg_test_plan()
        validate_generated_pcg_coverage_report()
        validate_generated_pcg_workflow_truth_suite()
        validate_generated_pcg_negative_boundary_suite()
        validate_generated_pcg_stability_suite()
        validate_generated_pcg_selector_truth_suite()
        validate_generated_pcg_effective_settings_suite()
        validate_generated_pcg_child_graph_ref_suite()
        validate_generated_pcg_residual_gap_suite()
        validate_generated_graph_test_surface_report()

        # --- widget.* smoke ---
        temp_wbp_asset = make_temp_asset_path("/Game/Codex/WBP_BridgeSmoke")
        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=200,
            code=(
                "import unreal, json\n"
                f"asset='{temp_wbp_asset}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.WidgetBlueprintFactory()\n"
                "wbp = asset_tools.create_asset(asset_name, pkg_path, unreal.WidgetBlueprint, factory)\n"
                "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                "print(json.dumps({'created': wbp is not None, 'exists': exists}, ensure_ascii=False))\n"
            ),
        )
        print(f"[PASS] temporary WidgetBlueprint created: {temp_wbp_asset}")

        wq = call_tool(client, 201, "widget.query", {"assetPath": temp_wbp_asset})
        if not isinstance(wq.get("assetPath"), str) or wq.get("assetPath") != temp_wbp_asset:
            fail(f"widget.query missing or wrong assetPath: {wq}")
        if not isinstance(wq.get("revision"), str) or not wq.get("revision"):
            fail(f"widget.query missing revision: {wq}")
        if not isinstance(wq.get("diagnostics"), list):
            fail(f"widget.query missing diagnostics[]: {wq}")
        print("[PASS] widget.query structure validated")

        wm = call_tool(client, 202, "widget.mutate", {
            "assetPath": temp_wbp_asset,
            "ops": [{"op": "addWidget", "args": {
                "widgetClass": "/Script/UMG.CanvasPanel",
                "name": "SmokeCanvas",
                "parent": "root",
            }}],
        })
        if not isinstance(wm.get("opResults"), list) or not wm.get("opResults"):
            fail(f"widget.mutate missing opResults: {wm}")
        first_op = wm["opResults"][0] if isinstance(wm["opResults"][0], dict) else {}
        if not first_op.get("ok"):
            fail(f"widget.mutate addWidget failed: {first_op}")
        if wm.get("newRevision") == wm.get("previousRevision"):
            fail(f"widget.mutate did not update revision: {wm}")
        print("[PASS] widget.mutate addWidget validated")

        wv = call_tool(client, 203, "widget.verify", {"assetPath": temp_wbp_asset})
        if wv.get("status") not in {"ok", "error"}:
            fail(f"widget.verify unexpected status: {wv}")
        if wv.get("assetPath") != temp_wbp_asset:
            fail(f"widget.verify wrong assetPath: {wv}")
        if not isinstance(wv.get("diagnostics"), list):
            fail(f"widget.verify missing diagnostics[]: {wv}")
        print("[PASS] widget.verify validated")

        print("[PASS] Bridge verification complete")
        return 0
    finally:
        if temp_asset:
            print(f"[WARN] cleanup skipped for temporary smoke blueprint asset: {temp_asset}")
        if temp_wbp_asset:
            print(f"[WARN] cleanup skipped for temporary smoke widget asset: {temp_wbp_asset}")
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
