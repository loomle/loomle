#!/usr/bin/env python3
import argparse
import json
import os
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
    def __init__(self, project_root: str, manifest_path: str, timeout_s: float) -> None:
        project = Path(project_root).resolve()
        manifest = Path(manifest_path).resolve()
        if not project.exists():
            fail(f"Project root not found: {project}")
        if not any(project.glob("*.uproject")):
            fail(f"No .uproject found under: {project}")
        if not manifest.exists():
            fail(f"mcp_server manifest not found: {manifest}")

        env = os.environ.copy()
        env["LOOMLE_PROJECT_ROOT"] = str(project)
        self.proc = subprocess.Popen(
            ["cargo", "run", "-q", "--manifest-path", str(manifest)],
            cwd=str(project),
            env=env,
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
                        out = self._pending.get(req_id)
                    if out is not None:
                        out.put(frame)
                # notifications are intentionally ignored for benchmark
        except Exception as exc:
            self._reader_error = str(exc)
            self._stop.set()
            with self._pending_lock:
                waiters = list(self._pending.values())
            for waiter in waiters:
                waiter.put({"_reader_error": self._reader_error})

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
            raise RuntimeError(f"timeout waiting for response to id={req_id}") from exc
        finally:
            with self._pending_lock:
                self._pending.pop(req_id, None)

        if "_reader_error" in response:
            raise RuntimeError(str(response["_reader_error"]))

        return response


@dataclass
class BenchResult:
    latencies_ms: list[float]
    errors: int
    ok: int
    wall_s: float


@dataclass
class BenchCaseResult:
    tool: str
    result: BenchResult


def run_benchmark(
    client: JsonRpcClient,
    method: str,
    params: dict[str, Any],
    total: int,
    concurrency: int,
    warmup: int,
) -> BenchResult:
    for _ in range(warmup):
        _ = client.request(method, params)

    latencies_ms: list[float] = []
    errors = 0
    ok = 0
    next_index = 0
    lock = threading.Lock()

    def worker() -> None:
        nonlocal next_index, errors, ok
        while True:
            with lock:
                if next_index >= total:
                    return
                next_index += 1

            start = time.perf_counter()
            try:
                resp = client.request(method, params)
                elapsed_ms = (time.perf_counter() - start) * 1000.0
                if "error" in resp:
                    with lock:
                        errors += 1
                else:
                    with lock:
                        ok += 1
                        latencies_ms.append(elapsed_ms)
            except Exception:
                with lock:
                    errors += 1

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(max(1, concurrency))]
    start_wall = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    wall_s = time.perf_counter() - start_wall

    return BenchResult(latencies_ms=latencies_ms, errors=errors, ok=ok, wall_s=wall_s)


def choose_connection(args: argparse.Namespace) -> BridgeConnection:
    return McpStdioConnection(
        project_root=args.project_root,
        manifest_path=args.mcp_manifest,
        timeout_s=args.timeout,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark Loomle bridge latency (p50/p95/p99)")
    parser.add_argument(
        "--project-root",
        required=True,
        help="UE project root for stdio mode, e.g. /.../UnrealProjects/Loombed",
    )
    parser.add_argument(
        "--mcp-manifest",
        default=str(Path(__file__).resolve().parents[1] / "mcp_server" / "Cargo.toml"),
        help="Path to mcp_server Cargo.toml for stdio mode",
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-request timeout in seconds")
    parser.add_argument("--total", type=int, default=200, help="Measured request count")
    parser.add_argument("--concurrency", type=int, default=8, help="Concurrent workers")
    parser.add_argument("--warmup", type=int, default=20, help="Warmup request count")
    parser.add_argument(
        "--tool",
        default="loomle",
        help="Tool name for tools/call (default: loomle)",
    )
    parser.add_argument(
        "--arguments",
        default="{}",
        help="JSON object for tools/call arguments (default: {})",
    )
    parser.add_argument(
        "--tools",
        default="",
        help="Comma-separated tool list for batch mode, e.g. loomle,context,graph.query",
    )
    parser.add_argument(
        "--output",
        default="",
        help="Optional CSV output file path",
    )
    args = parser.parse_args()


    try:
        tool_args = json.loads(args.arguments)
    except json.JSONDecodeError as exc:
        fail(f"--arguments is not valid JSON: {exc}")
    if not isinstance(tool_args, dict):
        fail("--arguments must decode to a JSON object")

    conn = choose_connection(args)
    client = JsonRpcClient(conn, timeout_s=args.timeout)
    try:
        init = client.request("initialize", {})
        if "error" in init:
            fail(f"initialize failed: {init['error']}")
        _ = client.request("tools/list", {})

        batch_tools = [t.strip() for t in args.tools.split(",") if t.strip()]
        if not batch_tools:
            batch_tools = [args.tool]

        case_results: list[BenchCaseResult] = []
        for tool_name in batch_tools:
            per_args = dict(tool_args)
            method = "tools/call"
            params = {"name": tool_name, "arguments": per_args}
            result = run_benchmark(
                client=client,
                method=method,
                params=params,
                total=max(1, args.total),
                concurrency=max(1, args.concurrency),
                warmup=max(0, args.warmup),
            )
            case_results.append(BenchCaseResult(tool=tool_name, result=result))
    finally:
        client.close()

    any_ok = False
    lines: list[str] = []
    header = "tool,total,ok,errors,error_rate_pct,min_ms,p50_ms,p95_ms,p99_ms,max_ms,mean_ms,throughput_rps,wall_s"
    lines.append(header)
    print(header)
    for case in case_results:
        result = case.result
        sorted_lat = sorted(result.latencies_ms)
        sent = result.ok + result.errors
        rps = (sent / result.wall_s) if result.wall_s > 0 else 0.0
        err_rate = (result.errors / sent * 100.0) if sent > 0 else 0.0
        if result.ok > 0:
            any_ok = True

        if sorted_lat:
            min_ms = f"{sorted_lat[0]:.2f}"
            p50_ms = f"{percentile(sorted_lat, 50):.2f}"
            p95_ms = f"{percentile(sorted_lat, 95):.2f}"
            p99_ms = f"{percentile(sorted_lat, 99):.2f}"
            max_ms = f"{sorted_lat[-1]:.2f}"
            mean_ms = f"{statistics.fmean(sorted_lat):.2f}"
        else:
            min_ms = p50_ms = p95_ms = p99_ms = max_ms = mean_ms = "n/a"

        line = (
            f"{case.tool},{sent},{result.ok},{result.errors},{err_rate:.2f}%,"
            f"{min_ms},{p50_ms},{p95_ms},{p99_ms},{max_ms},{mean_ms},{rps:.2f},{result.wall_s:.3f}"
        )
        lines.append(line)
        print(line)

    if args.output:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"saved_csv={out_path}")

    return 0 if any_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
