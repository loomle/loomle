# Loomle Tools

Unified local tooling for testing and diagnostics.

## Naming

- `test_*`: functional/integration checks
- `perf_*`: performance benchmarks
- `dev_*`: developer helpers
- `release_*`: release helpers

## Current Tools

- `test_bridge_smoke.py`: fast MCP end-to-end availability check
- `test_bridge_regression.py`: deeper functional regression for graph/tool behavior
- `test_bridge_windows.ps1`: Windows wrapper to run rust + smoke + regression in one command
- `perf_bridge_latency.py`: latency benchmark for a selected tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark

## Test Structure

- UE-independent MCP server tests stay in `mcp_server`:
  - `cd mcp_server && cargo test`
- UE-dependent smoke/regression launch MCP server from plugin path only:
  - `<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)`
- UE-dependent bridge checks and benchmarks stay in this `tools/` directory.

### Dev Project Root Config (optional)

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)
- If `--project-root` is omitted, `test_bridge_smoke.py` and `test_bridge_regression.py` read `project_root` from this local file.

### Windows quick run

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test_bridge_windows.ps1 -ProjectRoot "D:\UnrealProjects\Loombed"
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
  --project-root "/Users/xartest/Documents/UnrealProjects/Loombed" \
  --tool loomle --total 200 --concurrency 1 --warmup 20 \
  --output runtime/benchmarks/bridge-latency.csv
```

Example graph RW benchmark:

```bash
python3 tools/perf_graph_rw_temp_asset.py \
  --project-root "/Users/xartest/Documents/UnrealProjects/Loombed" \
  --output runtime/benchmarks/graph-rw.csv
```
