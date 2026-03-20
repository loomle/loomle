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

REQUIRED_TOOLS = {
    "loomle",
    "graph",
    "graph.list",
    "graph.resolve",
    "graph.query",
    "graph.ops",
    "graph.ops.resolve",
    "graph.mutate",
    "graph.verify",
    "diag.tail",
    "context",
    "editor.open",
    "editor.focus",
    "editor.screenshot",
    "execute",
}

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
    "runScript",
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
            all(isinstance(op, dict) and op.get("op") in EXPECTED_GRAPH_MUTATE_OPS for op in ops),
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

    print("[PASS] workspace graph catalogs validated")


def validate_generated_pcg_test_plan() -> None:
    with tempfile.TemporaryDirectory(prefix="loomle-pcg-plan-") as tmpdir:
        output_path = Path(tmpdir) / "pcg_test_plan.json"
        subprocess.run(
            [
                sys.executable,
                str(TOOLS_DIR / "generate_graph_test_plan.py"),
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

        actor_source = entry_by_class.get("UPCGDataFromActorSettings")
        _require(isinstance(actor_source, dict), "pcg plan missing UPCGDataFromActorSettings")
        _require(actor_source.get("mode") == "recipe_case", f"pcg actor-source mode mismatch: {actor_source}")
        _require(actor_source.get("recipe") == "pcg_actor_source_context", f"pcg actor-source recipe mismatch: {actor_source}")
        _require(actor_source.get("fixture") == "pcg_graph_with_world_actor", f"pcg actor-source fixture mismatch: {actor_source}")
        _require(actor_source.get("status") == "ready", f"pcg actor-source status mismatch: {actor_source}")

        console_variable = entry_by_class.get("UPCGGetConsoleVariableSettings")
        _require(isinstance(console_variable, dict), "pcg plan missing UPCGGetConsoleVariableSettings")
        _require(console_variable.get("mode") == "auto_case", f"pcg console-variable mode mismatch: {console_variable}")
        _require(console_variable.get("fixture") == "pcg_graph", f"pcg console-variable fixture mismatch: {console_variable}")
        _require(console_variable.get("recipe") is None, f"pcg console-variable recipe mismatch: {console_variable}")
        _require(console_variable.get("status") == "ready", f"pcg console-variable status mismatch: {console_variable}")

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

        deprecated_grass = entry_by_class.get("UDEPRECATED_PCGGenerateGrassMapsSettings")
        _require(isinstance(deprecated_grass, dict), "pcg plan missing UDEPRECATED_PCGGenerateGrassMapsSettings")
        _require(deprecated_grass.get("mode") == "inventory", f"pcg deprecated grass mode mismatch: {deprecated_grass}")
        _require(deprecated_grass.get("status") == "inventory_only", f"pcg deprecated grass status mismatch: {deprecated_grass}")

        print("[PASS] generated PCG test plan validated")


def validate_generated_pcg_coverage_report() -> None:
    payload = subprocess.check_output(
        [
            sys.executable,
            str(TOOLS_DIR / "generate_graph_test_coverage_report.py"),
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
    message = payload.get("message")
    if isinstance(message, str) and message.strip():
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
            [str(server_binary), "--project-root", str(project_root), "session"],
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

    def request(self, req_id: int, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self.proc.stdin is None:
            fail("loomle session stdin is not available")

        pending = self._pending_responses.pop(req_id, None)
        if pending is not None:
            if not pending.get("ok", False):
                fail(f"session error for {method}: {pending.get('error')}")
            return pending

        payload = {
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
            if frame_id != req_id:
                if isinstance(frame_id, int):
                    self._pending_responses[frame_id] = frame
                    while len(self._pending_responses) > 128:
                        self._pending_responses.pop(next(iter(self._pending_responses)))
                continue

            if not frame.get("ok", False):
                fail(f"session error for {method}: {frame.get('error')}")
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
        return structured

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


def resolve_project_local_loomle_binary(project_root: Path) -> Path:
    return project_root / "Loomle" / loomle_binary_name()


def resolve_repo_loomle_binary() -> Path:
    return REPO_ROOT / "mcp" / "client" / "target" / "release" / loomle_binary_name()


def resolve_default_loomle_binary(project_root: Path) -> Path:
    candidate = resolve_project_local_loomle_binary(project_root)
    if candidate.is_file():
        return candidate
    fail(
        "project-local loomle binary not found: "
        f"{candidate}. install the current checkout into the test project first, "
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
        help="UE project root, e.g. /Users/xartest/dev/LoomleDevHost. If omitted, read from tools/dev.project-root.local.json",
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
        help="Override path to the loomle client binary. Defaults to <ProjectRoot>/Loomle/loomle(.exe).",
    )
    parser.add_argument(
        "--mcp-server-bin",
        dest="loomle_bin_compat",
        default="",
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.loomle_bin).resolve()
        if args.loomle_bin
        else Path(args.loomle_bin_compat).resolve()
        if args.loomle_bin_compat
        else resolve_default_loomle_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")

    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)

    try:
        init_resp = client.request(1, "initialize", {})
        protocol_version = init_resp.get("result", {}).get("protocolVersion")
        if not protocol_version:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol_version}")

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

        graph_desc_payload = call_tool(client, 6, "graph", {"graphType": "blueprint"})
        ops = graph_desc_payload.get("ops")
        if not isinstance(ops, list):
            fail("graph payload missing ops[]")
        ops_set = {op for op in ops if isinstance(op, str)}
        if ops_set != EXPECTED_GRAPH_MUTATE_OPS:
            fail(f"graph ops mismatch. expected={sorted(EXPECTED_GRAPH_MUTATE_OPS)} actual={sorted(ops_set)}")
        print("[PASS] graph reports expected mutate ops")

        run_script_args = {
            "graphType": "blueprint",
            "assetPath": temp_asset,
            "graphName": "EventGraph",
            "ops": [
                {
                    "op": "runScript",
                    "args": {
                        "mode": "inlineCode",
                        "entry": "run",
                        "code": "def run(ctx):\n  return {'ok': True, 'assetPath': ctx.get('assetPath', '')}",
                        "input": {"source": "verify_bridge"},
                    },
                }
            ],
        }
        run_script_payload: dict[str, Any] | None = None
        for attempt in range(1, 4):
            payload = call_tool(client, 7, "graph.mutate", run_script_args)
            op_results = payload.get("opResults")
            if isinstance(op_results, list) and op_results:
                first_op = op_results[0] if isinstance(op_results[0], dict) else {}
                script_result = first_op.get("scriptResult")
                if first_op.get("ok") and isinstance(script_result, dict) and script_result.get("ok") is True:
                    run_script_payload = payload
                    break
            if attempt < 3:
                print(f"[WARN] graph.mutate runScript response incomplete (attempt {attempt}/3), retrying...")
                time.sleep(0.3)
                continue
            fail(f"graph.mutate runScript invalid payload={_compact_json(payload)}")
        if run_script_payload is None:
            fail("graph.mutate runScript retry loop ended without payload")
        print("[PASS] graph.mutate runScript inline execution verified")

        validate_workspace_catalogs()
        validate_workspace_examples()
        validate_generated_pcg_test_plan()
        validate_generated_pcg_coverage_report()

        print("[PASS] Bridge verification complete")
        return 0
    finally:
        try:
            _ = client.request(
                99,
                "tools/call",
                {
                    "name": "execute",
                    "arguments": {
                        "mode": "exec",
                        "code": (
                            "import unreal\n"
                            f"asset='{temp_asset}'\n"
                            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                            "  unreal.EditorAssetLibrary.delete_asset(asset)\n"
                        ),
                    },
                },
            )
        except Exception:
            pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
