#!/usr/bin/env python3
import argparse
import json
import socket
import sys
from pathlib import Path

REQUIRED_TOOLS = {
    "loomle",
    "context",
    "live",
    "execute",
}


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


def send_jsonrpc(sock: socket.socket, req_id: int, method: str, params: dict) -> dict:
    payload = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params,
    }
    sock.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))

    data = b""
    while b"\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            fail(f"Socket closed while waiting for response to {method}")
        data += chunk

    line, _, _ = data.partition(b"\n")
    try:
        response = json.loads(line.decode("utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"Invalid JSON response for {method}: {exc}")

    if "error" in response:
        fail(f"JSON-RPC error for {method}: {response['error']}")

    return response


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
    parser = argparse.ArgumentParser(description="Verify Loomle MCP bridge availability")
    parser.add_argument("--socket", required=True, help="Path to loomle-mcp.sock")
    parser.add_argument("--timeout", type=float, default=3.0, help="Socket timeout seconds")
    args = parser.parse_args()

    socket_path = Path(args.socket)
    if not socket_path.exists():
        fail(f"Socket not found: {socket_path}")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(args.timeout)
        try:
            sock.connect(str(socket_path))
        except OSError as exc:
            fail(f"Failed to connect socket {socket_path}: {exc}")

        init_resp = send_jsonrpc(sock, 1, "initialize", {})
        protocol_version = init_resp.get("result", {}).get("protocolVersion")
        if not protocol_version:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol_version}")

        tools_resp = send_jsonrpc(sock, 2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        tool_names = {
            tool.get("name") for tool in tools if isinstance(tool, dict) and isinstance(tool.get("name"), str)
        }
        missing = sorted(REQUIRED_TOOLS - tool_names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print(f"[PASS] tools/list includes required baseline tools ({len(REQUIRED_TOOLS)})")

        exec_resp = send_jsonrpc(
            sock,
            3,
            "tools/call",
            {
                "name": "execute",
                "arguments": {
                    "mode": "exec",
                    "code": "import unreal\nassert hasattr(unreal, 'BlueprintGraphBridge')",
                },
            },
        )
        exec_payload = parse_tool_payload(exec_resp, "tools/call.execute")
        if exec_payload.get("isError"):
            fail(f"execute failed: {exec_payload.get('message') or exec_payload}")
        print("[PASS] unreal.BlueprintGraphBridge is available")

    print("[PASS] Bridge verification complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
