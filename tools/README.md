# Loomle Tools

Developer utilities and performance tooling.

## Naming

- `perf_*`: performance benchmarks
- `dev_*`: developer helpers
- release helpers now live under `packaging/`

## Current Tools

- `dev_verify.py`: canonical local development verification flow
- `perf_bridge_latency.py`: latency benchmark for a selected tool call
- `perf_graph_rw_temp_asset.py`: temporary-asset graph read/write benchmark
- `runner_init_host_project.sh`: developer host-project bootstrap helper
- `runner_init_host_project.ps1`: Windows host-project bootstrap helper

## Test Structure

- UE-independent client/runtime contract tests live in `client`:
  - `cd client && cargo test`
- UE-dependent validation scripts live in `tests/`:
  - `tests/e2e/test_bridge_smoke.py`
  - `tests/e2e/test_bridge_regression.py`
  - `tests/e2e/test_bridge_windows.ps1`
  - `tests/integration/test_loomle_latency.py`
- The canonical validation entrypoint is now the installed project-local client:
  - `<ProjectRoot>/Loomle/loomle(.exe)`

### Canonical Local Dev Flow

Use `tools/dev_verify.py` instead of manually rebuilding and copying binaries. It installs the current checkout into the dev host project, restarts Unreal Editor, then validates through the same `Loomle/loomle` entrypoint that users get after install.

Recommended command:

```bash
python3 tools/dev_verify.py --project-root "/path/to/MyProject"
```

Useful variants:

```bash
python3 tools/dev_verify.py --project-root "/path/to/MyProject" --run-latency
python3 tools/dev_verify.py --project-root "/path/to/MyProject" --install-only
```

Only fall back to the lower-level scripts when you are intentionally debugging one phase in isolation.

### Dev Project Root Config (optional)

- Template: `tools/dev.project-root.example.json`
- Local file: `tools/dev.project-root.local.json` (gitignored)
- Recommended dev host: `/path/to/MyProject`
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
  --project-root "/path/to/MyProject" \
  --tool loomle --total 200 --concurrency 1 --warmup 20 \
  --output runtime/benchmarks/bridge-latency.csv
```

Example `loomle` fast-return validation:

```bash
python3 tests/integration/test_loomle_latency.py \
  --project-root "/path/to/MyProject" \
  --samples 30
```

Example graph RW benchmark:

```bash
python3 tools/perf_graph_rw_temp_asset.py \
  --project-root "/path/to/MyProject" \
  --output runtime/benchmarks/graph-rw.csv
```
