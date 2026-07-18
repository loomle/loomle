# Loomle Tools

Developer-facing entrypoints and benchmark scripts.

## 0.6 Legacy Tools

- `dev_verify.py`: frozen 0.6 local verification flow
- `perf_bridge_latency.py`: latency benchmark for a selected MCP tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark
- `benchmarks/create_temp_blueprint_asset.py`: helper for benchmark fixtures
- `benchmarks/run_graph_query_benchmark.py`: wrapper for stable graph-query
  benchmark invocations

## Legacy Dev Flow

Historical command:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject
```

`dev_verify.py` still builds the removed Rust Client and exercises the 0.6 tool
surface. It is retained as migration reference and is not a valid 0.7
verification command. The old flow synced the `engine/LoomleBridge` plugin
source into the dev project, built the project's Editor target, started Unreal
Editor, waited for the Bridge Runtime, and ran the smoke check through the
checkout-built Client.

This is a local development loop. It does not run `RunUAT BuildPlugin -Rocket`
or package the plugin. Release/package verification lives in the GitHub
workflows under `.github/workflows/`.

Useful variants:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject --run-regression
python3 tools/dev_verify.py --project-root /path/to/MyProject --run-latency
python3 tools/dev_verify.py --project-root /path/to/MyProject --skip-editor-build
python3 tools/dev_verify.py --project-root /path/to/MyProject --no-engine-changes
python3 tools/dev_verify.py --project-root /path/to/MyProject --install-only
```

## Dev Project Root Config

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)

If `--project-root` is omitted, dev and E2E scripts read `project_root` from
the local config file.

## 0.7 Validation

- SAL SDK: `npm run test --workspace @loomle/sal`
- Loomle interfaces: `npm run test --workspace @loomle/interfaces`
- TypeScript Client: `cd client && npm test`
- UE Bridge: UE 5.7 `RunUAT BuildPlugin`

The 0.6 UE smoke/regression suites require the former tool surface and will be
redesigned around SAL instead of being redirected to the current four-tool
SAL/Context Client. Retained non-SAL utilities will be migrated explicitly;
the current Client surface is not their compatibility adapter.

## Legacy Tests

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
