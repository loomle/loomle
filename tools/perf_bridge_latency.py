#!/usr/bin/env python3
import argparse
import json
import queue
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


def ensure_project_root(project_root: Path) -> Path:
    project_root = project_root.resolve()
    if not project_root.exists():
        fail(f"Project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"No .uproject found under: {project_root}")
    return project_root


def default_loomle_binary(project_root: Path) -> Path:
    binary_name = "loomle.exe" if sys.platform.startswith("win") else "loomle"
    candidate = project_root.resolve() / "Loomle" / binary_name
    if candidate.is_file():
        return candidate
    fail(
        "project-local loomle binary not found: "
        f"{candidate}. install the current checkout into the test project first, "
        "or pass --loomle-bin to override."
    )
    raise RuntimeError("unreachable")


class LoomleMcpClient:
    def __init__(self, project_root: Path, loomle_binary: Path, timeout_s: float) -> None:
        self.project_root = ensure_project_root(project_root)
        self.loomle_binary = loomle_binary.resolve()
        if not self.loomle_binary.exists():
            fail(f"loomle binary not found: {self.loomle_binary}")
        if not self.loomle_binary.is_file():
            fail(f"loomle binary path is not a file: {self.loomle_binary}")

        self.proc = subprocess.Popen(
            [
                str(self.loomle_binary),
                "--project-root",
                str(self.project_root),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.timeout_s = timeout_s
        self._next_id = 1
        self._id_lock = threading.Lock()
        self._pending: dict[int, "queue.Queue[dict[str, Any]]"] = {}
        self._pending_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._stop = threading.Event()
        self._reader_error: Optional[str] = None
        self._stderr_tail: list[str] = []
        self._stderr_lock = threading.Lock()
        self._stdout_reader = threading.Thread(target=self._stdout_reader_loop, daemon=True)
        self._stderr_reader = threading.Thread(target=self._stderr_reader_loop, daemon=True)
        self._stdout_reader.start()
        self._stderr_reader.start()
        init = self._request(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "loomle-perf-bridge-latency", "version": "0.1.0"},
            },
        )
        if "error" in init:
            raise RuntimeError(f"initialize failed: {init['error']}")
        self._notify("notifications/initialized", {})

    def close(self) -> None:
        self._stop.set()
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
        self._stdout_reader.join(timeout=1.0)
        self._stderr_reader.join(timeout=1.0)

    def _stderr_snapshot(self) -> str:
        with self._stderr_lock:
            return "\n".join(self._stderr_tail[-200:])

    def _stdout_reader_loop(self) -> None:
        try:
            if self.proc.stdout is None:
                return
            for line in self.proc.stdout:
                if self._stop.is_set():
                    return
                line = line.strip()
                if not line:
                    continue
                try:
                    frame = json.loads(line)
                except json.JSONDecodeError:
                    continue
                frame_id = frame.get("id")
                if isinstance(frame_id, int):
                    with self._pending_lock:
                        waiter = self._pending.get(frame_id)
                    if waiter is not None:
                        waiter.put(frame)
        except Exception as exc:
            self._reader_error = str(exc)
            self._stop.set()
            with self._pending_lock:
                waiters = list(self._pending.values())
            for waiter in waiters:
                waiter.put({"ok": False, "error": self._reader_error, "id": None})

    def _stderr_reader_loop(self) -> None:
        try:
            if self.proc.stderr is None:
                return
            for line in self.proc.stderr:
                if self._stop.is_set():
                    return
                text = line.rstrip()
                if not text:
                    continue
                with self._stderr_lock:
                    self._stderr_tail.append(text)
        except Exception:
            return

    def _request(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self.proc.stdin is None:
            raise RuntimeError("loomle stdio stdin is not available")
        if self._reader_error:
            raise RuntimeError(self._reader_error)

        with self._id_lock:
            request_id = self._next_id
            self._next_id += 1

        mailbox: "queue.Queue[dict[str, Any]]" = queue.Queue(maxsize=1)
        with self._pending_lock:
            self._pending[request_id] = mailbox

        payload = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
        wire = json.dumps(payload, separators=(",", ":")) + "\n"

        try:
            with self._send_lock:
                self.proc.stdin.write(wire)
                self.proc.stdin.flush()
            response = mailbox.get(timeout=self.timeout_s)
        except queue.Empty as exc:
            stderr_tail = self._stderr_snapshot()
            if stderr_tail:
                raise RuntimeError(
                    f"timeout waiting for MCP response id={request_id}; recent stderr:\n{stderr_tail}"
                ) from exc
            raise RuntimeError(f"timeout waiting for MCP response id={request_id}") from exc
        finally:
            with self._pending_lock:
                self._pending.pop(request_id, None)

        return response

    def _notify(self, method: str, params: dict[str, Any]) -> None:
        if self.proc.stdin is None:
            raise RuntimeError("loomle stdio stdin is not available")
        wire = json.dumps({"jsonrpc": "2.0", "method": method, "params": params}, separators=(",", ":")) + "\n"
        with self._send_lock:
            self.proc.stdin.write(wire)
            self.proc.stdin.flush()

    def call_tool(self, tool_name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        response = self._request("tools/call", {"name": tool_name, "arguments": arguments})
        if "error" in response:
            raise RuntimeError(str(response.get("error", "unknown MCP error")))
        result = response.get("result")
        if not isinstance(result, dict):
            raise RuntimeError(f"invalid MCP result payload: {response}")
        structured = result.get("structuredContent")
        if isinstance(structured, dict):
            return structured
        content = result.get("content")
        if not isinstance(content, list) or not content:
            raise RuntimeError(f"invalid tools/call content payload: {response}")
        first = content[0]
        if not isinstance(first, dict) or not isinstance(first.get("text"), str):
            raise RuntimeError(f"invalid tools/call text payload: {response}")
        return json.loads(first["text"])


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
    client: LoomleMcpClient,
    tool_name: str,
    arguments: dict[str, Any],
    total: int,
    concurrency: int,
    warmup: int,
) -> BenchResult:
    for _ in range(warmup):
        _ = client.call_tool(tool_name, arguments)

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
                resp = client.call_tool(tool_name, arguments)
                elapsed_ms = (time.perf_counter() - start) * 1000.0
                if bool(resp.get("isError")):
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
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    wall_s = time.perf_counter() - start_wall

    return BenchResult(latencies_ms=latencies_ms, errors=errors, ok=ok, wall_s=wall_s)


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark LOOMLE bridge latency (p50/p95/p99)")
    parser.add_argument(
        "--project-root",
        required=True,
        help="UE project root, e.g. /Users/xartest/dev/LoomleDevHost",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Override path to the loomle client binary. Defaults to <ProjectRoot>/Loomle/loomle(.exe).",
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-request timeout in seconds")
    parser.add_argument("--total", type=int, default=200, help="Measured request count")
    parser.add_argument("--concurrency", type=int, default=8, help="Concurrent workers")
    parser.add_argument("--warmup", type=int, default=20, help="Warmup request count")
    parser.add_argument(
        "--tool",
        default="loomle",
        help="Tool name for MCP tools/call (default: loomle)",
    )
    parser.add_argument(
        "--arguments",
        default="{}",
        help="JSON object for tool arguments (default: {})",
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

    project_root = Path(args.project_root)
    loomle_binary = Path(args.loomle_bin).resolve() if args.loomle_bin else default_loomle_binary(project_root)
    client = LoomleMcpClient(
        project_root=project_root,
        loomle_binary=loomle_binary,
        timeout_s=args.timeout,
    )
    try:
        batch_tools = [tool.strip() for tool in args.tools.split(",") if tool.strip()]
        if not batch_tools:
            batch_tools = [args.tool]

        case_results: list[BenchCaseResult] = []
        for tool_name in batch_tools:
            result = run_benchmark(
                client=client,
                tool_name=tool_name,
                arguments=dict(tool_args),
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
