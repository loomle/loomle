from __future__ import annotations

import unittest
import re
from pathlib import Path

from loomle_mcp.manifest import load_manifest


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "mcp" / "manifest" / "manifest.json"
PYTHON_LOCAL_TOOLS = {
    "status",
    "project.attach",
    "project.list",
    "schema.inspect",
}


class ToolManifestTests(unittest.TestCase):
    def test_lists_python_tools_without_native_only_install(self) -> None:
        manifest = load_manifest(MANIFEST)
        names = {tool["name"] for tool in manifest.list_tools("python")}

        self.assertIn("project.list", names)
        self.assertIn("project.attach", names)
        self.assertIn("schema.inspect", names)
        self.assertIn("context", names)
        self.assertIn("blueprint.graph.list", names)
        self.assertIn("blueprint.graph.palette", names)
        self.assertIn("blueprint.compile", names)
        self.assertIn("blueprint.graph.edit", names)
        self.assertIn("material.palette", names)
        self.assertIn("pcg.palette", names)
        self.assertIn("pcg.compile", names)
        self.assertIn("widget.palette", names)
        self.assertIn("widget.tree.inspect", names)
        self.assertIn("widget.tree.edit", names)
        self.assertIn("widget.edit", names)
        self.assertIn("widget.event.create", names)
        self.assertIn("widget.compile", names)
        self.assertNotIn("loomle", names)
        self.assertNotIn("setup.status", names)
        self.assertNotIn("setup.configure", names)
        self.assertNotIn("project.install", names)

    def test_widget_tree_inspect_manifest_declares_outline_layout_output(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool
            for tool in manifest.list_tools("python")
            if tool["name"] == "widget.tree.inspect"
        )
        view_enum = tool["inputSchema"]["properties"]["view"]["enum"]
        self.assertEqual(view_enum, ["outline", "layout"])

        output_schema = tool["outputSchema"]
        self.assertIn("widgetTreeNode", output_schema["$defs"])
        node_schema = output_schema["$defs"]["widgetTreeNode"]
        for field in [
            "name",
            "widgetClass",
            "parentName",
            "index",
            "isVariable",
            "variableGuid",
            "children",
        ]:
            self.assertIn(field, node_schema["required"])
        self.assertIn("slotClass", node_schema["properties"])
        self.assertIn("slot", node_schema["properties"])

    def test_widget_inspect_manifest_declares_property_output(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool
            for tool in manifest.list_tools("python")
            if tool["name"] == "widget.inspect"
        )

        output_schema = tool["outputSchema"]
        self.assertIn("widgetProperty", output_schema["$defs"])
        success_properties = output_schema["oneOf"][0]["properties"]
        for field in [
            "isError",
            "widgetClass",
            "properties",
            "slotClass",
            "slotProperties",
            "currentValues",
            "slotCurrentValues",
        ]:
            self.assertIn(field, success_properties)

    def test_widget_event_create_manifest_declares_native_event_output(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool
            for tool in manifest.list_tools("python")
            if tool["name"] == "widget.event.create"
        )

        output_schema = tool["outputSchema"]
        success_properties = output_schema["oneOf"][0]["properties"]
        self.assertEqual(
            success_properties["widget"]["properties"]["name"]["type"],
            "string",
        )
        self.assertEqual(success_properties["node"]["properties"]["nodeClass"]["type"], "string")
        self.assertEqual(output_schema["oneOf"][1]["properties"]["isError"]["const"], True)

    def test_python_manifest_covers_rust_runtime_tools(self) -> None:
        manifest = load_manifest(MANIFEST)
        python_names = {tool["name"] for tool in manifest.list_tools("python")}
        rust_source = (REPO_ROOT / "client" / "src" / "main.rs").read_text()
        rust_names = set(re.findall(r'Tool::new\("([^"]+)"', rust_source))
        rust_names.discard("project.install")

        self.assertFalse(rust_names - python_names)

    def test_python_manifest_tool_names_match_rust_plus_local_tools(self) -> None:
        manifest = load_manifest(MANIFEST)
        python_names = {tool["name"] for tool in manifest.list_tools("python")}
        rust_source = (REPO_ROOT / "client" / "src" / "main.rs").read_text()
        rust_names = set(re.findall(r'Tool::new\("([^"]+)"', rust_source))

        self.assertEqual(python_names - rust_names, PYTHON_LOCAL_TOOLS)

    def test_manifest_dispatch_transforms_are_implemented(self) -> None:
        manifest = load_manifest(MANIFEST)
        referenced: set[str] = set()
        for tool in manifest.tools_for("python"):
            dispatch = tool.get("dispatch", {})
            for field in ("args", "result"):
                value = dispatch.get(field)
                if isinstance(value, dict) and isinstance(value.get("transform"), str):
                    referenced.add(value["transform"])

        transform_source = (REPO_ROOT / "mcp" / "python" / "loomle_mcp" / "transforms.py").read_text()
        implemented = set(re.findall(r'if name == "([^"]+)"', transform_source))

        self.assertFalse(referenced - implemented)
        self.assertFalse(implemented - referenced)

    def test_schema_inspect_tools_match_rust_dispatch(self) -> None:
        manifest = load_manifest(MANIFEST)
        manifest_tools = {
            tool["name"]
            for tool in manifest.tools_for("python")
            if isinstance(tool.get("schemaInspect"), dict)
        }
        rust_source = (REPO_ROOT / "client" / "src" / "schema_inspect.rs").read_text()
        available = set(re.findall(r'"((?:blueprint|material|pcg|widget)\.[^":]+)"', rust_source))

        self.assertEqual(
            manifest_tools,
            {
                "blueprint.graph.edit",
                "blueprint.member.edit",
                "blueprint.node.edit",
                "material.graph.edit",
                "pcg.graph.edit",
                "pcg.parameter.edit",
                "widget.tree.edit",
                "widget.edit",
            },
        )
        self.assertTrue(manifest_tools <= available)

    def test_bridge_rpc_dispatch_does_not_use_retired_tool_names(self) -> None:
        manifest = load_manifest(MANIFEST)
        retired = {
            "blueprint.edit",
            "blueprint.verify",
            "material.query",
            "material.mutate",
            "material.verify",
            "material.describe",
            "pcg.list",
            "pcg.query",
            "pcg.mutate",
            "pcg.verify",
            "pcg.describe",
            "widget.query",
            "widget.mutate",
            "widget.verify",
            "widget.describe",
        }

        offenders = {
            tool["name"]: tool.get("dispatch", {}).get("tool")
            for tool in manifest.list_tools("python")
            if tool.get("dispatch", {}).get("tool") in retired
        }

        self.assertEqual(offenders, {})

    def test_status_tool_manifest_is_single_runtime_status_entrypoint(self) -> None:
        manifest = load_manifest(MANIFEST)
        names = {tool["name"] for tool in manifest.list_tools("python")}
        self.assertIn("status", names)
        self.assertNotIn("loomle", names)
        self.assertNotIn("setup.status", names)
        self.assertNotIn("setup.configure", names)

        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "status"
        )
        self.assertEqual(tool["inputSchema"]["properties"], {})
        output_properties = tool["outputSchema"]["properties"]
        self.assertIn("mcp", output_properties)
        self.assertIn("project", output_properties)
        self.assertIn("runtime", output_properties)
        self.assertIn("issues", output_properties)
        self.assertNotIn("plugin", output_properties)
        self.assertNotIn("hosts", output_properties)

    def test_blueprint_graph_edit_requires_asset_path_and_commands(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.edit"
        )

        required = set(tool["inputSchema"]["required"])
        self.assertEqual(required, {"assetPath", "graph", "commands"})
        self.assertNotIn("continueOnError", tool["inputSchema"]["properties"])
        self.assertNotIn("returnDiff", tool["inputSchema"]["properties"])
        self.assertNotIn("returnDiagnostics", tool["inputSchema"]["properties"])
        self.assertIn("outputSchema", tool)
        output_properties = tool["outputSchema"]["oneOf"][0]["properties"]
        self.assertIn("opResults", output_properties)
        self.assertNotIn("commandResults", output_properties)

    def test_blueprint_graph_layout_manifest_is_root_tree_only(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.layout"
        )

        input_schema = tool["inputSchema"]
        self.assertEqual(set(input_schema["required"]), {"assetPath", "graph", "root"})
        properties = input_schema["properties"]
        self.assertIn("root", properties)
        self.assertNotIn("scope", properties)
        self.assertNotIn("returnDiff", properties)
        self.assertNotIn("returnDiagnostics", properties)
        self.assertEqual(properties["root"]["required"], ["id"])

    def test_blueprint_graph_palette_manifest_is_graph_scoped(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.palette"
        )

        input_schema = tool["inputSchema"]
        self.assertEqual(set(input_schema["required"]), {"assetPath", "graph"})
        self.assertNotIn("graphName", input_schema["properties"])
        self.assertEqual(input_schema["properties"]["limit"]["maximum"], 500)
        from_pin = input_schema["properties"]["fromPins"]["items"]
        self.assertEqual(set(from_pin["required"]), {"node", "pin"})

        output_schema = tool["outputSchema"]
        titles = {entry["title"] for entry in output_schema["oneOf"]}
        self.assertIn("Blueprint Graph Palette Result", titles)
        self.assertIn("Blueprint Graph Palette Error", titles)

    def test_blueprint_graph_list_manifest_exposes_inventory_contract(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.list"
        )

        input_props = tool["inputSchema"]["properties"]
        self.assertIn("includeCompositeSubgraphs", input_props)
        self.assertFalse(input_props["includeCompositeSubgraphs"]["default"])

        output_schema = tool["outputSchema"]
        output_props = output_schema["properties"]
        self.assertIn("graphs", output_props)
        self.assertIn("diagnostics", output_props)
        self.assertEqual(
            output_schema["$defs"]["blueprintGraphListEntry"]["properties"]["graphKind"]["enum"],
            ["root", "function", "macro", "delegate_signature", "subgraph"],
        )
        self.assertEqual(
            output_schema["$defs"]["blueprintGraphRef"]["properties"]["kind"]["enum"],
            ["asset", "inline"],
        )
        self.assertEqual(
            output_props["code"]["enum"],
            ["INVALID_ARGUMENT", "ASSET_NOT_FOUND", "INTERNAL_ERROR"],
        )

    def test_blueprint_inspect_manifest_is_overview_entrypoint(self) -> None:
        manifest = load_manifest(MANIFEST)
        blueprint_tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.inspect"
        )
        class_tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.class.inspect"
        )

        blueprint_output = blueprint_tool["outputSchema"]["properties"]
        self.assertIn("routes", blueprint_output)
        self.assertIn("summary", blueprint_output)
        self.assertIn("variables", blueprint_output)
        self.assertIn("functions", blueprint_output)
        self.assertIn("components", blueprint_output)

        class_output = class_tool["outputSchema"]["properties"]
        self.assertIn("class", class_output)
        self.assertIn("settings", class_output)
        self.assertIn("implementedInterfaces", class_output)
        self.assertIn("interfaceFunctions", class_output)
        self.assertIn("classDefaults", class_output)
        self.assertIn("metadata", class_output)
        default_props = class_output["classDefaults"]["properties"]
        self.assertEqual(default_props["source"]["const"], "generatedClassCDO")
        self.assertEqual(default_props["comparison"]["const"], "parentClassCDO")

        class_edit_tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.class.edit"
        )
        class_edit_ops = class_edit_tool["inputSchema"]["properties"]["operation"]["enum"]
        self.assertEqual(
            class_edit_ops,
            ["setParent", "setSettings", "setDefault", "addInterface", "removeInterface"],
        )
        class_edit_args = class_edit_tool["inputSchema"]["properties"]["args"]["properties"]
        class_edit_props = class_edit_tool["inputSchema"]["properties"]
        self.assertIn("settings", class_edit_args)
        self.assertIn("generateAbstractClass", class_edit_args["settings"]["properties"])
        self.assertIn("property", class_edit_args)
        self.assertIn("value", class_edit_args)
        self.assertNotIn("returnDiff", class_edit_props)
        self.assertNotIn("returnDiagnostics", class_edit_props)
        self.assertIn("expectedRevision", class_edit_props)
        self.assertIn("outputSchema", class_edit_tool)
        self.assertIn("applied", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("settings", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("default", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("valid", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("resolvedRefs", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("planned", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("diagnostics", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("diff", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("previousRevision", class_edit_tool["outputSchema"]["properties"])
        self.assertIn("newRevision", class_edit_tool["outputSchema"]["properties"])

        node_inspect_tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.node.inspect"
        )
        self.assertIn("outputSchema", node_inspect_tool)
        node_output = node_inspect_tool["outputSchema"]["oneOf"][0]["properties"]
        self.assertIn("node", node_output)
        self.assertIn("editState", node_output)
        self.assertIn("editCapabilities", node_output)
        self.assertNotIn("pins", node_output)
        self.assertNotIn("state", node_output)
        self.assertNotIn("graphName", node_output)

    def test_blueprint_graph_inspect_manifest_exposes_flow_views(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.inspect"
        )

        schema = tool["inputSchema"]
        properties = schema["properties"]
        self.assertEqual(
            properties["view"]["enum"],
            ["summary", "exec_flow", "data_flow"],
        )
        self.assertEqual(properties["view"]["default"], "summary")
        self.assertIn("rootNode", properties)
        self.assertIn("rootPin", properties)
        self.assertIn("traversal", properties)
        self.assertNotIn("filter", properties)
        self.assertNotIn("page", properties)
        output_schema = tool["outputSchema"]
        self.assertEqual(
            [
                entry["properties"]["view"]["const"]
                for entry in output_schema["oneOf"]
                if "view" in entry["properties"]
            ],
            ["summary", "exec_flow", "data_flow"],
        )
        error_schema = next(
            entry for entry in output_schema["oneOf"]
            if entry.get("title") == "Blueprint Graph Inspect Error"
        )
        self.assertIn("NODE_NOT_FOUND", error_schema["properties"]["code"]["enum"])
        self.assertIn("PIN_NOT_FOUND", error_schema["properties"]["code"]["enum"])
        self.assertIn("blueprintGraphNodeSummary", output_schema["$defs"])

    def test_context_manifest_is_empty_input_with_editor_snapshot_output(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "context"
        )

        self.assertEqual(tool["inputSchema"]["properties"], {})
        output_schema = tool["outputSchema"]
        properties = output_schema["properties"]
        self.assertIn("activeAsset", properties)
        self.assertIn("activeEditor", properties)
        self.assertIn("activeGraph", properties)
        self.assertIn("selection", properties)
        self.assertNotIn("context", properties)

    def test_schema_inspect_operation_comes_from_manifest(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.graph.edit",
            operation="addFromPalette",
            include=["summary", "schema", "examples", "errors", "notes"],
        )

        self.assertEqual(payload["operation"], "addFromPalette")
        self.assertEqual(payload["schema"]["properties"]["kind"]["const"], "addFromPalette")
        self.assertEqual(payload["errors"], ["PALETTE_ENTRY_NOT_EXECUTABLE"])
        self.assertTrue(payload["examples"])

    def test_schema_inspect_lists_widget_tree_edit_operations(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="widget",
            tool_name="widget.tree.edit",
            operation=None,
            include=None,
        )

        names = {operation["name"] for operation in payload["operations"]}
        self.assertEqual(
            names,
            {"addFromPalette", "removeWidget", "renameWidget", "reparentWidget"},
        )

    def test_schema_inspect_lists_widget_edit_operations(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="widget",
            tool_name="widget.edit",
            operation=None,
            include=None,
        )

        names = {operation["name"] for operation in payload["operations"]}
        self.assertEqual(names, {"setProperty", "setSlotProperty"})

    def test_widget_tree_edit_output_schema_declares_mutation_envelope(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "widget.tree.edit"
        )

        self.assertIn("outputSchema", tool)
        output_properties = tool["outputSchema"]["oneOf"][0]["properties"]
        self.assertIn("applied", output_properties)
        self.assertIn("valid", output_properties)
        self.assertIn("dryRun", output_properties)
        self.assertIn("resolvedRefs", output_properties)
        self.assertIn("planned", output_properties)
        self.assertIn("diagnostics", output_properties)
        self.assertIn("diff", output_properties)
        self.assertIn("opResults", output_properties)
        self.assertIn("previousRevision", output_properties)
        self.assertIn("newRevision", output_properties)

    def test_schema_inspect_returns_widget_tree_edit_operation(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="widget",
            tool_name="widget.tree.edit",
            operation="addFromPalette",
            include=["summary", "schema", "notes"],
        )

        self.assertEqual(payload["operation"], "addFromPalette")
        self.assertIn("entry", payload["schema"]["properties"])
        self.assertTrue(
            any("full selected entry" in note for note in payload["notes"])
        )

    def test_schema_inspect_lists_material_and_pcg_edit_operations(self) -> None:
        manifest = load_manifest(MANIFEST)

        material = manifest.inspect_schema(
            domain="material",
            tool_name="material.graph.edit",
            operation=None,
            include=None,
        )
        self.assertIn("addFromPalette", {op["name"] for op in material["operations"]})
        self.assertIn("breakPinLinks", {op["name"] for op in material["operations"]})

        pcg = manifest.inspect_schema(
            domain="pcg",
            tool_name="pcg.graph.edit",
            operation=None,
            include=None,
        )
        self.assertIn("setNodeProperty", {op["name"] for op in pcg["operations"]})
        self.assertIn("connect", {op["name"] for op in pcg["operations"]})

    def test_graph_edit_operation_schemas_are_precise(self) -> None:
        manifest = load_manifest(MANIFEST)

        blueprint = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.graph.edit",
            operation="connect",
            include=["schema"],
        )
        self.assertEqual(blueprint["schema"]["required"], ["kind", "from", "to"])
        self.assertIn("$defs", blueprint["schema"])

        material = manifest.inspect_schema(
            domain="material",
            tool_name="material.graph.edit",
            operation="breakPinLinks",
            include=["schema"],
        )
        self.assertEqual(material["schema"]["required"], ["kind", "target"])

        pcg = manifest.inspect_schema(
            domain="pcg",
            tool_name="pcg.graph.edit",
            operation="setNodeProperty",
            include=["schema"],
        )
        self.assertEqual(pcg["schema"]["required"], ["kind", "node", "property", "value"])

    def test_blueprint_member_edit_operation_schema_is_precise(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.member.edit"
        )
        properties = tool["inputSchema"]["properties"]
        self.assertEqual(
            properties["memberKind"]["enum"],
            ["variable", "function", "macro", "dispatcher", "event", "component"],
        )
        self.assertNotIn("returnDiff", properties)
        self.assertNotIn("returnDiagnostics", properties)
        self.assertIn("expectedRevision", properties)
        self.assertIn("applied", tool["outputSchema"]["properties"])
        self.assertIn("valid", tool["outputSchema"]["properties"])
        self.assertIn("resolvedRefs", tool["outputSchema"]["properties"])
        self.assertIn("planned", tool["outputSchema"]["properties"])
        self.assertIn("diagnostics", tool["outputSchema"]["properties"])
        self.assertIn("diff", tool["outputSchema"]["properties"])
        self.assertIn("previousRevision", tool["outputSchema"]["properties"])
        self.assertIn("newRevision", tool["outputSchema"]["properties"])

        payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.member.edit",
            operation="variable.create",
            include=["schema"],
        )

        args_schema = payload["schema"]["properties"]["args"]
        self.assertEqual(args_schema["required"], ["variableName", "type"])
        self.assertIn("defaultValue", args_schema["properties"])
        self.assertNotIn("returnDiff", payload["schema"]["properties"])

        signature_payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.member.edit",
            operation="function.updateSignature",
            include=["schema"],
        )
        signature_args = signature_payload["schema"]["properties"]["args"]
        self.assertEqual(signature_args["required"], ["functionName"])
        self.assertIn("outputs", signature_args["properties"])

    def test_blueprint_node_edit_operation_schema_is_precise(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.node.edit"
        )
        properties = tool["inputSchema"]["properties"]
        self.assertNotIn("returnDiff", properties)
        self.assertNotIn("returnDiagnostics", properties)
        self.assertIn("expectedRevision", properties)
        self.assertIn("outputSchema", tool)
        output_properties = tool["outputSchema"]["oneOf"][0]["properties"]
        self.assertIn("opResults", output_properties)
        self.assertNotIn("commandResults", output_properties)

        payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.node.edit",
            operation="addPin",
            include=["schema"],
        )

        self.assertNotIn("returnDiff", payload["schema"]["properties"])
        self.assertNotIn("returnDiagnostics", payload["schema"]["properties"])
        args_schema = payload["schema"]["properties"]["args"]
        self.assertEqual(args_schema["required"], ["role"])
        self.assertEqual(
            args_schema["properties"]["role"]["enum"],
            ["case", "exec", "input", "pair", "option", "argument"],
        )

    def test_pcg_parameter_edit_operation_schema_is_precise(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="pcg",
            tool_name="pcg.parameter.edit",
            operation="rename",
            include=["schema"],
        )

        self.assertEqual(payload["schema"]["required"], ["name", "newName"])


if __name__ == "__main__":
    unittest.main()
