# Loomle

`Loomle Bridge` is a single project composed of:

- Unreal Editor plugin (`LoomleBridge`)
- External MCP server (`mcp_server`)

This repository keeps only two operational tracks: local testing and online release.

## Documentation

- Full project docs and interface specs: `docs/README.md`
- Developer tooling and local checks: `tools/README.md`

## Performance Requirement (Project-Wide)

For all projects using Loomle Bridge, disable Unreal Editor background CPU throttling:

`[/Script/UnrealEd.EditorPerformanceSettings] bThrottleCPUWhenNotForeground=False`

Set this in the UE project file:

- `Config/DefaultEditorSettings.ini`

Without this setting, bridge tail latency can degrade significantly when Unreal Editor is in background.

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

Windows one-shot:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\test_bridge_windows.ps1 -ProjectRoot "D:\UnrealProjects\Loombed"
```

### 4) Optional performance diagnostics

```bash
python3 tools/perf_bridge_latency.py \
  --project-root "/Users/xartest/Documents/UnrealProjects/Loombed" \
  --tool loomle --total 200 --concurrency 1 --warmup 20
```

## Online Release

- Verify workflow (no publish): `.github/workflows/release-verify-mac.yml`
- Release trigger: push tag `vX.Y.Z`
- Release workflow: `.github/workflows/release-loomle-bridge-mac.yml`
- Release gate: `cargo test` + `test_bridge_smoke.py` + `test_bridge_regression.py`
- Outputs:
  - `loomle-bridge-darwin.zip`
  - `loomle-bridge-manifest.json`
  - stable alias release: `bridge-latest`
- Stable download links:
  - `https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-darwin.zip`
  - `https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-manifest.json`

## Runtime Tools

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.query`
- `graph.actions`
- `graph.mutate`
