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
- `perf_bridge_latency.py`: latency benchmark for a selected tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark

## Test Structure

- UE-independent MCP server tests stay in `mcp_server`:
  - `cd mcp_server && cargo test`
- UE-dependent bridge checks and benchmarks stay in this `tools/` directory.

## Runtime Output

- Runtime artifacts and benchmark outputs belong in `runtime/`.
- `runtime/` is output-only; do not store tooling source there.
