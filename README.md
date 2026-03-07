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

## Release Runtime (Source-Independent, Minimal)

This section is for release package usage only. No source checkout or `cargo` is required.

1. Prepare `ProjectRoot` (directory containing `*.uproject`).
2. Ensure plugin package is installed under `<ProjectRoot>/Plugins/LoomleBridge`.
3. Start Unreal Editor with that project so bridge listener is available.
4. Launch MCP server binary from plugin path:

macOS:
```bash
"<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/darwin/loomle_mcp_server" \
  --project-root "<ProjectRoot>"
```

Linux:
```bash
"<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/linux/loomle_mcp_server" \
  --project-root "<ProjectRoot>"
```

Windows (PowerShell):
```powershell
& "<ProjectRoot>\Plugins\LoomleBridge\Tools\mcp\windows\loomle_mcp_server.exe" `
  --project-root "<ProjectRoot>"
```

Transport facts:
- MCP server uses stdio JSON-RPC.
- Unreal bridge endpoint is derived from `--project-root`.
- macOS/Linux RPC socket: `<ProjectRoot>/Intermediate/loomle.sock`.
- Windows RPC endpoint: `\\.\pipe\loomle`.

Minimal health probe sequence:
1. send `initialize`
2. send `tools/call` with `name="loomle"`
3. send `tools/call` with `name="context"`

## Local Testing

Optional dev convenience (source checkout only):

1. Copy `tools/dev.project-root.example.json` to `tools/dev.project-root.local.json`.
2. Set `"project_root"` to your UE project root.
3. Then `tools/test_bridge_smoke.py` and `tools/test_bridge_regression.py` can run without `--project-root`.

Before UE-dependent tests, build and sync the MCP server binary into the plugin runtime path:

```bash
cd mcp_server
cargo build --release
mkdir -p "<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/darwin"
cp target/release/loomle_mcp_server "<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/darwin/loomle_mcp_server"
chmod +x "<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/darwin/loomle_mcp_server"
```

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
  - package-internal MCP server: `LoomleBridge/Tools/mcp/darwin/loomle_mcp_server`
- stable alias release: `bridge-latest`
- manifest package metadata includes:
  - `server_binary_relpath`
  - `server_sha256`
- runtime startup requirement:
  - pass `--project-root <ProjectRoot>` when launching `loomle_mcp_server`
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
