#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the LOOMLE graph.query latency benchmark with stable argument encoding.")
    parser.add_argument("--project-root", required=True, help="UE project root containing a .uproject")
    parser.add_argument("--asset-path", required=True, help="Blueprint asset path to benchmark")
    parser.add_argument("--graph-name", default="EventGraph", help="Graph name to query")
    parser.add_argument("--graph-type", default="blueprint", help="Graph type to query")
    parser.add_argument("--limit", type=int, default=200, help="graph.query limit")
    parser.add_argument("--total", type=int, default=384, help="Total requests to send")
    parser.add_argument("--concurrency", type=int, default=48, help="Concurrent workers")
    parser.add_argument("--warmup", type=int, default=20, help="Warmup requests")
    parser.add_argument("--loomle-bin", default="", help="Optional path to the loomle client binary")
    parser.add_argument(
        "--output-file",
        default="",
        help="Optional file path to store combined benchmark stdout/stderr",
    )
    args = parser.parse_args()

    command = [
        sys.executable,
        str(REPO_ROOT / "tools" / "perf_bridge_latency.py"),
        "--project-root",
        args.project_root,
        "--tool",
        "graph.query",
        "--arguments",
        json.dumps(
            {
                "assetPath": args.asset_path,
                "graphName": args.graph_name,
                "graphType": args.graph_type,
                "limit": args.limit,
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ),
        "--total",
        str(args.total),
        "--concurrency",
        str(args.concurrency),
        "--warmup",
        str(args.warmup),
    ]
    if args.loomle_bin:
        command.extend(["--loomle-bin", args.loomle_bin])
    result = subprocess.run(command, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)

    if args.output_file:
        output_path = Path(args.output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(result.stdout + result.stderr, encoding="utf-8")

    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
