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

    def test_asset_create_user_defined_struct_dispatches_current_bridge_tool(self) -> None:
        payload = apply_args_transform(
            {"transform": "asset.create.args.v1"},
            {
                "kind": "userDefinedStruct",
                "assetPath": "/Game/ST_Test",
                "fields": [{"name": "DisplayName", "type": {"category": "text"}}],
            },
        )

        self.assertEqual(payload["__bridgeTool"], "blueprint.struct.edit")
        self.assertEqual(payload["operation"], "create")
        self.assertEqual(payload["args"]["fields"][0]["name"], "DisplayName")

    def test_asset_inspect_dispatches_current_bridge_tools(self) -> None:
        cases = [
            ("enum", "blueprint.enum.inspect"),
            ("userDefinedStruct", "blueprint.struct.inspect"),
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

    def test_asset_edit_user_defined_struct_dispatches_current_bridge_tool(self) -> None:
        payload = apply_args_transform(
            {"transform": "asset.edit.args.v1"},
            {
                "kind": "userDefinedStruct",
                "assetPath": "/Game/ST_Test",
                "operation": "addField",
                "args": {"name": "Score", "type": {"category": "int"}},
            },
        )

        self.assertEqual(payload["__bridgeTool"], "blueprint.struct.edit")
        self.assertEqual(payload["operation"], "addField")
        self.assertEqual(payload["args"]["name"], "Score")

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

    def test_blueprint_graph_summary_uses_node_index_and_refs(self) -> None:
        payload = apply_result_transform(
            {"transform": "blueprint.graph.inspect.result.v1"},
            {
                "semanticSnapshot": {
                    "nodes": [
                        {
                            "id": "event",
                            "className": "K2Node_Event",
                            "title": "Event BeginPlay",
                            "pins": [
                                {
                                    "name": "Then",
                                    "direction": "output",
                                    "category": "exec",
                                    "linkedTo": [{"nodeGuid": "print", "pin": "execute"}],
                                }
                            ],
                        },
                        {
                            "id": "print",
                            "className": "K2Node_CallFunction",
                            "title": "Print String",
                            "pins": [
                                {
                                    "name": "execute",
                                    "direction": "input",
                                    "category": "exec",
                                    "linkedTo": [{"nodeGuid": "event", "pin": "Then"}],
                                }
                            ],
                        },
                    ]
                }
            },
            {"view": "summary"},
        )

        self.assertEqual(payload["roots"], [{"id": "event"}])
        self.assertIn("event", payload["nodes"])
        self.assertNotIn("pins", payload["nodes"]["event"])
        self.assertEqual(payload["chains"][0]["root"], {"id": "event"})
        self.assertEqual(payload["chains"][0]["path"], [{"id": "event"}, {"id": "print"}])

    def test_blueprint_graph_exec_flow_returns_nodes_and_links(self) -> None:
        payload = apply_result_transform(
            {"transform": "blueprint.graph.inspect.result.v1"},
            {
                "semanticSnapshot": {
                    "nodes": [
                        {
                            "id": "event",
                            "className": "K2Node_Event",
                            "pins": [
                                {
                                    "name": "Then",
                                    "direction": "output",
                                    "category": "exec",
                                    "linkedTo": [{"nodeGuid": "print", "pin": "execute"}],
                                }
                            ],
                        },
                        {
                            "id": "print",
                            "className": "K2Node_CallFunction",
                            "pins": [
                                {
                                    "name": "execute",
                                    "direction": "input",
                                    "category": "exec",
                                    "linkedTo": [{"nodeGuid": "event", "pin": "Then"}],
                                }
                            ],
                        },
                    ]
                }
            },
            {"view": "exec_flow", "rootNode": {"id": "event"}},
        )

        self.assertEqual(payload["rootNode"], {"id": "event"})
        self.assertEqual(len(payload["nodes"]), 2)
        self.assertEqual(len(payload["links"]), 1)
        self.assertNotIn("flow", payload)

    def test_blueprint_graph_flow_targets_return_errors(self) -> None:
        payload = apply_result_transform(
            {"transform": "blueprint.graph.inspect.result.v1"},
            {
                "semanticSnapshot": {
                    "nodes": [
                        {
                            "id": "event",
                            "className": "K2Node_Event",
                            "pins": [
                                {"name": "Then", "direction": "output", "category": "exec"},
                            ],
                        },
                    ]
                }
            },
            {"view": "data_flow", "rootPin": {"node": {"id": "event"}, "pin": "Missing"}},
        )

        self.assertTrue(payload["isError"])
        self.assertEqual(payload["code"], "PIN_NOT_FOUND")

    def test_blueprint_graph_traversal_bounds_are_enforced(self) -> None:
        with self.assertRaises(TransformError):
            apply_args_transform(
                {"transform": "blueprint.graph.inspect.args.v1"},
                {
                    "assetPath": "/Game/BP_Test",
                    "graph": {"name": "EventGraph"},
                    "view": "exec_flow",
                    "rootNode": {"id": "event"},
                    "traversal": {"maxNodes": 1001},
                },
            )

    def test_blueprint_member_inspect_custom_event_filters_engine_events(self) -> None:
        payload = apply_result_transform(
            {"transform": "blueprint.member.inspect.result.v1"},
            {
                "assetPath": "/Game/BP_Test",
                "eventSignatures": [
                    {"name": "ReceiveBeginPlay", "isCustomEvent": False, "eventKind": "engine"},
                    {"name": "DoWork", "isCustomEvent": True, "eventKind": "custom"},
                ],
            },
            {"memberKind": "customEvent"},
        )

        self.assertEqual(payload["memberKind"], "customEvent")
        self.assertEqual([item["name"] for item in payload["items"]], ["DoWork"])

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
                "dryRun": True,
                "continueOnError": True,
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
        self.assertTrue(payload["dryRun"])
        self.assertNotIn("continueOnError", payload)
        self.assertEqual(payload["ops"][0]["op"], "addFromPalette")
        self.assertEqual(payload["ops"][0]["clientRef"], "branch")
        self.assertEqual(payload["ops"][0]["args"]["entryId"], "palette:branch")

        with self.assertRaises(TransformError):
            apply_args_transform(
                {"transform": "blueprint.graph.edit.args.v1"},
                {
                    "assetPath": "/Game/BP_Test",
                    "graphName": "EventGraph",
                    "commands": [{"kind": "removeNode", "node": {"id": "node-1"}}],
                },
            )

    def test_blueprint_graph_palette_transform_normalizes_public_pins(self) -> None:
        payload = apply_args_transform(
            {"transform": "blueprint.graph.palette.args.v1"},
            {
                "assetPath": "/Game/BP_Test",
                "graph": {"name": "EventGraph"},
                "limit": 500,
                "fromPins": [{"node": {"id": "node-1"}, "pin": "Then"}],
            },
        )

        self.assertEqual(payload["graphName"], "EventGraph")
        self.assertEqual(payload["fromPins"], [{"nodeId": "node-1", "pin": "Then"}])
        with self.assertRaises(TransformError):
            apply_args_transform(
                {"transform": "blueprint.graph.palette.args.v1"},
                {
                    "assetPath": "/Game/BP_Test",
                    "graph": {"name": "EventGraph"},
                    "limit": 501,
                },
            )

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
