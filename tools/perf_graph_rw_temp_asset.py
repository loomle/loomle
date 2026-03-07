#!/usr/bin/env python3
import argparse
import json
import queue
import socket
import statistics
import subprocess
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


class McpStdioConnection(BridgeConnection):
    def __init__(self, project_root: str, server_binary_path: str, timeout_s: float) -> None:
        project = Path(project_root).resolve()
        server_binary = Path(server_binary_path).resolve()
        if not project.exists():
            fail(f"Project root not found: {project}")
        if not any(project.glob("*.uproject")):
            fail(f"No .uproject found under: {project}")
        if not server_binary.exists():
            fail(f"mcp_server binary not found: {server_binary}")
        if not server_binary.is_file():
            fail(f"mcp_server binary path is not a file: {server_binary}")

        plugin_root = project / "Plugins" / "LoomleBridge"
        if not plugin_root.is_dir():
            fail(f"LoomleBridge plugin root not found: {plugin_root}")
        self.proc = subprocess.Popen(
            [str(server_binary), "--project-root", str(project)],
            cwd=str(plugin_root),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.timeout_s = timeout_s
        self._send_lock = threading.Lock()

    def send(self, payload: dict[str, Any]) -> None:
        if self.proc.stdin is None:
            raise RuntimeError("mcp stdio stdin is not available")
        wire = json.dumps(payload, separators=(",", ":")) + "\n"
        with self._send_lock:
            self.proc.stdin.write(wire)
            self.proc.stdin.flush()

    def recv_frame(self) -> dict[str, Any]:
        if self.proc.stdout is None:
            raise RuntimeError("mcp stdio stdout is not available")
        deadline = time.time() + self.timeout_s
        while time.time() < deadline:
            if self.proc.poll() is not None:
                err = ""
                if self.proc.stderr is not None:
                    err = self.proc.stderr.read().strip()
                raise RuntimeError(f"mcp_server exited early: {err}")
            line = self.proc.stdout.readline()
            if not line:
                time.sleep(0.01)
                continue
            line = line.strip()
            if not line:
                continue
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
        raise RuntimeError("timeout waiting for mcp stdio frame")

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()


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
        structured = result.get("structuredContent")
        if isinstance(structured, dict):
            return structured
        content = result.get("content")
        if not isinstance(content, list) or not content:
            raise RuntimeError(f"Malformed tools/call response for {name}: missing content")
        first = content[0]
        if not isinstance(first, dict) or not isinstance(first.get("text"), str):
            raise RuntimeError(f"Malformed tools/call response for {name}: missing text")
        payload = json.loads(first["text"])
        return payload


def choose_connection(args: argparse.Namespace) -> BridgeConnection:
    project_root = Path(args.project_root).resolve()
    if args.mcp_server_bin:
        server_binary = Path(args.mcp_server_bin).resolve()
    else:
        if sys.platform == "darwin":
            platform_dir = "darwin"
            binary_name = "loomle_mcp_server"
        elif sys.platform.startswith("linux"):
            platform_dir = "linux"
            binary_name = "loomle_mcp_server"
        elif sys.platform.startswith("win"):
            platform_dir = "windows"
            binary_name = "loomle_mcp_server.exe"
        else:
            fail(f"unsupported platform for mcp_server binary: {sys.platform}")
            raise RuntimeError("unreachable")
        server_binary = project_root / "Plugins" / "LoomleBridge" / "Tools" / "mcp" / platform_dir / binary_name

    return McpStdioConnection(
        project_root=str(project_root),
        server_binary_path=str(server_binary),
        timeout_s=args.timeout,
    )


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
    parser.add_argument(
        "--project-root",
        required=True,
        help="UE project root for stdio mode, e.g. /.../UnrealProjects/Loombed",
    )
    parser.add_argument(
        "--mcp-server-bin",
        default="",
        help="Override path to mcp_server binary. Defaults to <project>/Plugins/LoomleBridge/Tools/mcp/<platform>/...",
    )
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds")
    parser.add_argument("--output", default="", help="Optional CSV output file path")
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
    header = "tool,total,ok,errors,error_rate_pct,min_ms,p50_ms,p95_ms,p99_ms,max_ms,mean_ms,throughput_rps,wall_s"
    csv_lines: list[str] = [header]
    print(header)

    try:
        init = client.request("initialize", {})
        if "error" in init:
            fail(f"initialize failed: {init['error']}")
        _ = client.request("tools/list", {})

        create_code = (
            "import unreal, json\n"
            f"asset = '{asset_path}'\n"
            "pkg_path, asset_name = asset.rsplit('/', 1)\n"
            "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
            "factory = unreal.BlueprintFactory()\n"
            "factory.set_editor_property('ParentClass', unreal.Actor)\n"
            "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
            "obj_path = bp.get_path_name() if bp else ''\n"
            "err = '' if bp else 'create_asset returned None'\n"
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
        csv_lines.append(
            f"{query_result.name},{query_result.ok + query_result.errors},{query_result.ok},{query_result.errors},"
            f"{(query_result.errors / (query_result.ok + query_result.errors) * 100.0) if (query_result.ok + query_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(query_result.latencies_ms)[0]:.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(query_result.latencies_ms), 50):.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(query_result.latencies_ms), 95):.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(query_result.latencies_ms), 99):.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(query_result.latencies_ms)[-1]:.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(query_result.latencies_ms):.2f}' if query_result.latencies_ms else 'n/a'},"
            f"{((query_result.ok + query_result.errors) / query_result.wall_s) if query_result.wall_s > 0 else 0.0:.2f},"
            f"{query_result.wall_s:.3f}"
        )

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
        csv_lines.append(
            f"{mutate_result.name},{mutate_result.ok + mutate_result.errors},{mutate_result.ok},{mutate_result.errors},"
            f"{(mutate_result.errors / (mutate_result.ok + mutate_result.errors) * 100.0) if (mutate_result.ok + mutate_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(mutate_result.latencies_ms)[0]:.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(mutate_result.latencies_ms), 50):.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(mutate_result.latencies_ms), 95):.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(mutate_result.latencies_ms), 99):.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(mutate_result.latencies_ms)[-1]:.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(mutate_result.latencies_ms):.2f}' if mutate_result.latencies_ms else 'n/a'},"
            f"{((mutate_result.ok + mutate_result.errors) / mutate_result.wall_s) if mutate_result.wall_s > 0 else 0.0:.2f},"
            f"{mutate_result.wall_s:.3f}"
        )

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
        csv_lines.append(
            f"{add_result.name},{add_result.ok + add_result.errors},{add_result.ok},{add_result.errors},"
            f"{(add_result.errors / (add_result.ok + add_result.errors) * 100.0) if (add_result.ok + add_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(add_result.latencies_ms)[0]:.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(add_result.latencies_ms), 50):.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(add_result.latencies_ms), 95):.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(add_result.latencies_ms), 99):.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(add_result.latencies_ms)[-1]:.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(add_result.latencies_ms):.2f}' if add_result.latencies_ms else 'n/a'},"
            f"{((add_result.ok + add_result.errors) / add_result.wall_s) if add_result.wall_s > 0 else 0.0:.2f},"
            f"{add_result.wall_s:.3f}"
        )

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
        csv_lines.append(
            f"{connect_result.name},{connect_result.ok + connect_result.errors},{connect_result.ok},{connect_result.errors},"
            f"{(connect_result.errors / (connect_result.ok + connect_result.errors) * 100.0) if (connect_result.ok + connect_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(connect_result.latencies_ms)[0]:.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(connect_result.latencies_ms), 50):.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(connect_result.latencies_ms), 95):.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(connect_result.latencies_ms), 99):.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(connect_result.latencies_ms)[-1]:.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(connect_result.latencies_ms):.2f}' if connect_result.latencies_ms else 'n/a'},"
            f"{((connect_result.ok + connect_result.errors) / connect_result.wall_s) if connect_result.wall_s > 0 else 0.0:.2f},"
            f"{connect_result.wall_s:.3f}"
        )

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
        csv_lines.append(
            f"{remove_result.name},{remove_result.ok + remove_result.errors},{remove_result.ok},{remove_result.errors},"
            f"{(remove_result.errors / (remove_result.ok + remove_result.errors) * 100.0) if (remove_result.ok + remove_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(remove_result.latencies_ms)[0]:.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(remove_result.latencies_ms), 50):.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(remove_result.latencies_ms), 95):.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(remove_result.latencies_ms), 99):.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(remove_result.latencies_ms)[-1]:.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(remove_result.latencies_ms):.2f}' if remove_result.latencies_ms else 'n/a'},"
            f"{((remove_result.ok + remove_result.errors) / remove_result.wall_s) if remove_result.wall_s > 0 else 0.0:.2f},"
            f"{remove_result.wall_s:.3f}"
        )

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
        csv_lines.append(
            f"{compile_result.name},{compile_result.ok + compile_result.errors},{compile_result.ok},{compile_result.errors},"
            f"{(compile_result.errors / (compile_result.ok + compile_result.errors) * 100.0) if (compile_result.ok + compile_result.errors) else 0.0:.2f}%,"
            f"{f'{sorted(compile_result.latencies_ms)[0]:.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(compile_result.latencies_ms), 50):.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(compile_result.latencies_ms), 95):.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{f'{percentile(sorted(compile_result.latencies_ms), 99):.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{f'{sorted(compile_result.latencies_ms)[-1]:.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{f'{statistics.fmean(compile_result.latencies_ms):.2f}' if compile_result.latencies_ms else 'n/a'},"
            f"{((compile_result.ok + compile_result.errors) / compile_result.wall_s) if compile_result.wall_s > 0 else 0.0:.2f},"
            f"{compile_result.wall_s:.3f}"
        )

        all_errors = (
            query_result.errors
            + mutate_result.errors
            + add_result.errors
            + connect_result.errors
            + remove_result.errors
            + compile_result.errors
        )
        if args.output:
            out_path = Path(args.output)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text("\n".join(csv_lines) + "\n", encoding="utf-8")
            print(f"saved_csv={out_path}")

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
