from __future__ import annotations

import asyncio
import json
import tempfile
import unittest
from pathlib import Path

from loomle_mcp.bridge_rpc import rpc_health


class BridgeRpcTests(unittest.IsolatedAsyncioTestCase):
    async def test_rpc_health_round_trips_over_unix_socket(self) -> None:
        if not hasattr(asyncio, "start_unix_server"):
            self.skipTest("unix sockets are unavailable")

        with tempfile.TemporaryDirectory(dir="/private/tmp") as tmp:
            endpoint = Path(tmp) / "loomle.sock"
            seen: dict[str, object] = {}

            async def handle(
                reader: asyncio.StreamReader,
                writer: asyncio.StreamWriter,
            ) -> None:
                line = await reader.readline()
                request = json.loads(line.decode("utf-8"))
                seen["method"] = request["method"]
                response = {
                    "jsonrpc": "2.0",
                    "id": request["id"],
                    "result": {
                        "status": "Ready",
                        "isPIE": False,
                        "editorBusyReason": "",
                    },
                }
                writer.write(json.dumps(response).encode("utf-8") + b"\n")
                await writer.drain()
                writer.close()
                await writer.wait_closed()

            try:
                server = await asyncio.start_unix_server(handle, str(endpoint))
            except PermissionError as exc:
                self.skipTest(f"unix socket bind is not permitted in this environment: {exc}")
            async with server:
                payload = await rpc_health(endpoint)

            self.assertEqual(seen["method"], "rpc.health")
            self.assertEqual(payload["status"], "Ready")


if __name__ == "__main__":
    unittest.main()
