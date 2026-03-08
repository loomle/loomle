#!/usr/bin/env python3
import argparse
import statistics
import threading
import time
from pathlib import Path

from test_bridge_regression import wait_for_bridge_ready
from test_bridge_smoke import McpStdioClient, call_tool, fail, resolve_default_server_binary, resolve_project_root


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    if p <= 0:
        return values[0]
    if p >= 100:
        return values[-1]

    rank = (len(values) - 1) * (p / 100.0)
    lo = int(rank)
    hi = min(lo + 1, len(values) - 1)
    frac = rank - lo
    return values[lo] * (1.0 - frac) + values[hi] * frac


def timed_loomle_call(client: McpStdioClient, req_id: int) -> tuple[float, dict]:
    started = time.perf_counter()
    payload = call_tool(client, req_id, "loomle", {})
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    return elapsed_ms, payload


def measure_loomle(
    client: McpStdioClient,
    total: int,
    req_id_base: int,
) -> list[float]:
    latencies_ms: list[float] = []
    for offset in range(total):
        elapsed_ms, payload = timed_loomle_call(client, req_id_base + offset)
        status = payload.get("status")
        rpc_status = payload.get("runtime", {}).get("rpcHealth", {}).get("status")
        if status not in {"ok", "degraded", "error"}:
            fail(f"loomle returned unexpected status payload: {payload}")
        if rpc_status not in {"ok", "degraded", "error"}:
            fail(f"loomle returned unexpected rpc status payload: {payload}")
        latencies_ms.append(elapsed_ms)
    return latencies_ms


def slow_execute_worker(
    project_root: Path,
    server_binary: Path,
    timeout_s: float,
    sleep_s: float,
    req_id_base: int,
    repeat: int,
    errors: list[str],
) -> None:
    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=timeout_s)
    try:
        init = client.request(req_id_base, "initialize", {})
        if "error" in init:
            errors.append(f"initialize failed: {init['error']}")
            return
        _ = client.request(req_id_base + 1, "tools/list", {})
        for idx in range(repeat):
            try:
                call_tool(
                    client,
                    req_id_base + 10 + idx,
                    "execute",
                    {
                        "mode": "exec",
                        "code": (
                            "import time\n"
                            f"time.sleep({sleep_s})\n"
                            "print('loomle latency blocker finished')\n"
                        ),
                    },
                )
            except Exception as exc:
                errors.append(str(exc))
                return
    finally:
        client.close()


def print_stats(label: str, latencies_ms: list[float]) -> None:
    ordered = sorted(latencies_ms)
    p50 = percentile(ordered, 50)
    p95 = percentile(ordered, 95)
    p99 = percentile(ordered, 99)
    max_v = ordered[-1]
    mean_v = statistics.mean(ordered)
    print(
        f"[INFO] {label}: count={len(ordered)} "
        f"mean={mean_v:.1f}ms p50={p50:.1f}ms p95={p95:.1f}ms p99={p99:.1f}ms max={max_v:.1f}ms"
    )


def assert_budget(label: str, latencies_ms: list[float], max_budget_ms: float, p95_budget_ms: float) -> None:
    ordered = sorted(latencies_ms)
    p95 = percentile(ordered, 95)
    max_v = ordered[-1]
    if p95 > p95_budget_ms:
        fail(f"{label} exceeded p95 budget: p95={p95:.1f}ms budget={p95_budget_ms:.1f}ms")
    if max_v > max_budget_ms:
        fail(f"{label} exceeded max budget: max={max_v:.1f}ms budget={max_budget_ms:.1f}ms")
    print(f"[PASS] {label}: p95={p95:.1f}ms max={max_v:.1f}ms within budget")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate LOOMLE loomle fast-return behavior")
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root, e.g. /Users/xartest/dev/LoomleDevHost. If omitted, read tools/dev.project-root.local.json",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json)",
    )
    parser.add_argument(
        "--mcp-server-bin",
        default="",
        help="Override path to MCP server binary. Defaults to <project>/Plugins/LoomleBridge/Tools/mcp/<platform>/...",
    )
    parser.add_argument("--timeout", type=float, default=2.0, help="Per-request timeout seconds for validation clients")
    parser.add_argument("--warmup", type=int, default=5, help="Warmup loomle calls before measuring")
    parser.add_argument("--samples", type=int, default=30, help="Measured loomle calls per phase")
    parser.add_argument("--baseline-max-ms", type=float, default=300.0, help="Max latency budget for idle loomle")
    parser.add_argument("--baseline-p95-ms", type=float, default=150.0, help="P95 latency budget for idle loomle")
    parser.add_argument("--loaded-max-ms", type=float, default=350.0, help="Max latency budget under slow execute load")
    parser.add_argument("--loaded-p95-ms", type=float, default=320.0, help="P95 latency budget under slow execute load")
    parser.add_argument("--blocker-clients", type=int, default=2, help="Concurrent background clients issuing slow execute")
    parser.add_argument("--blocker-repeat", type=int, default=2, help="Slow execute calls per blocker client")
    parser.add_argument("--blocker-sleep-s", type=float, default=1.5, help="Sleep duration inside execute workload")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.mcp_server_bin).resolve() if args.mcp_server_bin else resolve_default_server_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    try:
        init = client.request(1, "initialize", {})
        if "error" in init:
            fail(f"initialize failed: {init['error']}")
        _ = client.request(2, "tools/list", {})
        wait_for_bridge_ready(client, timeout_s=120.0, interval_s=2.0)

        _ = measure_loomle(client, total=max(0, args.warmup), req_id_base=100)
        baseline = measure_loomle(client, total=max(1, args.samples), req_id_base=1000)
        print_stats("baseline", baseline)
        assert_budget("baseline", baseline, args.baseline_max_ms, args.baseline_p95_ms)

        blocker_errors: list[str] = []
        blocker_threads: list[threading.Thread] = []
        for idx in range(max(0, args.blocker_clients)):
            thread = threading.Thread(
                target=slow_execute_worker,
                kwargs={
                    "project_root": project_root,
                    "server_binary": server_binary,
                    "timeout_s": max(args.timeout, args.blocker_sleep_s + 2.0),
                    "sleep_s": args.blocker_sleep_s,
                    "req_id_base": 10_000 + idx * 100,
                    "repeat": max(1, args.blocker_repeat),
                    "errors": blocker_errors,
                },
                daemon=True,
            )
            blocker_threads.append(thread)
            thread.start()

        time.sleep(0.2)
        loaded = measure_loomle(client, total=max(1, args.samples), req_id_base=20_000)

        for thread in blocker_threads:
            thread.join()

        if blocker_errors:
            fail(f"background execute blockers failed: {blocker_errors[0]}")

        print_stats("loaded", loaded)
        assert_budget("loaded", loaded, args.loaded_max_ms, args.loaded_p95_ms)
    finally:
        client.close()

    print("[PASS] loomle latency validation completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
