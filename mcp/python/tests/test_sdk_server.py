from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


REPO_ROOT = Path(__file__).resolve().parents[3]


class PythonMcpSdkServerTests(unittest.IsolatedAsyncioTestCase):
    async def test_stdio_server_lists_tools_and_calls_schema_inspect(self) -> None:
        env = os.environ.copy()
        env["PYTHONPATH"] = str(REPO_ROOT / "mcp" / "python")
        params = StdioServerParameters(
            command=sys.executable,
            args=[str(REPO_ROOT / "mcp" / "python" / "loomle_mcp_server.py")],
            env=env,
        )

        async with stdio_client(params) as (read_stream, write_stream):
            async with ClientSession(read_stream, write_stream) as session:
                await session.initialize()

                tools = await session.list_tools()
                tool_names = {tool.name for tool in tools.tools}
                self.assertIn("schema_inspect", tool_names)
                self.assertIn("context", tool_names)
                self.assertIn("blueprint_graph_list", tool_names)
                self.assertIn("blueprint_graph_palette", tool_names)
                self.assertIn("blueprint_compile", tool_names)
                self.assertIn("blueprint_graph_edit", tool_names)
                self.assertIn("material_palette", tool_names)
                self.assertIn("pcg_palette", tool_names)
                self.assertIn("widget_palette", tool_names)
                self.assertIn("widget_tree_inspect", tool_names)
                self.assertIn("widget_tree_edit", tool_names)
                self.assertNotIn("project_install", tool_names)
                palette = next(
                    tool for tool in tools.tools
                    if tool.name == "blueprint_graph_palette"
                )
                self.assertIsNone(palette.outputSchema)
                graph_inspect = next(
                    tool for tool in tools.tools
                    if tool.name == "blueprint_graph_inspect"
                )
                self.assertIsNone(graph_inspect.outputSchema)
                self.assertIn("view", graph_inspect.inputSchema["properties"])
                self.assertIn("rootNode", graph_inspect.inputSchema["properties"])
                self.assertNotIn("filter", graph_inspect.inputSchema["properties"])
                graph_edit = next(
                    tool for tool in tools.tools
                    if tool.name == "blueprint_graph_edit"
                )
                self.assertEqual(
                    graph_edit.meta["schemaHints"][0]["operationFrom"],
                    "commands[].kind",
                )

                output_result = await session.call_tool(
                    "schema_inspect",
                    {
                        "domain": "blueprint",
                        "tool": "blueprint_graph_inspect",
                        "include": ["output"],
                    },
                )
                self.assertFalse(output_result.isError)
                graph_output_schema = output_result.structuredContent["outputSchema"]
                self.assertEqual(
                    [
                        entry["properties"]["view"]["const"]
                        for entry in graph_output_schema["oneOf"]
                        if "view" in entry["properties"]
                    ],
                    ["summary", "exec_flow", "data_flow"],
                )
                input_result = await session.call_tool(
                    "schema_inspect",
                    {
                        "domain": "blueprint",
                        "tool": "blueprint_graph_inspect",
                        "include": ["input"],
                    },
                )
                self.assertFalse(input_result.isError)
                self.assertIn(
                    "rootNode",
                    input_result.structuredContent["inputSchema"]["properties"],
                )

                result = await session.call_tool(
                    "schema_inspect",
                    {
                        "domain": "blueprint",
                        "tool": "blueprint_graph_edit",
                        "operation": "addFromPalette",
                        "include": ["summary", "operation", "errors"],
                    },
                )

                self.assertFalse(result.isError)
                self.assertIsNotNone(result.structuredContent)
                self.assertEqual(
                    result.structuredContent.get("operation"),
                    "addFromPalette",
                )


if __name__ == "__main__":
    unittest.main()
