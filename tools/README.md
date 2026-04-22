# Loomle Tools

Developer-facing entrypoints and benchmark scripts.

## Current Tools

- `dev_verify.py`: canonical local development verification flow
- `perf_bridge_latency.py`: latency benchmark for a selected MCP tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark
- `benchmarks/create_temp_blueprint_asset.py`: helper for benchmark fixtures
- `benchmarks/run_graph_query_benchmark.py`: wrapper for stable graph-query
  benchmark invocations

## Canonical Dev Flow

Use:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject
```

`dev_verify.py` builds the checkout `loomle` binary, syncs
`engine/LoomleBridge` into the dev project, starts Unreal Editor, waits for the
bridge runtime, then runs smoke and optional regression checks through the
checkout-built global-client path.

Useful variants:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject --run-latency
python3 tools/dev_verify.py --project-root /path/to/MyProject --install-only
```

## Dev Project Root Config

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)

If `--project-root` is omitted, dev and E2E scripts read `project_root` from
the local config file.

## Tests

- UE-independent Rust tests: `cd client && cargo test`
- UE smoke: `tests/e2e/test_bridge_smoke.py`
- UE regression: `tests/e2e/test_bridge_regression.py`
- Windows UE smoke: `tests/e2e/test_bridge_windows.ps1`
- Graph-suite workers: `tests/tools/`

## Runtime Output

Runtime artifacts and benchmark outputs belong in `runtime/`.
`runtime/` is output-only; do not store tooling source there.

## Benchmark Standard

Benchmarks should run through stdio MCP and accept an explicit `--loomle-bin`
when they need to test a checkout-built client.
