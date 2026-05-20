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
                self.assertIn("schema.inspect", tool_names)
                self.assertIn("context", tool_names)
                self.assertIn("blueprint.graph.list", tool_names)
                self.assertIn("blueprint.palette", tool_names)
                self.assertIn("blueprint.compile", tool_names)
                self.assertIn("blueprint.graph.edit", tool_names)
                self.assertIn("material.palette", tool_names)
                self.assertIn("pcg.palette", tool_names)
                self.assertIn("widget.palette", tool_names)
                self.assertIn("widget.tree.inspect", tool_names)
                self.assertIn("widget.tree.edit", tool_names)
                self.assertNotIn("project.install", tool_names)

                result = await session.call_tool(
                    "schema.inspect",
                    {
                        "domain": "blueprint",
                        "tool": "blueprint.graph.edit",
                        "operation": "addFromPalette",
                        "include": ["summary", "schema", "errors"],
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
