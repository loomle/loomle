from __future__ import annotations

import json
from typing import Any


class TransformError(ValueError):
    pass


def apply_args_transform(transform: Any, arguments: dict[str, Any]) -> dict[str, Any]:
    if transform in (None, "identity"):
        return dict(arguments)
    if not isinstance(transform, dict):
        raise TransformError(f"Unsupported args transform: {transform!r}")

    name = transform.get("transform")
    if name == "material.palette.args.v1":
        return asset_or_graph_args(
            arguments,
            "material.palette",
            copy_fields=["query", "elementTypes", "limit", "offset"],
        )
    if name == "pcg.palette.args.v1":
        return asset_or_graph_args(
            arguments,
            "pcg.palette",
            copy_fields=["query", "elementTypes", "limit", "offset"],
        )
    if name == "pcg.parameter.inspect.args.v1":
        return asset_or_graph_args(
            arguments,
            "pcg.parameter.inspect",
            copy_fields=["name"],
        )
    if name == "pcg.parameter.edit.args.v1":
        transformed = asset_or_graph_args(arguments, "pcg.parameter.edit", copy_fields=[])
        operation = string_field(arguments, "operation")
        if operation is None:
            raise TransformError("pcg.parameter.edit requires operation.")
        args = arguments.get("args")
        if not isinstance(args, dict):
            raise TransformError("pcg.parameter.edit requires args.")
        transformed["operation"] = operation
        transformed["args"] = args
        copy_mutation_controls(arguments, transformed)
        return transformed
    if name == "pcg.compile.args.v1":
        return asset_or_graph_args(arguments, "pcg.compile", copy_fields=[])
    if name == "pcg.graph.inspect.args.v1":
        return pcg_graph_inspect_args(arguments)
    if name == "pcg.node.inspect.args.v1":
        return pcg_node_inspect_args(arguments)
    if name == "pcg.graph.layout.args.v1":
        return selection_graph_layout_args(arguments, "pcg.graph.layout")
    if name == "pcg.graph.edit.args.v1":
        return pcg_graph_edit_args(arguments)
    if name == "material.graph.inspect.args.v1":
        return asset_or_graph_args(
            arguments,
            "material.graph.inspect",
            copy_fields=["graphName", "nodeIds", "nodeClasses", "includeConnections"],
        )
    if name == "material.graph.layout.args.v1":
        return selection_graph_layout_args(arguments, "material.graph.layout")
    if name == "material.graph.edit.args.v1":
        return material_graph_edit_args(arguments)
    if name == "material.node.edit.args.v1":
        return material_node_edit_args(arguments)
    if name == "widget.tree.inspect.args.v1":
        transformed: dict[str, Any] = {}
        copy_if_present(arguments, transformed, "assetPath")
        view = arguments.get("view")
        transformed["includeSlotProperties"] = view in ("layout", "details")
        return transformed
    if name == "widget.inspect.args.v1":
        transformed = {}
        copy_if_present(arguments, transformed, "widgetClass")
        copy_if_present(arguments, transformed, "assetPath")
        widget = arguments.get("widget")
        if isinstance(widget, dict) and isinstance(widget.get("name"), str):
            transformed["widgetName"] = widget["name"]
        copy_if_present(arguments, transformed, "widgetName")
        if "widgetClass" not in transformed and not (
            "assetPath" in transformed and "widgetName" in transformed
        ):
            raise TransformError(
                "widget.inspect requires widgetClass, or assetPath plus widget.name."
            )
        return transformed
    if name == "widget.tree.edit.args.v1":
        return widget_tree_edit_args(arguments)
    if name == "asset.create.args.v1":
        return asset_create_args(arguments)
    if name == "asset.inspect.args.v1":
        return asset_inspect_args(arguments)
    if name == "asset.edit.args.v1":
        return asset_edit_args(arguments)
    if name == "blueprint.graph.inspect.args.v1":
        return blueprint_graph_inspect_args(arguments)
    if name == "blueprint.node.inspect.args.v1":
        return blueprint_node_inspect_args(arguments)
    if name == "blueprint.node.edit.args.v1":
        return blueprint_node_edit_args(arguments)
    if name == "blueprint.palette.args.v1":
        return blueprint_palette_args(arguments)
    if name == "blueprint.compile.args.v1":
        return blueprint_compile_args(arguments)
    if name == "blueprint.graph.layout.args.v1":
        return blueprint_graph_layout_args(arguments)
    if name == "blueprint.graph.edit.args.v1":
        return blueprint_graph_edit_args(arguments)

    raise TransformError(f"Unsupported args transform: {name!r}")


def apply_result_transform(
    transform: Any,
    payload: dict[str, Any],
    original_arguments: dict[str, Any],
) -> dict[str, Any]:
    if transform in (None, "structured"):
        return payload
    if not isinstance(transform, dict):
        raise TransformError(f"Unsupported result transform: {transform!r}")

    name = transform.get("transform")
    if name == "pcg.compile.result.v1":
        return shape_pcg_compile_result(payload)
    if name == "pcg.graph.inspect.result.v1":
        return shape_pcg_graph_inspect_result(payload, original_arguments)
    if name == "pcg.node.inspect.result.v1":
        return shape_pcg_node_inspect_result(payload)
    if name == "blueprint.inspect.result.v1":
        return shape_blueprint_inspect_result(payload)
    if name == "blueprint.class.inspect.result.v1":
        return shape_blueprint_class_inspect_result(payload)
    if name == "blueprint.member.inspect.result.v1":
        return shape_blueprint_member_inspect_result(payload, original_arguments)
    if name == "blueprint.graph.inspect.result.v1":
        return shape_blueprint_graph_inspect_result(payload, original_arguments)
    if name == "blueprint.graph.edit.result.v1":
        return augment_blueprint_graph_edit_result(payload)
    if name == "blueprint.compile.result.v1":
        return shape_blueprint_compile_result(payload)
    if name == "widget.tree.inspect.result.v1":
        return shape_widget_tree_inspect_payload(payload, original_arguments)
    raise TransformError(f"Unsupported result transform: {name!r}")


def asset_or_graph_args(
    arguments: dict[str, Any],
    tool_name: str,
    *,
    copy_fields: list[str],
) -> dict[str, Any]:
    asset_path = graph_asset_path(arguments) or string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError(f"{tool_name} requires assetPath or graph.")
    transformed: dict[str, Any] = {"assetPath": asset_path}
    copy_if_present(arguments, transformed, "__bridgeTool")
    for field in copy_fields:
        copy_if_present(arguments, transformed, field)
    return transformed


def graph_asset_path(arguments: dict[str, Any]) -> str | None:
    graph = arguments.get("graph")
    if isinstance(graph, dict):
        value = graph.get("assetPath")
        if isinstance(value, str) and value:
            return value
    graph_ref = arguments.get("graphRef")
    if isinstance(graph_ref, dict):
        value = graph_ref.get("assetPath")
        if isinstance(value, str) and value:
            return value
    return None


def string_field(arguments: dict[str, Any], field: str) -> str | None:
    value = arguments.get(field)
    if isinstance(value, str) and value:
        return value
    return None


def copy_if_present(source: dict[str, Any], target: dict[str, Any], field: str) -> None:
    if field in source:
        target[field] = source[field]


def copy_mutation_controls(source: dict[str, Any], target: dict[str, Any]) -> None:
    for field in [
        "expectedRevision",
        "idempotencyKey",
        "dryRun",
        "returnDiff",
        "returnDiagnostics",
    ]:
        copy_if_present(source, target, field)


def asset_create_args(arguments: dict[str, Any]) -> dict[str, Any]:
    kind = string_field(arguments, "kind")
    asset_path = string_field(arguments, "assetPath")
    if kind is None:
        raise TransformError("asset.create requires kind.")
    if asset_path is None:
        raise TransformError("asset.create requires assetPath.")
    if kind == "blueprint":
        args: dict[str, Any] = {"__bridgeTool": "blueprint.class.edit", "assetPath": asset_path, "operation": "create"}
        op_args: dict[str, Any] = {}
        parent_class = string_field(arguments, "parentClassPath") or string_field(arguments, "parentClass")
        if parent_class is not None:
            op_args["parentClassPath"] = parent_class
        args["args"] = op_args
        copy_mutation_controls(arguments, args)
        return args
    if kind == "enum":
        args = {"__bridgeTool": "blueprint.enum.edit", "assetPath": asset_path, "operation": "create", "args": enum_args(arguments)}
        copy_mutation_controls(arguments, args)
        return args
    if kind == "userDefinedStruct":
        args = {"__bridgeTool": "blueprint.struct.edit", "assetPath": asset_path, "operation": "create", "args": struct_args(arguments)}
        copy_mutation_controls(arguments, args)
        return args
    if kind in ("material", "materialFunction", "pcgGraph", "widgetBlueprint"):
        return dict(arguments)
    raise TransformError(f"Unsupported asset.create kind: {kind}.")


def asset_inspect_args(arguments: dict[str, Any]) -> dict[str, Any]:
    kind = string_field(arguments, "kind")
    if kind is None:
        raise TransformError("asset.inspect requires kind.")
    args = dict(arguments)
    if kind == "blueprint":
        args["__bridgeTool"] = "blueprint.inspect"
        return args
    if kind == "enum":
        args["__bridgeTool"] = "blueprint.enum.inspect"
        return args
    if kind == "userDefinedStruct":
        args["__bridgeTool"] = "blueprint.struct.inspect"
        return args
    args.pop("kind", None)
    if kind in ("material", "materialFunction"):
        args["__bridgeTool"] = "material.graph.inspect"
        return asset_or_graph_args(args, "asset.inspect", copy_fields=["nodeIds", "nodeClasses", "includeConnections"])
    if kind == "pcgGraph":
        transformed = pcg_graph_inspect_args(args)
        transformed["__bridgeTool"] = "pcg.graph.inspect"
        return transformed
    if kind == "widgetBlueprint":
        transformed = apply_args_transform({"transform": "widget.tree.inspect.args.v1"}, args)
        transformed["__bridgeTool"] = "widget.tree.inspect"
        return transformed
    raise TransformError(f"Unsupported asset.inspect kind: {kind}.")


def asset_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    operation = string_field(arguments, "operation")
    if operation is None:
        raise TransformError("asset.edit requires operation.")
    if operation == "updateMetadata":
        return dict(arguments)
    kind = string_field(arguments, "kind")
    if kind is None:
        raise TransformError(f"asset.edit operation {operation} requires kind.")
    if kind == "enum" and operation == "updateEntries":
        asset_path = string_field(arguments, "assetPath")
        if asset_path is None:
            raise TransformError("asset.edit requires assetPath.")
        args = {"__bridgeTool": "blueprint.enum.edit", "assetPath": asset_path, "operation": "updateEntries", "args": enum_args(arguments)}
        copy_mutation_controls(arguments, args)
        return args
    if kind == "userDefinedStruct" and operation in {
        "setTooltip",
        "addField",
        "removeField",
        "renameField",
        "changeFieldType",
        "setFieldDefault",
        "setFieldTooltip",
        "setFieldMetadata",
        "moveField",
    }:
        asset_path = string_field(arguments, "assetPath")
        if asset_path is None:
            raise TransformError("asset.edit requires assetPath.")
        args = {"__bridgeTool": "blueprint.struct.edit", "assetPath": asset_path, "operation": operation, "args": struct_args(arguments)}
        copy_mutation_controls(arguments, args)
        return args
    raise TransformError(f"Unsupported asset.edit operation for kind {kind}: {operation}.")


def enum_args(arguments: dict[str, Any]) -> dict[str, Any]:
    args = arguments.get("args")
    out = dict(args) if isinstance(args, dict) else {}
    copy_if_present(arguments, out, "entries")
    copy_if_present(arguments, out, "displayNames")
    return out


def struct_args(arguments: dict[str, Any]) -> dict[str, Any]:
    args = arguments.get("args")
    out = dict(args) if isinstance(args, dict) else {}
    for field in (
        "fields",
        "tooltip",
        "toolTip",
        "fieldId",
        "id",
        "name",
        "fieldName",
        "newName",
        "displayName",
        "type",
        "defaultValue",
        "value",
        "metadata",
        "removeKeys",
        "relativeToFieldId",
        "targetFieldId",
        "position",
    ):
        copy_if_present(arguments, out, field)
    return out


def blueprint_graph_address(arguments: dict[str, Any], asset_path: str, tool_name: str, required: bool = True) -> tuple[str | None, dict[str, Any] | None]:
    graph = arguments.get("graph")
    if not isinstance(graph, dict):
        if required:
            raise TransformError(f"{tool_name} requires graph.")
        return None, None
    graph_id = graph.get("id")
    if isinstance(graph_id, str) and graph_id:
        return None, {"kind": "asset", "assetPath": asset_path, "graphId": graph_id}
    graph_name = graph.get("name")
    if isinstance(graph_name, str) and graph_name:
        return graph_name, None
    if required:
        raise TransformError(f"{tool_name} graph requires id or name.")
    return None, None


def write_graph_address(target: dict[str, Any], graph_name: str | None, graph_ref: dict[str, Any] | None) -> None:
    if graph_name is not None:
        target["graphName"] = graph_name
    if graph_ref is not None:
        target["graphRef"] = graph_ref


def blueprint_graph_inspect_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("blueprint.graph.inspect requires assetPath.")
    graph_name, graph_ref = blueprint_graph_address(arguments, asset_path, "blueprint.graph.inspect")
    out: dict[str, Any] = {"assetPath": asset_path}
    write_graph_address(out, graph_name, graph_ref)
    out["includeConnections"] = True
    out["limit"] = 10000
    return out


def blueprint_node_inspect_args(arguments: dict[str, Any]) -> dict[str, Any]:
    out = blueprint_graph_inspect_args(arguments)
    node = arguments.get("node")
    if not isinstance(node, dict) or not isinstance(node.get("id"), str):
        raise TransformError("blueprint.node.inspect requires node.id.")
    out["nodeId"] = node["id"]
    for field in ["view", "filter", "page", "includeConnections", "limit", "cursor"]:
        out.pop(field, None)
    return out


def blueprint_node_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    out = blueprint_node_inspect_args(arguments)
    operation = string_field(arguments, "operation")
    if operation is None:
        raise TransformError("blueprint.node.edit requires operation.")
    out["operation"] = operation
    out["args"] = arguments.get("args", {})
    copy_mutation_controls(arguments, out)
    return out


def blueprint_palette_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("blueprint.palette requires assetPath.")
    out: dict[str, Any] = {"assetPath": asset_path}
    graph_name, graph_ref = blueprint_graph_address(arguments, asset_path, "blueprint.palette", required=False)
    write_graph_address(out, graph_name, graph_ref)
    for field in ["query", "contextSensitive", "fromPins", "limit", "offset"]:
        copy_if_present(arguments, out, field)
    return out


def blueprint_compile_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("blueprint.compile requires assetPath.")
    out: dict[str, Any] = {"assetPath": asset_path}
    graph_name, graph_ref = blueprint_graph_address(arguments, asset_path, "blueprint.compile", required=False)
    if graph_name is None and graph_ref is None:
        graph_name = "EventGraph"
    write_graph_address(out, graph_name, graph_ref)
    for field in ["limit", "cursor", "layoutDetail"]:
        copy_if_present(arguments, out, field)
    return out


def blueprint_graph_layout_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("blueprint.graph.layout requires assetPath.")
    graph_name, graph_ref = blueprint_graph_address(arguments, asset_path, "blueprint.graph.layout")
    scope = arguments.get("scope")
    if not isinstance(scope, dict):
        raise TransformError("blueprint.graph.layout requires scope.")
    mode = scope.get("mode")
    if mode != "selection":
        raise TransformError("Python MCP currently supports blueprint.graph.layout scope.mode='selection'.")
    nodes = scope.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        raise TransformError("scope.nodes must contain at least one node.")
    node_ids = []
    for index, node in enumerate(nodes):
        if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
            raise TransformError(f"scope.nodes[{index}] requires id.")
        node_ids.append(node["id"])
    out: dict[str, Any] = {"assetPath": asset_path}
    write_graph_address(out, graph_name, graph_ref)
    out["ops"] = [{"op": "layoutGraph", "scope": "selection", "nodeIds": node_ids}]
    for field in ["expectedRevision", "dryRun"]:
        copy_if_present(arguments, out, field)
    return out


def blueprint_graph_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("blueprint.graph.edit requires assetPath.")
    out: dict[str, Any] = {"assetPath": asset_path}
    graph_name, graph_ref = blueprint_graph_address(arguments, asset_path, "blueprint.graph.edit", required=False)
    write_graph_address(out, graph_name, graph_ref)
    commands = arguments.get("commands")
    if not isinstance(commands, list):
        raise TransformError("blueprint.graph.edit requires commands.")
    out["ops"] = [op for command in commands for op in compile_blueprint_graph_command(command)]
    for field in ["expectedRevision", "idempotencyKey", "dryRun", "continueOnError"]:
        copy_if_present(arguments, out, field)
    return out


def compile_blueprint_graph_command(command: Any) -> list[dict[str, Any]]:
    if not isinstance(command, dict):
        raise TransformError("graph command must be an object.")
    kind = string_field(command, "kind")
    if kind == "addFromPalette":
        entry = command.get("entry")
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            raise TransformError("addFromPalette requires entry.id.")
        args: dict[str, Any] = {"entryId": entry["id"], "entry": entry}
        for field in ["position", "anchor", "from", "fromPins", "contextSensitive"]:
            copy_if_present(command, args, field)
        if "contextSensitive" not in args and isinstance(entry.get("contextSensitive"), bool):
            args["contextSensitive"] = entry["contextSensitive"]
        op: dict[str, Any] = {"op": "addFromPalette", "args": args}
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        ops = [op]
        defaults = command.get("defaults")
        if isinstance(defaults, list) and alias is not None:
            for default in defaults:
                if not isinstance(default, dict):
                    continue
                pin = default.get("pin") or default.get("pinName")
                if isinstance(pin, str):
                    ops.append({"op": "setPinDefault", "args": {"target": {"nodeRef": alias, "pin": pin}, "value": json_string_value(default.get("value"))}})
        return ops
    if kind == "removeNode":
        return [{"op": "removeNode", "args": node_ref_token_required(command, "node")}]
    if kind == "reconstructNode":
        args = node_ref_token_required(command, "node")
        args["preserveLinks"] = bool(command.get("preserveLinks", True))
        return [{"op": "reconstructNode", "args": args}]
    if kind == "duplicateNode":
        args = node_ref_token_required(command, "node")
        offset = command.get("offset")
        if isinstance(offset, dict):
            if "x" in offset:
                args["dx"] = offset["x"]
            if "y" in offset:
                args["dy"] = offset["y"]
        op = {"op": "duplicateNode", "args": args}
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        return [op]
    if kind == "moveNode":
        op = compile_move_node(command)
        args = {k: v for k, v in op.items() if k != "op"}
        return [{"op": op["op"], "args": args}]
    if kind in ("connect", "disconnect"):
        return [{"op": "connectPins" if kind == "connect" else "disconnectPins", "args": {"from": pin_endpoint(command.get("from")), "to": pin_endpoint(command.get("to"))}}]
    if kind == "breakLinks":
        return [{"op": "breakPinLinks", "args": {"target": pin_endpoint(command.get("target"))}}]
    if kind == "setPinDefault":
        if "value" not in command:
            raise TransformError("setPinDefault requires value.")
        return [{"op": "setPinDefault", "args": {"target": pin_endpoint(command.get("target")), "value": json_string_value(command["value"])}}]
    if kind == "setNodeComment":
        args = node_ref_token_required(command, "node")
        args["comment"] = command.get("comment") if isinstance(command.get("comment"), str) else ""
        return [{"op": "setNodeComment", "args": args}]
    if kind == "setNodeEnabled":
        args = node_ref_token_required(command, "node")
        args["enabled"] = bool(command.get("enabled", True))
        return [{"op": "setNodeEnabled", "args": args}]
    if kind == "addReroute":
        args = {}
        copy_if_present(command, args, "position")
        op = {"op": "addReroute", "args": args}
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        return [op]
    if kind == "addCommentBox":
        args = {}
        bounds = command.get("bounds")
        if isinstance(bounds, dict):
            args["position"] = {"x": bounds.get("x", 0), "y": bounds.get("y", 0)}
            copy_if_present(bounds, args, "w")
            if "w" in args:
                args["width"] = args.pop("w")
            copy_if_present(bounds, args, "h")
            if "h" in args:
                args["height"] = args.pop("h")
        if isinstance(command.get("text"), str):
            args["text"] = command["text"]
        op = {"op": "addCommentBox", "args": args}
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        return [op]
    raise TransformError(f"Unsupported graph.edit command kind: {kind}")


def node_ref_token_required(command: dict[str, Any], field: str) -> dict[str, Any]:
    node = command.get(field)
    if not isinstance(node, dict):
        raise TransformError(f"{field} is required.")
    return node_ref_token(node)


def material_node_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    transformed = asset_or_graph_args(arguments, "material.node.edit", copy_fields=[])
    node = arguments.get("node")
    if not isinstance(node, dict):
        raise TransformError("material.node.edit requires node.")
    property_name = string_field(arguments, "property")
    if property_name is None:
        raise TransformError("material.node.edit requires property.")
    if "value" not in arguments:
        raise TransformError("material.node.edit requires value.")
    op = node_ref_token(node)
    op.update({"op": "setProperty", "property": property_name, "value": arguments["value"]})
    transformed["ops"] = [op]
    copy_mutation_controls(arguments, transformed)
    return transformed


def material_graph_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    transformed = asset_or_graph_args(arguments, "material.graph.edit", copy_fields=[])
    commands = arguments.get("commands")
    if not isinstance(commands, list):
        raise TransformError("material.graph.edit requires commands.")
    transformed["ops"] = [op for command in commands for op in compile_material_graph_command(command)]
    for field in ["expectedRevision", "idempotencyKey", "dryRun", "continueOnError"]:
        copy_if_present(arguments, transformed, field)
    return transformed


def pcg_graph_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    transformed = asset_or_graph_args(arguments, "pcg.graph.edit", copy_fields=[])
    commands = arguments.get("commands")
    if not isinstance(commands, list):
        raise TransformError("pcg.graph.edit requires commands.")
    transformed["ops"] = [op for command in commands for op in compile_pcg_graph_command(command)]
    for field in ["expectedRevision", "idempotencyKey", "dryRun", "continueOnError"]:
        copy_if_present(arguments, transformed, field)
    return transformed


def compile_material_graph_command(command: Any) -> list[dict[str, Any]]:
    if not isinstance(command, dict):
        raise TransformError("material.graph.edit command must be an object.")
    kind = string_field(command, "kind")
    if kind == "addFromPalette":
        entry = command.get("entry")
        if not isinstance(entry, dict):
            raise TransformError("addFromPalette requires entry from material.palette.")
        payload = entry.get("payload")
        if not isinstance(payload, dict) or not isinstance(payload.get("nodeClassPath"), str):
            raise TransformError("material.palette entry.payload requires nodeClassPath.")
        op: dict[str, Any] = {
            "op": "addNode.byClass",
            "nodeClassPath": payload["nodeClassPath"],
            "entryId": entry.get("id"),
            "entry": entry,
        }
        copy_position(command, op)
        for field in ["anchor", "near", "from", "target", "parameterName"]:
            copy_if_present(command, op, field)
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        return [op]
    if kind == "removeNode":
        node = command.get("node")
        if not isinstance(node, dict):
            raise TransformError("removeNode requires node.")
        op = node_ref_token(node)
        op["op"] = "removeNode"
        return [op]
    if kind == "moveNode":
        return [compile_move_node(command)]
    if kind in ("connect", "disconnect"):
        return [{
            "op": "connectPins" if kind == "connect" else "disconnectPins",
            "from": pin_endpoint(command.get("from")),
            "to": pin_endpoint(command.get("to")),
        }]
    if kind == "breakPinLinks":
        return [{"op": "breakPinLinks", "target": pin_endpoint(command.get("target"))}]
    raise TransformError(f"Unsupported material.graph.edit command kind: {kind}")


def compile_pcg_graph_command(command: Any) -> list[dict[str, Any]]:
    if not isinstance(command, dict):
        raise TransformError("pcg.graph.edit command must be an object.")
    kind = string_field(command, "kind")
    if kind == "addFromPalette":
        entry = command.get("entry")
        if not isinstance(entry, dict):
            raise TransformError("addFromPalette requires entry from pcg.palette.")
        if entry.get("executable") is False:
            raise TransformError(f"pcg.palette entry is not executable: {entry.get('id')}")
        op: dict[str, Any] = {"op": "addFromPalette", "entryId": entry.get("id"), "entry": entry}
        copy_position(command, op)
        for field in ["anchor", "near", "from", "target", "behavior"]:
            copy_if_present(command, op, field)
        alias = string_field(command, "alias")
        if alias is not None:
            op["clientRef"] = alias
        return [op]
    if kind == "removeNode":
        node = command.get("node")
        if not isinstance(node, dict):
            raise TransformError("removeNode requires node.")
        op = node_ref_token(node)
        op["op"] = "removeNode"
        return [op]
    if kind == "moveNode":
        return [compile_move_node(command)]
    if kind in ("connect", "disconnect"):
        conversion_policy = command.get("conversionPolicy")
        if isinstance(conversion_policy, str) and conversion_policy != "strict":
            raise TransformError("pcg.graph.edit connect currently supports conversionPolicy='strict' only.")
        return [{
            "op": "connectPins" if kind == "connect" else "disconnectPins",
            "from": pin_endpoint(command.get("from")),
            "to": pin_endpoint(command.get("to")),
        }]
    if kind == "setPinDefault":
        if "value" not in command:
            raise TransformError("setPinDefault requires value.")
        return [{"op": "setPinDefault", "target": pin_endpoint(command.get("target")), "value": json_string_value(command["value"])}]
    if kind == "setNodeProperty":
        node = command.get("node")
        if not isinstance(node, dict):
            raise TransformError("setNodeProperty requires node.")
        property_name = string_field(command, "property")
        if property_name is None or "value" not in command:
            raise TransformError("setNodeProperty requires property and value.")
        op = node_ref_token(node)
        op.update({"op": "setProperty", "property": property_name, "value": json_string_value(command["value"])})
        return [op]
    raise TransformError(f"Unsupported pcg.graph.edit command kind: {kind}")


def compile_move_node(command: dict[str, Any]) -> dict[str, Any]:
    node = command.get("node")
    if not isinstance(node, dict):
        raise TransformError("moveNode requires node.")
    op = node_ref_token(node)
    position = command.get("position")
    delta = command.get("delta")
    if isinstance(position, dict):
        op["op"] = "moveNode"
        copy_if_present(position, op, "x")
        copy_if_present(position, op, "y")
    elif isinstance(delta, dict):
        op["op"] = "moveNodeBy"
        if "x" in delta:
            op["dx"] = delta["x"]
        if "y" in delta:
            op["dy"] = delta["y"]
    else:
        raise TransformError("moveNode requires position or delta.")
    return op


def copy_position(source: dict[str, Any], target: dict[str, Any]) -> None:
    position = source.get("position")
    if isinstance(position, dict):
        copy_if_present(position, target, "x")
        copy_if_present(position, target, "y")


def pin_endpoint(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise TransformError("pin endpoint must be an object.")
    node = value.get("node")
    if not isinstance(node, dict):
        raise TransformError("pin endpoint requires node.")
    out = node_ref_token(node)
    pin = value.get("pin") or value.get("pinName")
    if not isinstance(pin, str) or not pin:
        raise TransformError("pin endpoint requires pin.")
    out["pinName"] = pin
    return out


def json_string_value(value: Any) -> str:
    if isinstance(value, str):
        return value
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def pcg_graph_inspect_args(arguments: dict[str, Any]) -> dict[str, Any]:
    transformed = asset_or_graph_args(arguments, "pcg.graph.inspect", copy_fields=[])
    filter_value = arguments.get("filter")
    if isinstance(filter_value, dict):
        copy_if_present(filter_value, transformed, "nodeIds")
    view = arguments.get("view", "overview")
    if view in ("links", "defaults", "full"):
        transformed["includeConnections"] = True
    return transformed


def pcg_node_inspect_args(arguments: dict[str, Any]) -> dict[str, Any]:
    node_class = string_field(arguments, "nodeClass") or string_field(arguments, "settingsClass")
    if node_class is not None:
        return {"nodeClass": node_class}
    transformed = asset_or_graph_args(arguments, "pcg.node.inspect", copy_fields=[])
    node = arguments.get("node")
    node_id = node.get("id") if isinstance(node, dict) else arguments.get("nodeId")
    if not isinstance(node_id, str) or not node_id:
        raise TransformError("pcg.node.inspect requires node.id.")
    transformed["nodeId"] = node_id
    return transformed


def selection_graph_layout_args(arguments: dict[str, Any], tool_name: str) -> dict[str, Any]:
    transformed = asset_or_graph_args(arguments, tool_name, copy_fields=[])
    scope = arguments.get("scope")
    if not isinstance(scope, dict) or scope.get("mode") != "selection":
        raise TransformError(f"{tool_name} supports only scope.mode='selection'.")
    nodes = scope.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        raise TransformError("scope.nodes must contain at least one node.")
    node_ids = []
    for index, node in enumerate(nodes):
        if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
            raise TransformError(f"scope.nodes[{index}] requires id.")
        node_ids.append(node["id"])
    transformed["ops"] = [{"op": "layoutGraph", "scope": "selection", "nodeIds": node_ids}]
    for field in ["expectedRevision", "dryRun"]:
        copy_if_present(arguments, transformed, field)
    return transformed


def shape_pcg_compile_result(payload: dict[str, Any]) -> dict[str, Any]:
    status = payload.get("status") if isinstance(payload.get("status"), str) else "error"
    compile_report = payload.get("compileReport")
    if not isinstance(compile_report, dict):
        compile_report = {}
    compiled = compile_report.get("compiled")
    if not isinstance(compiled, bool):
        compiled = status == "ok"
    return {
        "assetPath": payload.get("assetPath"),
        "status": status,
        "valid": status == "ok" and compiled,
        "compiled": compiled,
        "summary": payload.get("summary"),
        "diagnostics": payload.get("diagnostics", []),
        "compileReport": compile_report,
        "queryReport": payload.get("queryReport"),
    }


def shape_blueprint_inspect_result(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "assetPath": payload.get("assetPath"),
        "blueprintClass": payload.get("blueprintClass"),
        "parentClass": payload.get("parentClass"),
        "parentClassPath": payload.get("parentClassPath"),
        "implementedInterfaces": payload.get("implementedInterfaces", []),
        "variables": payload.get("variables", []),
        "functions": payload.get("functions", []),
        "interfaceFunctions": payload.get("interfaceFunctions", []),
        "macros": payload.get("macros", []),
        "dispatchers": payload.get("dispatchers", []),
        "eventSignatures": payload.get("eventSignatures", []),
        "components": payload.get("components", []),
        "summary": {
            "interfaceCount": len(payload.get("implementedInterfaces", []) if isinstance(payload.get("implementedInterfaces"), list) else []),
            "variableCount": len(payload.get("variables", []) if isinstance(payload.get("variables"), list) else []),
            "functionCount": len(payload.get("functions", []) if isinstance(payload.get("functions"), list) else []),
            "interfaceFunctionCount": len(payload.get("interfaceFunctions", []) if isinstance(payload.get("interfaceFunctions"), list) else []),
            "componentCount": len(payload.get("components", []) if isinstance(payload.get("components"), list) else []),
            "eventSignatureCount": len(payload.get("eventSignatures", []) if isinstance(payload.get("eventSignatures"), list) else []),
        },
    }


def shape_blueprint_class_inspect_result(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "assetPath": payload.get("assetPath"),
        "blueprintClass": payload.get("blueprintClass"),
        "parentClass": payload.get("parentClass"),
        "parentClassPath": payload.get("parentClassPath"),
        "implementedInterfaces": payload.get("implementedInterfaces", []),
        "interfaceFunctions": payload.get("interfaceFunctions", []),
        "classDefaults": payload.get("classDefaults"),
        "metadata": payload.get("metadata"),
    }


def shape_blueprint_member_inspect_result(payload: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any]:
    member_kind = string_field(arguments, "memberKind")
    name_filter = string_field(arguments, "name")
    field_by_kind = {
        "variable": "variables",
        "function": "functions",
        "component": "components",
        "macro": "macros",
        "dispatcher": "dispatchers",
        "event": "eventSignatures",
        "customEvent": "eventSignatures",
    }
    field = field_by_kind.get(member_kind or "")
    if field is None:
        raise TransformError(f"Unsupported memberKind: {member_kind}")
    items = payload.get(field, [])
    if not isinstance(items, list):
        items = []
    if name_filter is not None:
        items = [item for item in items if isinstance(item, dict) and item.get("name") == name_filter]
    return {"assetPath": payload.get("assetPath"), "memberKind": member_kind, "items": items}


def shape_blueprint_compile_result(payload: dict[str, Any]) -> dict[str, Any]:
    report = payload.get("compileReport")
    result = dict(report) if isinstance(report, dict) else {}
    for field in ["assetPath", "status", "diagnostics"]:
        if field in payload:
            result[field] = payload[field]
    result.setdefault("compiled", False)
    return result


def augment_blueprint_graph_edit_result(payload: dict[str, Any]) -> dict[str, Any]:
    result = dict(payload)
    if "opResults" in result and "commandResults" not in result:
        result["commandResults"] = result["opResults"]
    return result


def shape_blueprint_graph_inspect_result(payload: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any]:
    result = dict(payload)
    if "graph" not in result:
        graph_ref = result.get("graphRef")
        if isinstance(graph_ref, dict):
            graph_id = graph_ref.get("id") or graph_ref.get("graphId")
            if isinstance(graph_id, str):
                result["graph"] = {"id": graph_id}
        elif isinstance(result.get("graphName"), str):
            result["graph"] = {"name": result["graphName"]}
    view = arguments.get("view", "summary")
    snapshot = result.get("semanticSnapshot")
    nodes = snapshot.get("nodes") if isinstance(snapshot, dict) and isinstance(snapshot.get("nodes"), list) else []
    node_map = {node_id(node): node for node in nodes if isinstance(node, dict) and node_id(node)}
    links = blueprint_graph_links(nodes)
    if view == "exec_flow":
        root_node = arguments.get("rootNode") if isinstance(arguments.get("rootNode"), dict) else {}
        root_id = root_node.get("id") if isinstance(root_node.get("id"), str) else ""
        direction = traversal_direction(arguments, "downstream")
        ordered, traversed, truncated = trace_exec(root_id, node_map, links, direction)
        result.pop("semanticSnapshot", None)
        result["view"] = "exec_flow"
        result["rootNode"] = compact_blueprint_graph_node(node_map[root_id]) if root_id in node_map else {"id": root_id, "missing": True}
        result["flow"] = {
            "direction": direction,
            "nodes": [compact_blueprint_graph_node(node_map[item]) for item in ordered if item in node_map],
            "links": [link_to_json(link) for link in traversed],
            "truncated": truncated,
        }
    elif view == "data_flow":
        root_pin = arguments.get("rootPin") if isinstance(arguments.get("rootPin"), dict) else {}
        root_node = root_pin.get("node") if isinstance(root_pin.get("node"), dict) else {}
        root_node_id = root_node.get("id") if isinstance(root_node.get("id"), str) else ""
        root_pin_name = root_pin.get("pin") if isinstance(root_pin.get("pin"), str) else ""
        direction = traversal_direction(arguments, "upstream")
        ordered, traversed, truncated = trace_data(root_node_id, root_pin_name, node_map, links, direction)
        result.pop("semanticSnapshot", None)
        result["view"] = "data_flow"
        result["rootPin"] = {"node": {"id": root_node_id}, "pin": root_pin_name}
        result["dataFlow"] = {
            "direction": direction,
            "nodes": [compact_blueprint_graph_node(node_map[item]) for item in ordered if item in node_map],
            "links": [link_to_json(link) for link in traversed],
            "truncated": truncated,
        }
    else:
        exec_links = [link for link in links if link["kind"] == "exec"]
        roots = graph_roots(node_map, exec_links)
        covered: set[str] = set()
        chains = []
        for root_id in roots:
            ordered, traversed, truncated = trace_exec(root_id, node_map, links, "downstream")
            covered.update(ordered)
            chains.append({
                "root": compact_blueprint_graph_node(node_map[root_id]),
                "nodeCount": len(ordered),
                "linkCount": len(traversed),
                "path": [{"id": item, "label": node_label(node_map[item])} for item in ordered[:12] if item in node_map],
                "truncated": truncated,
            })
        result.pop("semanticSnapshot", None)
        result["view"] = "summary"
        result["boundary"] = {
            "entries": [compact_blueprint_graph_node(node_map[item]) for item in roots if item in node_map],
            "outputs": [],
        }
        result["roots"] = [compact_blueprint_graph_node(node_map[item]) for item in roots if item in node_map]
        result["chains"] = chains
        result["looseNodes"] = [compact_blueprint_graph_node(node) for key, node in node_map.items() if key not in covered]
        result["linkCounts"] = {
            "exec": len(exec_links),
            "data": len([link for link in links if link["kind"] == "data"]),
        }
    meta = result.get("meta")
    if not isinstance(meta, dict):
        meta = {}
        result["meta"] = meta
    meta["view"] = view
    return result


def compact_blueprint_graph_node(node: dict[str, Any]) -> dict[str, Any]:
    fields = [
        "id", "guid", "name", "className", "classPath", "nodeClassPath", "title",
        "nodeTitle", "enabled", "isNodeEnabled", "position", "layout", "graphName",
        "childGraphRef", "memberReference", "functionReference", "k2Extensions",
        "embeddedTemplate", "effectiveSettings", "contextSensitiveConstruct",
        "graphBoundarySummary", "hasNodeEditCapabilities", "inspectWith",
    ]
    out = {field: node[field] for field in fields if field in node}
    if isinstance(out.get("k2Extensions"), dict):
        out["k2Extensions"] = dict(out["k2Extensions"])
        out["k2Extensions"].pop("comment", None)
    return out


def node_id(node: dict[str, Any]) -> str | None:
    value = node.get("id") or node.get("guid")
    return value if isinstance(value, str) and value else None


def node_label(node: dict[str, Any]) -> str:
    for field in ["title", "nodeTitle", "name", "className"]:
        value = node.get(field)
        if isinstance(value, str) and value:
            return value
    return node_id(node) or "<unknown>"


def node_text(node: dict[str, Any]) -> str:
    return " ".join(
        value
        for field in ["className", "classPath", "nodeClassPath", "title", "nodeTitle", "name"]
        if isinstance((value := node.get(field)), str)
    ).lower()


def is_entry_node(node: dict[str, Any]) -> bool:
    text = node_text(node)
    return (
        "k2node_event" in text
        or "k2node_customevent" in text
        or "functionentry" in text
        or "receivebeginplay" in text
        or "event " in text
    )


def pin_name(pin: dict[str, Any]) -> str | None:
    value = pin.get("name")
    return value if isinstance(value, str) and value else None


def is_exec_pin(pin: dict[str, Any]) -> bool:
    value = pin.get("category") or pin.get("type")
    return isinstance(value, str) and value.lower() == "exec"


def is_output_pin(pin: dict[str, Any]) -> bool:
    return isinstance(pin.get("direction"), str) and pin["direction"].lower() == "output"


def pin_key(node: str, pin: str) -> str:
    return f"{node}:{pin}"


def traversal_direction(arguments: dict[str, Any], default: str) -> str:
    traversal = arguments.get("traversal")
    value = traversal.get("direction") if isinstance(traversal, dict) else None
    return value if value in {"upstream", "downstream", "both"} else default


def blueprint_graph_links(nodes: Any) -> list[dict[str, str]]:
    pin_map: dict[str, dict[str, Any]] = {}
    for node in nodes if isinstance(nodes, list) else []:
        if not isinstance(node, dict) or not (owner := node_id(node)):
            continue
        pins = node.get("pins")
        for pin in pins if isinstance(pins, list) else []:
            if isinstance(pin, dict) and (name := pin_name(pin)):
                pin_map[pin_key(owner, name)] = pin

    links: list[dict[str, str]] = []
    seen: set[tuple[str, str, str, str, str]] = set()
    for node in nodes if isinstance(nodes, list) else []:
        if not isinstance(node, dict) or not (owner := node_id(node)):
            continue
        pins = node.get("pins")
        for pin in pins if isinstance(pins, list) else []:
            if not isinstance(pin, dict) or not (name := pin_name(pin)):
                continue
            peers = pin.get("linkedTo")
            for peer in peers if isinstance(peers, list) else []:
                if not isinstance(peer, dict):
                    continue
                peer_node = peer.get("nodeId") or peer.get("nodeGuid") or peer.get("guid")
                peer_pin = peer.get("pin") or peer.get("pinName")
                if not isinstance(peer_node, str) or not isinstance(peer_pin, str):
                    continue
                peer_pin_obj = pin_map.get(pin_key(peer_node, peer_pin))
                kind = "exec" if is_exec_pin(pin) or (isinstance(peer_pin_obj, dict) and is_exec_pin(peer_pin_obj)) else "data"
                if is_output_pin(pin):
                    from_node, from_pin, to_node, to_pin = owner, name, peer_node, peer_pin
                elif isinstance(peer_pin_obj, dict) and is_output_pin(peer_pin_obj):
                    from_node, from_pin, to_node, to_pin = peer_node, peer_pin, owner, name
                elif owner <= peer_node:
                    from_node, from_pin, to_node, to_pin = owner, name, peer_node, peer_pin
                else:
                    from_node, from_pin, to_node, to_pin = peer_node, peer_pin, owner, name
                key = (from_node, from_pin, to_node, to_pin, kind)
                if key not in seen:
                    seen.add(key)
                    links.append({
                        "fromNodeId": from_node,
                        "fromPin": from_pin,
                        "toNodeId": to_node,
                        "toPin": to_pin,
                        "kind": kind,
                    })
    return links


def link_to_json(link: dict[str, str]) -> dict[str, str]:
    return dict(link)


def trace_exec(root_id: str, node_map: dict[str, dict[str, Any]], links: list[dict[str, str]], direction: str) -> tuple[list[str], list[dict[str, str]], bool]:
    ordered: list[str] = []
    visited: set[str] = set()
    traversed: list[dict[str, str]] = []
    stack = [(root_id, 0)]
    max_depth = 64
    max_nodes = 250
    while stack:
        current, depth = stack.pop()
        if current in visited:
            continue
        visited.add(current)
        ordered.append(current)
        if len(ordered) >= max_nodes or depth >= max_depth:
            continue
        if direction in {"downstream", "both"}:
            for link in [item for item in links if item["kind"] == "exec" and item["fromNodeId"] == current]:
                traversed.append(link)
                stack.append((link["toNodeId"], depth + 1))
        if direction in {"upstream", "both"}:
            for link in [item for item in links if item["kind"] == "exec" and item["toNodeId"] == current]:
                traversed.append(link)
                stack.append((link["fromNodeId"], depth + 1))
    return [item for item in ordered if item in node_map], traversed, len(ordered) >= max_nodes


def trace_data(root_node: str, root_pin: str, node_map: dict[str, dict[str, Any]], links: list[dict[str, str]], direction: str) -> tuple[list[str], list[dict[str, str]], bool]:
    ordered: list[str] = []
    visited_nodes: set[str] = set()
    visited_pins: set[str] = set()
    traversed: list[dict[str, str]] = []
    stack = [(pin_key(root_node, root_pin), 0)]
    while stack:
        current, depth = stack.pop()
        if current in visited_pins:
            continue
        visited_pins.add(current)
        owner, _, _ = current.partition(":")
        if owner not in visited_nodes:
            visited_nodes.add(owner)
            ordered.append(owner)
        if depth >= 64:
            continue
        if direction in {"upstream", "both"}:
            for link in [item for item in links if item["kind"] == "data" and pin_key(item["toNodeId"], item["toPin"]) == current]:
                traversed.append(link)
                stack.append((pin_key(link["fromNodeId"], link["fromPin"]), depth + 1))
        if direction in {"downstream", "both"}:
            for link in [item for item in links if item["kind"] == "data" and pin_key(item["fromNodeId"], item["fromPin"]) == current]:
                traversed.append(link)
                stack.append((pin_key(link["toNodeId"], link["toPin"]), depth + 1))
    return [item for item in ordered if item in node_map], traversed, False


def graph_roots(node_map: dict[str, dict[str, Any]], exec_links: list[dict[str, str]]) -> list[str]:
    incoming = {link["toNodeId"] for link in exec_links}
    outgoing = {link["fromNodeId"] for link in exec_links}
    return sorted(
        node
        for node, value in node_map.items()
        if is_entry_node(value) or (node not in incoming and node in outgoing)
    )


def compact_graph_pin(pin: Any, *, include_connections: bool) -> Any:
    if not isinstance(pin, dict):
        return pin
    fields = ["name", "direction", "category", "subCategory", "subCategoryObject", "type", "isReference", "isConst", "isArray"]
    out = {field: pin[field] for field in fields if field in pin}
    if include_connections:
        for field in ["linkedTo", "links"]:
            if field in pin:
                out[field] = pin[field]
    return out


def shape_pcg_graph_inspect_result(payload: dict[str, Any], arguments: dict[str, Any]) -> dict[str, Any]:
    result = dict(payload)
    view = arguments.get("view", "overview")
    include_connections = view in ("links", "defaults", "full")
    text = None
    filter_value = arguments.get("filter")
    if isinstance(filter_value, dict) and isinstance(filter_value.get("text"), str):
        text = filter_value["text"].lower()
    page = arguments.get("page")
    cursor = int(page.get("cursor", 0)) if isinstance(page, dict) and str(page.get("cursor", "0")).isdigit() else 0
    limit = int(page.get("limit", 50)) if isinstance(page, dict) and isinstance(page.get("limit"), int) else 50
    snapshot = result.get("semanticSnapshot")
    returned = 0
    total = 0
    next_cursor = ""
    kept: set[str] = set()
    if isinstance(snapshot, dict):
        nodes = snapshot.get("nodes")
        if isinstance(nodes, list):
            filtered = [node for node in nodes if text is None or text in json.dumps(node, ensure_ascii=False).lower()]
            total = len(filtered)
            end = min(cursor + max(1, min(limit, 1000)), total)
            next_cursor = str(end) if end < total else ""
            shaped_nodes = []
            for node in filtered[cursor:end]:
                if isinstance(node, dict):
                    node_id = node.get("id")
                    if isinstance(node_id, str):
                        kept.add(node_id)
                    shaped_nodes.append(compact_pcg_graph_node(node, view))
                else:
                    shaped_nodes.append(node)
            returned = len(shaped_nodes)
            snapshot["nodes"] = shaped_nodes
        if include_connections:
            edges = snapshot.get("edges")
            if isinstance(edges, list):
                snapshot["edges"] = [
                    edge for edge in edges
                    if not isinstance(edge, dict)
                    or edge.get("fromNodeId") in kept
                    or edge.get("toNodeId") in kept
                ]
        else:
            snapshot["edges"] = []
    meta = result.get("meta")
    if not isinstance(meta, dict):
        meta = {}
        result["meta"] = meta
    meta.update({"view": view, "totalNodes": total, "returnedNodes": returned, "truncated": bool(next_cursor)})
    result["nextCursor"] = next_cursor
    return result


def compact_pcg_graph_node(node: dict[str, Any], view: Any) -> dict[str, Any]:
    if view == "full":
        return dict(node)
    fields = ["id", "guid", "nodeClassPath", "title", "enabled", "position", "layout", "childGraphRef", "settings"]
    out = {field: node[field] for field in fields if field in node}
    if view in ("pins", "links", "defaults"):
        pins = node.get("pins")
        out["pins"] = [compact_graph_pin(pin, include_connections=view in ("links", "defaults")) for pin in pins] if isinstance(pins, list) else []
    return out


def shape_pcg_node_inspect_result(payload: dict[str, Any]) -> dict[str, Any]:
    mode = payload.get("mode")
    if mode == "instance":
        node = payload.get("node") if isinstance(payload.get("node"), dict) else {}
        settings = node.get("settings") or node.get("effectiveSettings") or {}
        return {
            "mode": "instance",
            "assetPath": payload.get("assetPath"),
            "nodeId": payload.get("nodeId"),
            "node": node,
            "settings": settings,
            "properties": settings.get("properties", []) if isinstance(settings, dict) else [],
            "pins": node.get("pins", []) if isinstance(node, dict) else [],
        }
    if mode == "class":
        return {
            "mode": "class",
            "nodeClass": payload.get("nodeClass"),
            "title": payload.get("title"),
            "tooltip": payload.get("tooltip"),
            "settingsType": payload.get("settingsType"),
            "inputPins": payload.get("inputPins", []),
            "outputPins": payload.get("outputPins", []),
            "properties": payload.get("properties", []),
        }
    return payload


def widget_tree_edit_args(arguments: dict[str, Any]) -> dict[str, Any]:
    asset_path = string_field(arguments, "assetPath")
    if asset_path is None:
        raise TransformError("widget.tree.edit requires assetPath.")
    commands = arguments.get("commands")
    if not isinstance(commands, list):
        raise TransformError("widget.tree.edit requires commands.")
    if not commands:
        raise TransformError("widget.tree.edit commands must be non-empty.")

    ops = []
    for command in commands:
        if not isinstance(command, dict):
            raise TransformError("widget.tree.edit commands entries must be objects.")
        ops.append(compile_widget_tree_command(command))

    transformed: dict[str, Any] = {"assetPath": asset_path, "ops": ops}
    for field in [
        "expectedRevision",
        "idempotencyKey",
        "dryRun",
        "returnDiff",
        "returnDiagnostics",
        "continueOnError",
    ]:
        copy_if_present(arguments, transformed, field)
    return transformed


def compile_widget_tree_command(command: dict[str, Any]) -> dict[str, Any]:
    kind = string_field(command, "kind")
    if kind is None:
        raise TransformError("widget.tree.edit command requires kind.")

    if kind == "addFromPalette":
        entry = command.get("entry")
        if not isinstance(entry, dict):
            raise TransformError("addFromPalette requires entry from widget.palette.")
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id:
            raise TransformError("addFromPalette requires entry.id.")
        if entry.get("executable") is False:
            raise TransformError(f"widget.palette entry is not executable: {entry_id}")
        payload = entry.get("payload")
        if not isinstance(payload, dict):
            raise TransformError("addFromPalette requires entry.payload.")
        widget_class = payload.get("widgetClass")
        if not isinstance(widget_class, str) or not widget_class:
            raise TransformError("widget.palette entry.payload requires widgetClass.")
        name = string_field(command, "name")
        if name is None:
            raise TransformError("addFromPalette requires name.")

        args: dict[str, Any] = {"widgetClass": widget_class, "name": name}
        parent_name = widget_parent_name(command)
        if parent_name is not None:
            args["parentName"] = parent_name
        if isinstance(command.get("slot"), dict):
            args["slot"] = command["slot"]
        return {"op": "addWidget", "args": args}

    if kind == "removeWidget":
        name = widget_target_name(command)
        if name is None:
            raise TransformError("removeWidget requires name or target.name.")
        return {"op": "removeWidget", "args": {"name": name}}

    if kind == "setProperty":
        name = widget_target_name(command)
        if name is None:
            raise TransformError("setProperty requires name or target.name.")
        property_name = string_field(command, "property")
        if property_name is None:
            raise TransformError("setProperty requires property.")
        value = command.get("value")
        if not isinstance(value, str):
            raise TransformError("setProperty requires string value.")
        return {
            "op": "setProperty",
            "args": {"name": name, "property": property_name, "value": value},
        }

    if kind == "reparentWidget":
        name = widget_target_name(command)
        if name is None:
            raise TransformError("reparentWidget requires name or target.name.")
        new_parent = command.get("newParent")
        if isinstance(new_parent, str) and new_parent:
            new_parent_name = new_parent
        elif isinstance(new_parent, dict) and isinstance(new_parent.get("name"), str):
            new_parent_name = new_parent["name"]
        else:
            raise TransformError("reparentWidget requires newParent or newParent.name.")
        args = {"name": name, "newParent": new_parent_name}
        if isinstance(command.get("slot"), dict):
            args["slot"] = command["slot"]
        return {"op": "reparentWidget", "args": args}

    raise TransformError(f"Unsupported widget.tree.edit command kind: {kind}.")


def node_ref_token(node: dict[str, Any]) -> dict[str, Any]:
    if isinstance(node.get("id"), str) and node["id"]:
        return {"nodeId": node["id"]}
    if isinstance(node.get("alias"), str) and node["alias"]:
        return {"clientRef": node["alias"]}
    raise TransformError("node reference requires id or alias.")


def widget_parent_name(command: dict[str, Any]) -> str | None:
    parent_name = string_field(command, "parentName")
    if parent_name is not None:
        return parent_name
    parent = command.get("parent")
    if isinstance(parent, str) and parent:
        return parent
    if isinstance(parent, dict) and isinstance(parent.get("name"), str):
        return parent["name"]
    return None


def widget_target_name(command: dict[str, Any]) -> str | None:
    name = string_field(command, "name")
    if name is not None:
        return name
    target = command.get("target")
    if isinstance(target, dict) and isinstance(target.get("name"), str):
        return target["name"]
    return None


def shape_widget_tree_inspect_payload(
    payload: dict[str, Any],
    arguments: dict[str, Any],
) -> dict[str, Any]:
    shaped = dict(payload)
    view = arguments.get("view") if isinstance(arguments.get("view"), str) else "outline"
    names = set()
    text: str | None = None
    filter_value = arguments.get("filter")
    if isinstance(filter_value, dict):
        raw_names = filter_value.get("names")
        if isinstance(raw_names, list):
            names = {item for item in raw_names if isinstance(item, str)}
        raw_text = filter_value.get("text")
        if isinstance(raw_text, str):
            text = raw_text.lower()

    shaped["view"] = view
    root = shaped.get("rootWidget")
    if view == "outline":
        shaped["rootWidget"] = prune_widget_tree_outline(root)

    if names or text is not None:
        matches: list[Any] = []
        collect_widget_tree_matches(shaped.get("rootWidget"), names, text, matches)
        shaped["matches"] = matches
        if view == "details":
            shaped.pop("rootWidget", None)
    return shaped


def prune_widget_tree_outline(node: Any) -> Any:
    if not isinstance(node, dict):
        return node
    pruned = dict(node)
    pruned.pop("slot", None)
    children = pruned.get("children")
    if isinstance(children, list):
        pruned["children"] = [prune_widget_tree_outline(child) for child in children]
    return pruned


def collect_widget_tree_matches(
    node: Any,
    names: set[str],
    text: str | None,
    out: list[Any],
) -> None:
    if not isinstance(node, dict):
        return
    name_matches = not names or node.get("name") in names
    text_matches = text is None or text in json.dumps(node, ensure_ascii=False).lower()
    if name_matches and text_matches:
        out.append(node)
    children = node.get("children")
    if isinstance(children, list):
        for child in children:
            collect_widget_tree_matches(child, names, text, out)
