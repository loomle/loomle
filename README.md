# Loomle

`Loomle Bridge` is a single project composed of:

- Unreal Editor plugin (`LoomleBridge`)
- External MCP server (`mcp_server`)

This repository keeps only two operational tracks: local testing and online release.

## Documentation

- Full project docs and interface specs: `docs/README.md`
- Developer tooling and local checks: `tools/README.md`

## Local Testing

### 1) MCP server tests (UE-independent)

```bash
cd mcp_server
cargo test
```

### 2) Bridge protocol smoke test (requires UE Editor)

macOS/Linux:

```bash
python3 tools/test_bridge_smoke.py \
  --project-root "/Users/xartest/Documents/UnrealProjects/Loombed"
```

Windows:

```powershell
python tools/test_bridge_smoke.py --project-root "D:\\UnrealProjects\\Loombed"
```

### 3) Bridge functional regression (deeper coverage)

```bash
python3 tools/test_bridge_regression.py \
  --project-root "/Users/xartest/Documents/UnrealProjects/Loombed"
```

### 4) Optional performance diagnostics

```bash
python3 tools/perf_bridge_latency.py \
  --socket "/Users/xartest/Documents/UnrealProjects/Loomle/Intermediate/loomle.sock" \
  --tool loomle --total 200 --concurrency 1 --warmup 20
```

## Online Release

- Trigger: push tag `vX.Y.Z`
- Workflow: `.github/workflows/release-loomle-bridge-mac.yml`
- Outputs:
  - `loomle-bridge-darwin.zip`
  - `loomle-bridge-manifest.json`
  - stable alias release: `bridge-latest`

## Runtime Tools

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.query`
- `graph.actions`
- `graph.mutate`
