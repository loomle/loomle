from __future__ import annotations

import unittest

from loomle_mcp.transforms import (
    TransformError,
    apply_args_transform,
    apply_result_transform,
)


class TransformTests(unittest.TestCase):
    def test_asset_create_blueprint_dispatches_current_bridge_tool(self) -> None:
        payload = apply_args_transform(
            {"transform": "asset.create.args.v1"},
            {
                "kind": "blueprint",
                "assetPath": "/Game/BP_Test",
                "parentClass": "/Script/Engine.Actor",
            },
        )

        self.assertEqual(payload["__bridgeTool"], "blueprint.class.edit")
        self.assertEqual(payload["operation"], "create")

    def test_asset_inspect_dispatches_current_bridge_tools(self) -> None:
        cases = [
            ("material", "material.graph.inspect"),
            ("materialFunction", "material.graph.inspect"),
            ("pcgGraph", "pcg.graph.inspect"),
            ("widgetBlueprint", "widget.tree.inspect"),
        ]
        for kind, bridge_tool in cases:
            with self.subTest(kind=kind):
                payload = apply_args_transform(
                    {"transform": "asset.inspect.args.v1"},
                    {"kind": kind, "assetPath": "/Game/TestAsset"},
                )

                self.assertEqual(payload["__bridgeTool"], bridge_tool)

    def test_asset_or_graph_transform_prefers_graph_asset_path(self) -> None:
        payload = apply_args_transform(
            {"transform": "pcg.palette.args.v1"},
            {
                "assetPath": "/Game/Old",
                "graph": {"kind": "asset", "assetPath": "/Game/New"},
                "query": "spawn",
                "limit": 5,
            },
        )

        self.assertEqual(payload["assetPath"], "/Game/New")
        self.assertEqual(payload["query"], "spawn")
        self.assertEqual(payload["limit"], 5)

    def test_asset_or_graph_transform_requires_asset_path_or_graph(self) -> None:
        with self.assertRaises(TransformError):
            apply_args_transform({"transform": "pcg.compile.args.v1"}, {})

    def test_widget_tree_inspect_transform_shapes_view(self) -> None:
        payload = apply_args_transform(
            {"transform": "widget.tree.inspect.args.v1"},
            {"assetPath": "/Game/UI/WBP_Menu", "view": "layout"},
        )

        self.assertEqual(payload["assetPath"], "/Game/UI/WBP_Menu")
        self.assertTrue(payload["includeSlotProperties"])

    def test_pcg_compile_result_matches_public_shape(self) -> None:
        payload = apply_result_transform(
            {"transform": "pcg.compile.result.v1"},
            {
                "assetPath": "/Game/PCG_Test",
                "status": "ok",
                "summary": {"nodeCount": 2},
                "compileReport": {"compiled": True},
            },
            {},
        )

        self.assertTrue(payload["valid"])
        self.assertTrue(payload["compiled"])
        self.assertEqual(payload["diagnostics"], [])

    def test_widget_tree_result_prunes_outline_slot_data(self) -> None:
        payload = apply_result_transform(
            {"transform": "widget.tree.inspect.result.v1"},
            {
                "rootWidget": {
                    "name": "Root",
                    "slot": {"padding": 4},
                    "children": [{"name": "Title", "slot": {"padding": 2}}],
                }
            },
            {"view": "outline"},
        )

        self.assertEqual(payload["view"], "outline")
        self.assertNotIn("slot", payload["rootWidget"])
        self.assertNotIn("slot", payload["rootWidget"]["children"][0])

    def test_widget_tree_edit_add_from_palette_transform(self) -> None:
        payload = apply_args_transform(
            {"transform": "widget.tree.edit.args.v1"},
            {
                "assetPath": "/Game/UI/WBP_Menu",
                "commands": [
                    {
                        "kind": "addFromPalette",
                        "entry": {
                            "id": "widget.palette:text",
                            "payload": {"widgetClass": "/Script/UMG.TextBlock"},
                        },
                        "name": "TitleText",
                        "parent": {"name": "RootCanvas"},
                    }
                ],
            },
        )

        self.assertEqual(payload["assetPath"], "/Game/UI/WBP_Menu")
        self.assertEqual(payload["ops"][0]["op"], "addWidget")
        self.assertEqual(payload["ops"][0]["args"]["widgetClass"], "/Script/UMG.TextBlock")
        self.assertEqual(payload["ops"][0]["args"]["parentName"], "RootCanvas")

    def test_widget_tree_edit_set_property_transform(self) -> None:
        payload = apply_args_transform(
            {"transform": "widget.tree.edit.args.v1"},
            {
                "assetPath": "/Game/UI/WBP_Menu",
                "commands": [
                    {
                        "kind": "setProperty",
                        "target": {"name": "TitleText"},
                        "property": "Text",
                        "value": "Hello",
                    }
                ],
                "dryRun": True,
            },
        )

        self.assertTrue(payload["dryRun"])
        self.assertEqual(
            payload["ops"][0],
            {
                "op": "setProperty",
                "args": {"name": "TitleText", "property": "Text", "value": "Hello"},
            },
        )

    def test_material_graph_edit_add_from_palette_transform(self) -> None:
        payload = apply_args_transform(
            {"transform": "material.graph.edit.args.v1"},
            {
                "assetPath": "/Game/M_Test",
                "commands": [
                    {
                        "kind": "addFromPalette",
                        "entry": {
                            "id": "material.palette:multiply",
                            "payload": {
                                "nodeClassPath": "/Script/Engine.MaterialExpressionMultiply"
                            },
                        },
                        "alias": "multiply",
                        "position": {"x": 240, "y": 120},
                    }
                ],
            },
        )

        self.assertEqual(payload["ops"][0]["op"], "addNode.byClass")
        self.assertEqual(payload["ops"][0]["clientRef"], "multiply")

    def test_blueprint_graph_edit_add_from_palette_transform(self) -> None:
        payload = apply_args_transform(
            {"transform": "blueprint.graph.edit.args.v1"},
            {
                "assetPath": "/Game/BP_Test",
                "graph": {"name": "EventGraph"},
                "commands": [
                    {
                        "kind": "addFromPalette",
                        "entry": {"id": "palette:branch", "contextSensitive": True},
                        "alias": "branch",
                        "position": {"x": 400, "y": 200},
                    }
                ],
            },
        )

        self.assertEqual(payload["graphName"], "EventGraph")
        self.assertEqual(payload["ops"][0]["op"], "addFromPalette")
        self.assertEqual(payload["ops"][0]["clientRef"], "branch")
        self.assertEqual(payload["ops"][0]["args"]["entryId"], "palette:branch")

    def test_pcg_graph_edit_set_node_property_transform(self) -> None:
        payload = apply_args_transform(
            {"transform": "pcg.graph.edit.args.v1"},
            {
                "assetPath": "/Game/PCG_Test",
                "commands": [
                    {
                        "kind": "setNodeProperty",
                        "node": {"alias": "sampler"},
                        "property": "PointExtents",
                        "value": {"x": 100},
                    }
                ],
            },
        )

        self.assertEqual(payload["ops"][0]["op"], "setProperty")
        self.assertEqual(payload["ops"][0]["clientRef"], "sampler")
        self.assertEqual(payload["ops"][0]["value"], '{"x":100}')


if __name__ == "__main__":
    unittest.main()
