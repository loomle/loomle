#!/usr/bin/env python3
import argparse
import json
import os
import queue
import socket
import statistics
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    if p <= 0:
        return sorted_values[0]
    if p >= 100:
        return sorted_values[-1]
    rank = (len(sorted_values) - 1) * (p / 100.0)
    lo = int(rank)
    hi = min(lo + 1, len(sorted_values) - 1)
    frac = rank - lo
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac


class BridgeConnection:
    def send(self, payload: dict[str, Any]) -> None:
        raise NotImplementedError

    def recv_frame(self) -> dict[str, Any]:
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
            fail(f"Failed to connect unix socket {sock_path}: {exc}")
        self._pending = b""
        self._send_lock = threading.Lock()

    def send(self, payload: dict[str, Any]) -> None:
        wire = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        with self._send_lock:
            self.sock.sendall(wire)

    def recv_frame(self) -> dict[str, Any]:
        while b"\n" not in self._pending:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("socket closed while receiving")
            self._pending += chunk
        line, _, self._pending = self._pending.partition(b"\n")
        try:
            return json.loads(line.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"invalid json frame: {exc}") from exc

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
        self._pending = b""
        self._send_lock = threading.Lock()

    def send(self, payload: dict[str, Any]) -> None:
        wire = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        with self._send_lock:
            self.fh.write(wire)
            self.fh.flush()

    def recv_frame(self) -> dict[str, Any]:
        while b"\n" not in self._pending:
            chunk = os.read(self.fd, 4096)
            if not chunk:
                raise RuntimeError("named pipe closed while receiving")
            self._pending += chunk
        line, _, self._pending = self._pending.partition(b"\n")
        try:
            return json.loads(line.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"invalid json frame: {exc}") from exc

    def close(self) -> None:
        try:
            self.fh.close()
        except OSError:
            pass


class JsonRpcClient:
    def __init__(self, conn: BridgeConnection, timeout_s: float) -> None:
        self.conn = conn
        self.timeout_s = timeout_s
        self._next_id = 1
        self._id_lock = threading.Lock()
        self._pending: dict[int, "queue.Queue[dict[str, Any]]"] = {}
        self._pending_lock = threading.Lock()
        self._stop = threading.Event()
        self._reader_error: Optional[str] = None
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        self.conn.close()
        self._reader.join(timeout=1.0)

    def _reader_loop(self) -> None:
        try:
            while not self._stop.is_set():
                frame = self.conn.recv_frame()
                frame_id = frame.get("id")
                if isinstance(frame_id, (int, float)):
                    req_id = int(frame_id)
                    with self._pending_lock:
                        mailbox = self._pending.get(req_id)
                    if mailbox is not None:
                        mailbox.put(frame)
        except Exception as exc:
            self._reader_error = str(exc)
            self._stop.set()
            with self._pending_lock:
                mailboxes = list(self._pending.values())
            for mailbox in mailboxes:
                mailbox.put({"_reader_error": self._reader_error})

    def request(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self._reader_error:
            raise RuntimeError(self._reader_error)
        with self._id_lock:
            req_id = self._next_id
            self._next_id += 1
        mailbox: "queue.Queue[dict[str, Any]]" = queue.Queue(maxsize=1)
        with self._pending_lock:
            self._pending[req_id] = mailbox
        payload = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}
        try:
            self.conn.send(payload)
            response = mailbox.get(timeout=self.timeout_s)
        except queue.Empty as exc:
            raise RuntimeError(f"timeout waiting for response id={req_id}") from exc
        finally:
            with self._pending_lock:
                self._pending.pop(req_id, None)
        if "_reader_error" in response:
            raise RuntimeError(str(response["_reader_error"]))
        return response

    def call_tool(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        response = self.request("tools/call", {"name": name, "arguments": arguments})
        if "error" in response:
            raise RuntimeError(f"JSON-RPC error on {name}: {response['error']}")
        result = response.get("result")
        if not isinstance(result, dict):
            raise RuntimeError(f"Malformed tools/call response for {name}: missing result")
        content = result.get("content")
        if not isinstance(content, list) or not content:
            raise RuntimeError(f"Malformed tools/call response for {name}: missing content")
        first = content[0]
        if not isinstance(first, dict) or not isinstance(first.get("text"), str):
            raise RuntimeError(f"Malformed tools/call response for {name}: missing text")
        payload = json.loads(first["text"])
        return payload


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


@dataclass
class BenchResult:
    name: str
    total: int
    ok: int
    errors: int
    wall_s: float
    latencies_ms: list[float]


def run_case(
    client: JsonRpcClient,
    case_name: str,
    tool_name: str,
    arguments: dict[str, Any],
    total: int,
    concurrency: int,
    warmup: int,
) -> BenchResult:
    for _ in range(warmup):
        _ = client.call_tool(tool_name, arguments)

    latencies_ms: list[float] = []
    ok = 0
    errors = 0
    next_index = 0
    lock = threading.Lock()

    def worker() -> None:
        nonlocal next_index, ok, errors
        while True:
            with lock:
                if next_index >= total:
                    return
                next_index += 1
            start = time.perf_counter()
            try:
                payload = client.call_tool(tool_name, arguments)
                elapsed_ms = (time.perf_counter() - start) * 1000.0
                is_error = bool(payload.get("isError"))
                with lock:
                    if is_error:
                        errors += 1
                    else:
                        ok += 1
                        latencies_ms.append(elapsed_ms)
            except Exception:
                with lock:
                    errors += 1

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(max(1, concurrency))]
    wall_start = time.perf_counter()
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    wall_s = time.perf_counter() - wall_start
    return BenchResult(
        name=case_name,
        total=total,
        ok=ok,
        errors=errors,
        wall_s=wall_s,
        latencies_ms=latencies_ms,
    )


def print_result(result: BenchResult) -> None:
    sorted_lat = sorted(result.latencies_ms)
    sent = result.ok + result.errors
    err_rate = (result.errors / sent * 100.0) if sent else 0.0
    throughput = (sent / result.wall_s) if result.wall_s > 0 else 0.0
    if sorted_lat:
        min_ms = f"{sorted_lat[0]:.2f}"
        p50_ms = f"{percentile(sorted_lat, 50):.2f}"
        p95_ms = f"{percentile(sorted_lat, 95):.2f}"
        p99_ms = f"{percentile(sorted_lat, 99):.2f}"
        max_ms = f"{sorted_lat[-1]:.2f}"
        mean_ms = f"{statistics.fmean(sorted_lat):.2f}"
    else:
        min_ms = p50_ms = p95_ms = p99_ms = max_ms = mean_ms = "n/a"

    print(
        f"{result.name},{sent},{result.ok},{result.errors},{err_rate:.2f}%,"
        f"{min_ms},{p50_ms},{p95_ms},{p99_ms},{max_ms},{mean_ms},{throughput:.2f},{result.wall_s:.3f}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create temporary Blueprint asset, stress graph.query/graph.mutate, and clean up."
    )
    parser.add_argument("--transport", choices=["auto", "unix", "pipe"], default="auto")
    parser.add_argument("--socket", help="Unix socket path, e.g. /.../Intermediate/loomle.sock")
    parser.add_argument("--pipe-name", default="loomle", help="Windows named pipe name")
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds")
    parser.add_argument("--query-total", type=int, default=1500)
    parser.add_argument("--query-concurrency", type=int, default=16)
    parser.add_argument("--query-warmup", type=int, default=80)
    parser.add_argument("--mutate-total", type=int, default=600)
    parser.add_argument("--mutate-concurrency", type=int, default=8)
    parser.add_argument("--mutate-warmup", type=int, default=40)
    parser.add_argument("--add-total", type=int, default=800)
    parser.add_argument("--add-concurrency", type=int, default=8)
    parser.add_argument("--add-warmup", type=int, default=40)
    parser.add_argument("--connect-total", type=int, default=800)
    parser.add_argument("--connect-concurrency", type=int, default=8)
    parser.add_argument("--connect-warmup", type=int, default=40)
    parser.add_argument("--remove-total", type=int, default=800)
    parser.add_argument("--remove-concurrency", type=int, default=8)
    parser.add_argument("--remove-warmup", type=int, default=40)
    parser.add_argument("--compile-total", type=int, default=120)
    parser.add_argument("--compile-concurrency", type=int, default=2)
    parser.add_argument("--compile-warmup", type=int, default=10)
    args = parser.parse_args()

    conn = choose_connection(args)
    client = JsonRpcClient(conn, timeout_s=args.timeout)
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    asset_path = f"/Game/Codex/BP_GraphStress_{timestamp}"
    graph_name = "EventGraph"

    print(f"temp_asset={asset_path}")
    print("case,total,ok,errors,error_rate_pct,min_ms,p50_ms,p95_ms,p99_ms,max_ms,mean_ms,throughput_rps,wall_s")

    try:
        init = client.request("initialize", {})
        if "error" in init:
            fail(f"initialize failed: {init['error']}")
        _ = client.request("tools/list", {})

        create_code = (
            "import unreal, json\n"
            "B = unreal.LoomleBlueprintAdapter\n"
            f"asset = '{asset_path}'\n"
            "obj_path, err = B.create_blueprint(asset, '/Script/Engine.Actor')\n"
            "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
            "print(json.dumps({'objPath': obj_path, 'err': err, 'exists': exists}, ensure_ascii=False))\n"
        )
        create_payload = client.call_tool("execute", {"mode": "exec", "code": create_code})
        if create_payload.get("isError"):
            fail(f"failed to create temp asset: {create_payload}")

        query_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "limit": 200,
        }

        mutate_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "runScript",
                    "args": {
                        "mode": "inlineCode",
                        "entry": "run",
                        "code": "def run(ctx):\n  return {'ok': True, 'source': 'stress'}",
                        "input": {"source": "graph_stress_bench"},
                    },
                }
            ],
        }

        setup_nodes_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "branch_a",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 0, "y": 0},
                    },
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "branch_b",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 280, "y": 0},
                    },
                },
            ],
        }
        setup_payload = client.call_tool("graph.mutate", setup_nodes_args)
        if setup_payload.get("isError"):
            fail(f"setup nodes failed: {setup_payload}")
        op_results = setup_payload.get("opResults", [])
        if not isinstance(op_results, list) or len(op_results) < 2:
            fail("setup nodes missing opResults")
        branch_a = op_results[0].get("nodeId")
        branch_b = op_results[1].get("nodeId")
        if not branch_a or not branch_b:
            fail(f"setup nodes missing node ids: {op_results}")

        add_rollback_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "tmp_add",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 520, "y": 0},
                    },
                },
                {
                    "op": "removeNode",
                    "args": {
                        "target": {"nodeRef": "tmp_add"},
                    },
                },
            ],
        }

        connect_rollback_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "connectPins",
                    "args": {
                        "from": {"nodeId": branch_a, "pin": "then"},
                        "to": {"nodeId": branch_b, "pin": "execute"},
                    },
                },
                {
                    "op": "disconnectPins",
                    "args": {
                        "from": {"nodeId": branch_a, "pin": "then"},
                        "to": {"nodeId": branch_b, "pin": "execute"},
                    },
                },
            ],
        }

        remove_rollback_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {
                    "op": "addNode.byClass",
                    "clientRef": "tmp_remove_1",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 800, "y": 0},
                    },
                },
                {
                    "op": "addNode.byClass",
                    "clientRef": "tmp_remove_2",
                    "args": {
                        "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse",
                        "position": {"x": 1080, "y": 0},
                    },
                },
                {
                    "op": "removeNode",
                    "args": {
                        "target": {"nodeRef": "tmp_remove_1"},
                    },
                },
                {
                    "op": "removeNode",
                    "args": {
                        "target": {"nodeRef": "tmp_remove_2"},
                    },
                },
            ],
        }

        compile_args = {
            "assetPath": asset_path,
            "graphName": graph_name,
            "graphType": "blueprint",
            "ops": [
                {"op": "compile"},
            ],
        }

        query_result = run_case(
            client=client,
            case_name="graph.query",
            tool_name="graph.query",
            arguments=query_args,
            total=max(1, args.query_total),
            concurrency=max(1, args.query_concurrency),
            warmup=max(0, args.query_warmup),
        )
        print_result(query_result)

        mutate_result = run_case(
            client=client,
            case_name="graph.mutate.runScript",
            tool_name="graph.mutate",
            arguments=mutate_args,
            total=max(1, args.mutate_total),
            concurrency=max(1, args.mutate_concurrency),
            warmup=max(0, args.mutate_warmup),
        )
        print_result(mutate_result)

        add_result = run_case(
            client=client,
            case_name="graph.mutate.add.rollback",
            tool_name="graph.mutate",
            arguments=add_rollback_args,
            total=max(1, args.add_total),
            concurrency=max(1, args.add_concurrency),
            warmup=max(0, args.add_warmup),
        )
        print_result(add_result)

        connect_result = run_case(
            client=client,
            case_name="graph.mutate.connect.rollback",
            tool_name="graph.mutate",
            arguments=connect_rollback_args,
            total=max(1, args.connect_total),
            concurrency=max(1, args.connect_concurrency),
            warmup=max(0, args.connect_warmup),
        )
        print_result(connect_result)

        remove_result = run_case(
            client=client,
            case_name="graph.mutate.remove.rollback",
            tool_name="graph.mutate",
            arguments=remove_rollback_args,
            total=max(1, args.remove_total),
            concurrency=max(1, args.remove_concurrency),
            warmup=max(0, args.remove_warmup),
        )
        print_result(remove_result)

        compile_result = run_case(
            client=client,
            case_name="graph.mutate.compile",
            tool_name="graph.mutate",
            arguments=compile_args,
            total=max(1, args.compile_total),
            concurrency=max(1, args.compile_concurrency),
            warmup=max(0, args.compile_warmup),
        )
        print_result(compile_result)

        all_errors = (
            query_result.errors
            + mutate_result.errors
            + add_result.errors
            + connect_result.errors
            + remove_result.errors
            + compile_result.errors
        )
        return 0 if all_errors == 0 else 2
    finally:
        try:
            delete_code = (
                "import unreal, json\n"
                f"asset = '{asset_path}'\n"
                "exists_before = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                "deleted = unreal.EditorAssetLibrary.delete_asset(asset)\n"
                "exists_after = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                "print(json.dumps({'existsBefore': exists_before, 'deleted': deleted, 'existsAfter': exists_after}, ensure_ascii=False))\n"
            )
            payload = client.call_tool("execute", {"mode": "exec", "code": delete_code})
            print(f"cleanup={json.dumps(payload, ensure_ascii=False)}")
        except Exception as exc:
            print(f"cleanup_error={exc}")
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
