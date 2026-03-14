# Loomle Tools

Developer utilities and performance tooling.

## Naming

- `perf_*`: performance benchmarks
- `dev_*`: developer helpers
- release helpers now live under `packaging/`

## Current Tools

- `perf_bridge_latency.py`: latency benchmark for a selected tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark
- `runner_init_host_project.sh`: developer host-project bootstrap helper
- `runner_init_host_project.ps1`: Windows host-project bootstrap helper

## Test Structure

- UE-independent MCP server tests live in `mcp/server`:
  - `cd mcp/server && cargo test`
- UE-dependent validation scripts live in `tests/`:
  - `tests/e2e/test_bridge_smoke.py`
  - `tests/e2e/test_bridge_regression.py`
  - `tests/e2e/test_bridge_windows.ps1`
  - `tests/integration/test_loomle_latency.py`
- E2E scripts still default to the plugin MCP server path:
  - `<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)`

### Refresh Rules After Code Changes

- If you only change `mcp/server/`, rebuild the Rust binary and replace the dev-host plugin copy of `loomle_mcp_server`. Unreal Editor usually does not need a restart.
- If you change Unreal plugin C++ under `engine/LoomleBridge/Source/LoomleBridge/`, rebuild the Editor target and restart Unreal Editor before validating behavior.
- If you change both sides, rebuild and replace the `mcp/server` binary first, then rebuild the Unreal plugin, then restart Unreal Editor.
- If you only change Python tests or docs, Unreal Editor does not need a restart.
- If compilation succeeded but runtime behavior still looks old, first suspect a stale Unreal Editor session before suspecting the code path.

### Dev Project Root Config (optional)

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)
- Recommended dev host: `/Users/xartest/dev/LoomleDevHost`
- If `--project-root` is omitted, the E2E test scripts read `project_root` from this local file.

### Windows quick run

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\e2e\test_bridge_windows.ps1 -ProjectRoot "D:\LoomleDevHost"
```

## Runtime Output

- Runtime artifacts and benchmark outputs belong in `runtime/`.
- `runtime/` is output-only; do not store tooling source there.

## Benchmark Standard

- Use `--project-root` (stdio MCP mode only). Legacy socket/pipe benchmark mode is removed.
- Export CSV with `--output` for reproducible comparison across runs.

Example latency benchmark:

```bash
python3 tools/perf_bridge_latency.py \
  --project-root "/Users/xartest/dev/LoomleDevHost" \
  --tool loomle --total 200 --concurrency 1 --warmup 20 \
  --output runtime/benchmarks/bridge-latency.csv
```

Example `loomle` fast-return validation:

```bash
python3 tests/integration/test_loomle_latency.py \
  --project-root "/Users/xartest/dev/LoomleDevHost" \
  --samples 30
```

Example graph RW benchmark:

```bash
python3 tools/perf_graph_rw_temp_asset.py \
  --project-root "/Users/xartest/dev/LoomleDevHost" \
  --output runtime/benchmarks/graph-rw.csv
```
