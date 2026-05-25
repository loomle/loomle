from __future__ import annotations

import json
import os
import site
import sys
import tempfile
import unittest
import asyncio
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


REPO_ROOT = Path(__file__).resolve().parents[3]


class PythonMcpProjectToolTests(unittest.IsolatedAsyncioTestCase):
    async def test_project_list_attach_and_status_use_runtime_registry(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            loomle_home = Path(tmp) / ".loomle"
            project_root = Path(tmp) / "DemoProject"
            plugin_root = Path(tmp) / "FabPlugin" / "LoomleBridge"
            mcp_runtime = plugin_root / "Resources" / "MCP"
            endpoint = project_root / "Intermediate" / "loomle.sock"
            runtime_dir = loomle_home / "state" / "runtimes"
            project_dir = loomle_home / "state" / "projects"
            runtime_dir.mkdir(parents=True)
            project_dir.mkdir(parents=True)
            endpoint.parent.mkdir(parents=True)
            endpoint.write_text("", encoding="utf-8")
            mcp_runtime.mkdir(parents=True)
            (mcp_runtime / "loomle_mcp_server.py").write_text("# test\n", encoding="utf-8")
            codex_dir = Path(tmp) / "home" / ".codex"
            codex_dir.mkdir(parents=True)

            project_id = "demo-project-id"
            runtime_record = {
                "schemaVersion": 1,
                "runtimeId": project_id,
                "projectId": project_id,
                "name": "DemoProject",
                "projectRoot": str(project_root),
                "uproject": str(project_root / "DemoProject.uproject"),
                "endpoint": str(endpoint),
                "platform": "darwin",
                "pid": 123,
                "pluginPath": str(plugin_root),
                "pluginInstallScope": "engine",
                "pluginManagedBy": "fab",
                "pluginVersion": "0.6.0",
                "protocolVersion": 1,
                "startedAt": "2026-05-20T00:00:00Z",
                "lastSeenAt": "2026-05-20T00:00:00Z",
            }
            (runtime_dir / f"{project_id}.json").write_text(
                json.dumps(runtime_record),
                encoding="utf-8",
            )

            python_path_entries = [str(REPO_ROOT / "mcp" / "python")]
            python_path_entries.extend(
                entry
                for entry in os.environ.get("PYTHONPATH", "").split(os.pathsep)
                if entry
            )
            site_package_entries = [
                entry
                for entry in {
                    *getattr(site, "getsitepackages", lambda: [])(),
                    site.getusersitepackages(),
                }
                if entry
            ]
            python_path_entries.extend(site_package_entries)
            for entry in site_package_entries:
                site_path = Path(entry)
                python_path_entries.extend(
                    str(site_path / child)
                    for child in ("win32", "win32/lib", "pywin32_system32")
                )

            env = os.environ.copy()
            env["PYTHONPATH"] = os.pathsep.join(python_path_entries)
            env["LOOMLE_HOME"] = str(loomle_home)
            env["HOME"] = str(Path(tmp) / "home")
            env["USERPROFILE"] = str(Path(tmp) / "home")
            env["APPDATA"] = str(Path(tmp) / "home" / "AppData" / "Roaming")
            params = StdioServerParameters(
                command=sys.executable,
                args=[str(REPO_ROOT / "mcp" / "python" / "loomle_mcp_server.py")],
                env=env,
            )

            async with stdio_client(params) as (read_stream, write_stream):
                async with ClientSession(read_stream, write_stream) as session:
                    await session.initialize()

                    listed = await session.call_tool(
                        "project.list",
                        {"status": "online", "includeDiagnostics": True},
                    )
                    self.assertFalse(listed.isError)
                    projects = listed.structuredContent["projects"]
                    self.assertEqual(len(projects), 1)
                    self.assertEqual(projects[0]["projectId"], project_id)
                    self.assertTrue(projects[0]["attachable"])

                    attached = await session.call_tool(
                        "project.attach",
                        {"projectId": project_id},
                    )
                    self.assertFalse(attached.isError)
                    self.assertTrue(attached.structuredContent["attached"])
                    self.assertEqual(attached.structuredContent["projectId"], project_id)

                    status = await session.call_tool("loomle.status", {})
                    self.assertFalse(status.isError)
                    self.assertTrue(status.structuredContent["attached"])
                    self.assertEqual(status.structuredContent["onlineProjectCount"], 1)

                    setup = await session.call_tool("setup.status", {})
                    self.assertFalse(setup.isError)
                    self.assertEqual(setup.structuredContent["channel"], "fab")
                    self.assertEqual(setup.structuredContent["bridge"]["state"], "ready")
                    self.assertTrue(setup.structuredContent["fabPythonMcp"]["available"])
                    self.assertEqual(
                        setup.structuredContent["recommendation"]["action"],
                        "configureFabPython",
                    )
                    self.assertIn(
                        "configureCodex",
                        setup.structuredContent["recommendation"]["safeAutomaticActions"],
                    )

                    configured = await session.call_tool(
                        "setup.configure",
                        {"host": "codex"},
                    )
                    self.assertFalse(configured.isError)
                    self.assertEqual(configured.structuredContent["serverOwner"], "fab")
                    codex_config = (codex_dir / "config.toml").read_text(encoding="utf-8")
                    self.assertIn("[mcp_servers.loomle]", codex_config)
                    self.assertIn("loomle_mcp_server.py", codex_config)

    async def test_bridge_rpc_dispatch_uses_manifest_target_tool(self) -> None:
        if not hasattr(asyncio, "start_unix_server"):
            self.skipTest("unix sockets are unavailable")

        with tempfile.TemporaryDirectory(dir="/private/tmp") as tmp:
            root = Path(tmp)
            loomle_home = root / ".loomle"
            project_root = root / "DemoProject"
            endpoint = root / "loomle.sock"
            runtime_dir = loomle_home / "state" / "runtimes"
            runtime_dir.mkdir(parents=True)

            project_id = "demo-project-id"
            (runtime_dir / f"{project_id}.json").write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "runtimeId": project_id,
                        "projectId": project_id,
                        "name": "DemoProject",
                        "projectRoot": str(project_root),
                        "endpoint": str(endpoint),
                        "pluginVersion": "0.6.0",
                        "protocolVersion": 1,
                    }
                ),
                encoding="utf-8",
            )

            seen: dict[str, object] = {}

            async def handle(
                reader: asyncio.StreamReader,
                writer: asyncio.StreamWriter,
            ) -> None:
                try:
                    while True:
                        line = await reader.readline()
                        if not line:
                            break
                        request = json.loads(line.decode("utf-8"))
                        seen["method"] = request["method"]
                        seen["tool"] = request["params"]["tool"]
                        response = {
                            "jsonrpc": "2.0",
                            "id": request["id"],
                            "result": {
                                "ok": True,
                                "payload": {
                                    "status": "ok",
                                    "tool": request["params"]["tool"],
                                },
                                "diagnostics": [],
                            },
                        }
                        writer.write(json.dumps(response).encode("utf-8") + b"\n")
                        await writer.drain()
                finally:
                    writer.close()
                    await writer.wait_closed()

            try:
                server = await asyncio.start_unix_server(handle, str(endpoint))
            except PermissionError as exc:
                self.skipTest(f"unix socket bind is not permitted in this environment: {exc}")

            env = os.environ.copy()
            env["PYTHONPATH"] = str(REPO_ROOT / "mcp" / "python")
            env["LOOMLE_HOME"] = str(loomle_home)
            params = StdioServerParameters(
                command=sys.executable,
                args=[str(REPO_ROOT / "mcp" / "python" / "loomle_mcp_server.py")],
                env=env,
            )

            async with server:
                async with stdio_client(params) as (read_stream, write_stream):
                    async with ClientSession(read_stream, write_stream) as session:
                        await session.initialize()
                        result = await session.call_tool(
                            "blueprint.graph.edit",
                            {
                                "assetPath": "/Game/Test/BP_Test",
                                "commands": [
                                    {
                                        "kind": "addFromPalette",
                                        "entry": {"id": "palette:..."},
                                    }
                                ]
                            },
                        )
                server.close()
                await server.wait_closed()

            self.assertFalse(result.isError, result)
            self.assertEqual(result.structuredContent["status"], "ok")
            self.assertEqual(seen["method"], "rpc.invoke")
            self.assertEqual(seen["tool"], "blueprint.graph.edit")


if __name__ == "__main__":
    unittest.main()
