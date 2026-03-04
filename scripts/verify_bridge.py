#!/usr/bin/env python3
import argparse
import json
import os
import socket
import sys
from pathlib import Path
from typing import Any

REQUIRED_TOOLS = {
    "loomle",
    "graph",
    "graph.list",
    "graph.query",
    "graph.addable",
    "graph.mutate",
    "context",
    "execute",
}

EXPECTED_GRAPH_MUTATE_OPS = {
    "addNode.byClass",
    "addNode.byAction",
    "connectPins",
    "disconnectPins",
    "breakPinLinks",
    "setPinDefault",
    "removeNode",
    "moveNode",
    "compile",
    "runScript",
}

def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


class BridgeConnection:
    def send(self, payload: dict[str, Any]) -> None:
        raise NotImplementedError

    def recv_frame(self, method: str, req_id: int) -> dict[str, Any]:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class UnixBridgeConnection(BridgeConnection):
    def __init__(self, socket_path: str, timeout_s: float) -> None:
        sock_path = Path(socket_path)
        if not sock_path.exists():
            fail(f"Socket not found: {sock_path}")

        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout_s)
        try:
            self.sock.connect(str(sock_path))
        except OSError as exc:
            fail(f"Failed to connect socket {sock_path}: {exc}")
        self.pending = b""

    def send(self, payload: dict[str, Any]) -> None:
        self.sock.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))

    def recv_frame(self, method: str, req_id: int) -> dict[str, Any]:
        while True:
            while b"\n" not in self.pending:
                chunk = self.sock.recv(4096)
                if not chunk:
                    fail(f"Socket closed while waiting for response to {method} (id={req_id})")
                self.pending += chunk

            line, _, self.pending = self.pending.partition(b"\n")
            try:
                frame = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError as exc:
                fail(f"Invalid JSON response for {method}: {exc}")

            frame_id = frame.get("id")
            if frame_id != req_id:
                # Bridge may push notifications while waiting for tools/call response.
                continue

            return frame

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


class WindowsPipeConnection(BridgeConnection):
    def __init__(self, pipe_name: str, timeout_s: float) -> None:
        import ctypes

        self.kernel32 = ctypes.windll.kernel32
        self.kernel32.WaitNamedPipeW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32]
        self.kernel32.WaitNamedPipeW.restype = ctypes.c_int

        full_name = f"\\\\.\\pipe\\{pipe_name}"
        timeout_ms = int(timeout_s * 1000)
        if self.kernel32.WaitNamedPipeW(full_name, timeout_ms) == 0:
            fail(f"Named pipe not ready: {full_name}")

        try:
            self.fh = open(full_name, "r+b", buffering=0)
        except OSError as exc:
            fail(f"Failed to open named pipe {full_name}: {exc}")

        self.fd = self.fh.fileno()
        self.pending = b""

    def send(self, payload: dict[str, Any]) -> None:
        wire = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.fh.write(wire)
        self.fh.flush()

    def recv_frame(self, method: str, req_id: int) -> dict[str, Any]:
        while True:
            while b"\n" not in self.pending:
                chunk = os.read(self.fd, 4096)
                if not chunk:
                    fail(f"Named pipe closed while waiting for response to {method} (id={req_id})")
                self.pending += chunk

            line, _, self.pending = self.pending.partition(b"\n")
            try:
                frame = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError as exc:
                fail(f"Invalid JSON response for {method}: {exc}")

            frame_id = frame.get("id")
            if frame_id != req_id:
                # Bridge may push notifications while waiting for tools/call response.
                continue

            return frame

    def close(self) -> None:
        try:
            self.fh.close()
        except OSError:
            pass


def choose_connection(args: argparse.Namespace) -> BridgeConnection:
    if args.transport == "unix":
        if not args.socket:
            fail("--socket is required for --transport unix")
        return UnixBridgeConnection(args.socket, args.timeout)

    if args.transport == "pipe":
        if sys.platform != "win32":
            fail("--transport pipe is only supported on Windows")
        return WindowsPipeConnection(args.pipe_name, args.timeout)

    if sys.platform == "win32":
        return WindowsPipeConnection(args.pipe_name, args.timeout)

    if not args.socket:
        fail("On macOS/Linux please provide --socket")
    return UnixBridgeConnection(args.socket, args.timeout)


def send_jsonrpc(conn: BridgeConnection, req_id: int, method: str, params: dict) -> dict:
    payload = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params,
    }
    conn.send(payload)
    frame = conn.recv_frame(method, req_id)
    if "error" in frame:
        fail(f"JSON-RPC error for {method}: {frame['error']}")
    return frame


def parse_tool_payload(response: dict, method: str) -> dict:
    result = response.get("result")
    if not isinstance(result, dict):
        fail(f"Invalid {method} response: missing result object")

    content = result.get("content")
    if not isinstance(content, list) or not content:
        fail(f"Invalid {method} response: missing content")

    first = content[0]
    if not isinstance(first, dict):
        fail(f"Invalid {method} response: malformed content item")

    text = first.get("text")
    if not isinstance(text, str):
        fail(f"Invalid {method} response: missing text payload")

    try:
        payload = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"Invalid tool payload JSON for {method}: {exc}")

    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify Loomle bridge availability")
    parser.add_argument("--transport", choices=["auto", "unix", "pipe"], default="auto")
    parser.add_argument("--socket", help="Unix socket path, e.g. /.../Intermediate/loomle.sock")
    parser.add_argument("--pipe-name", default="loomle", help="Windows named pipe name (default: loomle)")
    parser.add_argument("--timeout", type=float, default=3.0, help="Connection timeout seconds")
    args = parser.parse_args()

    conn = choose_connection(args)
    try:
        init_resp = send_jsonrpc(conn, 1, "initialize", {})
        protocol_version = init_resp.get("result", {}).get("protocolVersion")
        if not protocol_version:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol_version}")

        tools_resp = send_jsonrpc(conn, 2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        tool_names = {
            tool.get("name") for tool in tools if isinstance(tool, dict) and isinstance(tool.get("name"), str)
        }
        missing = sorted(REQUIRED_TOOLS - tool_names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print(f"[PASS] tools/list includes required baseline tools ({len(REQUIRED_TOOLS)})")

        exec_resp = send_jsonrpc(
            conn,
            3,
            "tools/call",
            {
                "name": "execute",
                "arguments": {
                    "mode": "exec",
                    "code": "import unreal\nassert hasattr(unreal, 'LoomleBlueprintAdapter')",
                },
            },
        )
        exec_payload = parse_tool_payload(exec_resp, "tools/call.execute")
        if exec_payload.get("isError"):
            fail(f"execute failed: {exec_payload.get('message') or exec_payload}")
        print("[PASS] unreal.LoomleBlueprintAdapter is available")

        graph_desc_resp = send_jsonrpc(
            conn,
            40,
            "tools/call",
            {
                "name": "graph",
                "arguments": {"graphType": "blueprint"},
            },
        )
        graph_desc_payload = parse_tool_payload(graph_desc_resp, "tools/call.graph")
        if graph_desc_payload.get("isError"):
            fail(f"graph failed: {graph_desc_payload.get('message') or graph_desc_payload}")
        ops = graph_desc_payload.get("ops")
        if not isinstance(ops, list):
            fail("graph payload missing ops[]")
        ops_set = {op for op in ops if isinstance(op, str)}
        if ops_set != EXPECTED_GRAPH_MUTATE_OPS:
            fail(f"graph ops mismatch. expected={sorted(EXPECTED_GRAPH_MUTATE_OPS)} actual={sorted(ops_set)}")
        print("[PASS] graph reports expected mutate ops")

        run_script_resp = send_jsonrpc(
            conn,
            41,
            "tools/call",
            {
                "name": "graph.mutate",
                "arguments": {
                    "graphType": "blueprint",
                    "assetPath": "/Game/Codex/BP_BridgeVerify",
                    "graphName": "EventGraph",
                    "dryRun": False,
                    "ops": [
                        {
                            "op": "runScript",
                            "args": {
                                "mode": "inlineCode",
                                "entry": "run",
                                "code": "def run(ctx):\n  return {'ok': True, 'assetPath': ctx.get('assetPath', '')}",
                                "input": {"source": "verify_bridge"},
                            },
                        }
                    ],
                },
            },
        )
        run_script_payload = parse_tool_payload(run_script_resp, "tools/call.graph.mutate")
        if run_script_payload.get("isError"):
            fail(f"graph.mutate runScript failed: {run_script_payload.get('message') or run_script_payload}")
        op_results = run_script_payload.get("opResults", [])
        if not isinstance(op_results, list) or not op_results:
            fail("graph.mutate runScript missing opResults")
        first_op = op_results[0] if isinstance(op_results[0], dict) else {}
        if not first_op.get("ok"):
            fail(f"graph.mutate runScript op failed: {first_op}")
        script_result = first_op.get("scriptResult")
        if not isinstance(script_result, dict) or script_result.get("ok") is not True:
            fail(f"graph.mutate runScript missing/invalid scriptResult: {first_op}")
        print("[PASS] graph.mutate runScript inline execution verified")
    finally:
        conn.close()

    print("[PASS] Bridge verification complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
