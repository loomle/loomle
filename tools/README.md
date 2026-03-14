# Loomle Tools

Unified local tooling for testing and diagnostics.

## Naming

- `test_*`: functional/integration checks
- `perf_*`: performance benchmarks
- `dev_*`: developer helpers
- release helpers now live under `packaging/`

## Current Tools

- `test_bridge_smoke.py`: fast MCP end-to-end availability check, including `graph.resolve` tool presence
- `test_bridge_regression.py`: deeper functional regression for graph/tool behavior, including PCG `context`/`graph.resolve` addressing
- `test_loomle_latency.py`: validates `loomle` fast-return behavior at idle and under slow execute load
- `test_bridge_windows.ps1`: Windows wrapper to run rust + smoke + regression in one command
- `perf_bridge_latency.py`: latency benchmark for a selected tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark

## Test Structure

- UE-independent MCP server tests stay in `mcp/server`:
  - `cd mcp/server && cargo test`
- UE-dependent smoke/regression default to the plugin MCP server path:
  - `<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)`
- Both scripts also accept `--mcp-server-bin` so runner/dev flows can point at a freshly built repo binary when needed.
- UE-dependent bridge checks and benchmarks stay in this `tools/` directory.

### Refresh Rules After Code Changes

- If you only change `mcp/server/`, rebuild the Rust binary and replace the dev-host plugin copy of `loomle_mcp_server`. Unreal Editor usually does not need a restart.
- If you change Unreal plugin C++ under `Source/LoomleBridge/`, rebuild the Editor target and restart Unreal Editor before validating behavior.
- If you change both sides, rebuild and replace the `mcp/server` binary first, then rebuild the Unreal plugin, then restart Unreal Editor.
- If you only change Python tests or docs, Unreal Editor does not need a restart.
- If compilation succeeded but runtime behavior still looks old, first suspect a stale Unreal Editor session before suspecting the code path.

### Dev Project Root Config (optional)

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)
- Recommended dev host: `/Users/xartest/dev/LoomleDevHost`
- If `--project-root` is omitted, `test_bridge_smoke.py` and `test_bridge_regression.py` read `project_root` from this local file.

### Windows quick run

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test_bridge_windows.ps1 -ProjectRoot "D:\LoomleDevHost"
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
python3 tools/test_loomle_latency.py \
  --project-root "/Users/xartest/dev/LoomleDevHost" \
  --samples 30
```

Example graph RW benchmark:

```bash
python3 tools/perf_graph_rw_temp_asset.py \
  --project-root "/Users/xartest/dev/LoomleDevHost" \
  --output runtime/benchmarks/graph-rw.csv
```
