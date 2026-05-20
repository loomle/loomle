from __future__ import annotations

import unittest
import re
from pathlib import Path

from loomle_mcp.manifest import load_manifest


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "mcp" / "manifest" / "manifest.json"


class ToolManifestTests(unittest.TestCase):
    def test_lists_python_tools_without_native_only_install(self) -> None:
        manifest = load_manifest(MANIFEST)
        names = {tool["name"] for tool in manifest.list_tools("python")}

        self.assertIn("project.list", names)
        self.assertIn("project.attach", names)
        self.assertIn("schema.inspect", names)
        self.assertIn("context", names)
        self.assertIn("blueprint.graph.list", names)
        self.assertIn("blueprint.palette", names)
        self.assertIn("blueprint.compile", names)
        self.assertIn("blueprint.graph.edit", names)
        self.assertIn("material.palette", names)
        self.assertIn("pcg.palette", names)
        self.assertIn("pcg.compile", names)
        self.assertIn("widget.palette", names)
        self.assertIn("widget.tree.inspect", names)
        self.assertIn("widget.tree.edit", names)
        self.assertIn("widget.compile", names)
        self.assertNotIn("project.install", names)

    def test_python_manifest_covers_rust_runtime_tools(self) -> None:
        manifest = load_manifest(MANIFEST)
        python_names = {tool["name"] for tool in manifest.list_tools("python")}
        rust_source = (REPO_ROOT / "client" / "src" / "main.rs").read_text()
        rust_names = set(re.findall(r'Tool::new\("([^"]+)"', rust_source))
        rust_names.discard("project.install")

        self.assertFalse(rust_names - python_names)

    def test_blueprint_graph_edit_requires_asset_path_and_commands(self) -> None:
        manifest = load_manifest(MANIFEST)
        tool = next(
            tool for tool in manifest.list_tools("python")
            if tool["name"] == "blueprint.graph.edit"
        )

        required = set(tool["inputSchema"]["required"])
        self.assertEqual(required, {"assetPath", "commands"})

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
            {"addFromPalette", "removeWidget", "setProperty", "reparentWidget"},
        )

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
        payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.member.edit",
            operation="variable.create",
            include=["schema"],
        )

        args_schema = payload["schema"]["properties"]["args"]
        self.assertEqual(args_schema["required"], ["variableName", "type"])
        self.assertIn("defaultValue", args_schema["properties"])

    def test_blueprint_node_edit_operation_schema_is_precise(self) -> None:
        manifest = load_manifest(MANIFEST)
        payload = manifest.inspect_schema(
            domain="blueprint",
            tool_name="blueprint.node.edit",
            operation="addPin",
            include=["schema"],
        )

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
