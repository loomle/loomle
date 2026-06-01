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
_SESSIONS: dict[Path, "BridgeRpcSession"] = {}
_SESSIONS_LOCK = asyncio.Lock()


async def rpc_health(endpoint: Path) -> dict[str, Any]:
    value = await rpc_request(endpoint, "rpc.health", {})
    if not isinstance(value, dict):
        raise BridgeRpcError("rpc.health returned a non-object result")
    return value


async def rpc_capabilities(endpoint: Path) -> dict[str, Any]:
    value = await rpc_request(endpoint, "rpc.capabilities", {})
    if not isinstance(value, dict):
        raise BridgeRpcError("rpc.capabilities returned a non-object result")
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
    for attempt in range(2):
        session = await get_session(endpoint)
        try:
            return await session.request(method, params)
        except BridgeRpcError:
            await remove_session(endpoint, session)
            if attempt == 0:
                continue
            raise
    raise BridgeRpcError("runtime RPC session retry failed")


async def get_session(endpoint: Path) -> "BridgeRpcSession":
    endpoint = endpoint.resolve()
    async with _SESSIONS_LOCK:
        session = _SESSIONS.get(endpoint)
        if session is not None and not session.closed:
            return session
        session = await BridgeRpcSession.connect(endpoint)
        _SESSIONS[endpoint] = session
        return session


async def remove_session(endpoint: Path, session: "BridgeRpcSession") -> None:
    endpoint = endpoint.resolve()
    async with _SESSIONS_LOCK:
        if _SESSIONS.get(endpoint) is session:
            _SESSIONS.pop(endpoint, None)
    await session.close()


async def close_all_sessions() -> None:
    async with _SESSIONS_LOCK:
        sessions = list(_SESSIONS.values())
        _SESSIONS.clear()
    await asyncio.gather(*(session.close() for session in sessions), return_exceptions=True)


class BridgeRpcSession:
    def __init__(
        self,
        endpoint: Path,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        self.endpoint = endpoint
        self.reader = reader
        self.writer = writer
        self.pending: dict[str, asyncio.Future[Any]] = {}
        self.write_lock = asyncio.Lock()
        self.closed = False
        self._close_started = False
        self.reader_task = asyncio.create_task(self._read_loop())

    @classmethod
    async def connect(cls, endpoint: Path) -> "BridgeRpcSession":
        reader, writer = await open_unix_connection(endpoint)
        return cls(endpoint, reader, writer)

    async def request(self, method: str, params: dict[str, Any]) -> Any:
        if self.closed:
            raise BridgeRpcError("runtime RPC connection is closed")
        request_id = f"loomle-python-{next(_REQUEST_IDS)}"
        loop = asyncio.get_running_loop()
        future: asyncio.Future[Any] = loop.create_future()
        self.pending[request_id] = future
        request = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }
        try:
            async with self.write_lock:
                self.writer.write(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")
                await self.writer.drain()
        except Exception as exc:
            self.pending.pop(request_id, None)
            if not future.done():
                future.cancel()
            await self.close()
            raise BridgeRpcError(f"failed to write runtime RPC request: {exc}") from exc
        return await future

    async def _read_loop(self) -> None:
        try:
            while not self.closed:
                line = await self.reader.readline()
                if not line:
                    raise BridgeRpcError("runtime RPC connection closed")
                try:
                    envelope = json.loads(line.decode("utf-8"))
                    if not isinstance(envelope, dict):
                        raise BridgeRpcError("runtime RPC response is not an object")
                    response_id = str(envelope.get("id"))
                    future = self.pending.pop(response_id, None)
                    if future is None:
                        continue
                    if "error" in envelope:
                        future.set_exception(decode_rpc_error(envelope["error"]))
                    elif "result" in envelope:
                        future.set_result(envelope["result"])
                    else:
                        future.set_exception(BridgeRpcError("runtime RPC response missing both result and error"))
                except Exception as exc:
                    if isinstance(exc, BridgeRpcError):
                        raise
                    raise BridgeRpcError(f"failed to decode runtime RPC response: {exc}") from exc
        except Exception as exc:
            self.closed = True
            error = exc if isinstance(exc, BridgeRpcError) else BridgeRpcError(str(exc))
            for future in list(self.pending.values()):
                if not future.done():
                    future.set_exception(error)
            self.pending.clear()
        finally:
            self.closed = True

    async def close(self) -> None:
        if self._close_started:
            return
        self._close_started = True
        self.closed = True
        self.writer.close()
        if not self.reader_task.done():
            self.reader_task.cancel()
        try:
            await asyncio.wait_for(self.writer.wait_closed(), timeout=1.0)
        except Exception:
            pass
        if not self.reader_task.done():
            try:
                await self.reader_task
            except asyncio.CancelledError:
                pass
            except Exception:
                pass
        for future in list(self.pending.values()):
            if not future.done():
                future.set_exception(BridgeRpcError("runtime RPC session closed"))
        self.pending.clear()


async def open_unix_connection(endpoint: Path) -> tuple[asyncio.StreamReader, asyncio.StreamWriter]:
    if not endpoint.exists():
        raise BridgeRpcError(
            f"expected LOOMLE runtime endpoint was not found: {endpoint}"
        )

    return await asyncio.open_unix_connection(str(endpoint))


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
