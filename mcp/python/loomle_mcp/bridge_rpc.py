from __future__ import annotations

import asyncio
import itertools
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class BridgeRpcError(RuntimeError):
    pass


@dataclass(frozen=True)
class BridgeRpcInvokeError(BridgeRpcError):
    code: int
    message: str
    retryable: bool
    detail: str | None = None
    payload: dict[str, Any] | None = None


_REQUEST_IDS = itertools.count(1)


async def rpc_health(endpoint: Path) -> dict[str, Any]:
    value = await rpc_request(endpoint, "rpc.health", {})
    if not isinstance(value, dict):
        raise BridgeRpcError("rpc.health returned a non-object result")
    return value


async def rpc_invoke(endpoint: Path, tool: str, args: dict[str, Any]) -> dict[str, Any]:
    value = await rpc_request(
        endpoint,
        "rpc.invoke",
        {
            "tool": tool,
            "args": args,
        },
    )
    if not isinstance(value, dict):
        raise BridgeRpcError(f"rpc.invoke for tool {tool!r} returned a non-object result")
    if not value.get("ok"):
        raise BridgeRpcError(f"rpc.invoke for tool {tool!r} returned a non-ok result envelope")
    payload = value.get("payload", {})
    if not isinstance(payload, dict):
        raise BridgeRpcError(f"rpc.invoke for tool {tool!r} returned a non-object payload")
    return payload


async def rpc_request(endpoint: Path, method: str, params: dict[str, Any]) -> Any:
    if os.name == "nt":
        raise BridgeRpcError("Windows named pipe transport is not implemented in Python MCP yet.")
    return await unix_socket_request(endpoint, method, params)


async def unix_socket_request(endpoint: Path, method: str, params: dict[str, Any]) -> Any:
    if not endpoint.exists():
        raise BridgeRpcError(
            f"expected LOOMLE runtime endpoint was not found: {endpoint}"
        )

    reader, writer = await asyncio.open_unix_connection(str(endpoint))
    try:
        request_id = f"loomle-python-{next(_REQUEST_IDS)}"
        request = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }
        writer.write(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")
        await writer.drain()

        line = await reader.readline()
        if not line:
            raise BridgeRpcError("runtime RPC connection closed before a response was received")

        envelope = json.loads(line.decode("utf-8"))
        if not isinstance(envelope, dict):
            raise BridgeRpcError("runtime RPC response is not an object")
        if str(envelope.get("id")) != request_id:
            raise BridgeRpcError("runtime RPC response id did not match request id")
        if "error" in envelope:
            raise decode_rpc_error(envelope["error"])
        if "result" not in envelope:
            raise BridgeRpcError("runtime RPC response missing both result and error")
        return envelope["result"]
    finally:
        writer.close()
        await writer.wait_closed()


def decode_rpc_error(error: Any) -> BridgeRpcError:
    if not isinstance(error, dict):
        return BridgeRpcError("runtime RPC returned a malformed error")
    data = error.get("data")
    detail = data.get("detail") if isinstance(data, dict) else None
    payload = None
    if isinstance(detail, str):
        try:
            parsed = json.loads(detail)
            if isinstance(parsed, dict):
                payload = parsed
        except json.JSONDecodeError:
            payload = None
    return BridgeRpcInvokeError(
        code=int(error.get("code", 1011)),
        message=str(error.get("message", "INTERNAL_ERROR")),
        retryable=bool(data.get("retryable")) if isinstance(data, dict) else False,
        detail=detail if isinstance(detail, str) else None,
        payload=payload,
    )
