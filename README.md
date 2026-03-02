# LOOMLE

LOOMLE is a project-local bridge that lets Codex operate UE5 Editor through MCP + Python.

## One-Line Prompt (for users)

Use this exact prompt in Codex from your UE project root:

```text
Install Loomle (https://github.com/loomle/loomle) by following ./Loomle/README.md.
```

## Install Contract (for Codex)

Use one idempotent entrypoint:

```bash
./Loomle/scripts/install_loomle.sh
```

What this script does:

1. Verifies plugin path `./Loomle/Plugins/LoomleMcpBridge` exists.
2. Ensures `<Project>.uproject` wiring:
   - `AdditionalPluginDirectories` includes `./Loomle/Plugins`
   - `LoomleMcpBridge` is enabled for `Editor`
3. Uses prebuilt plugin in this order:
   - compatible local binary under `./Loomle/Plugins/LoomleMcpBridge/Binaries/Mac`
   - fallback: build `<Project>Editor` (`Mac Development`)
4. Launches Unreal Editor using `open -na ...UnrealEditor.app --args <Project>.uproject`.
5. Verifies bridge endpoint and MCP baseline (`initialize`, `tools/list`, `loomle`, `execute` with `BlueprintGraphBridge` assertion).

Optional flags:

- `--skip-build`
- `--skip-launch`
- `--skip-verify`
- `--force-build`

## CI Build (GitHub Actions)

Workflow file: `.github/workflows/build-plugin.yml`

It builds `LoomleMcpBridge` for both `Win64` and `Mac`, then uploads packaged plugin artifacts.

Requirements:

1. A `self-hosted` Windows runner with UE 5.7 installed.
2. A `self-hosted` macOS runner with UE 5.7 installed.
3. Configure repo variables (recommended):
   - `UE_5_7_ROOT_WIN` (for example `C:\Program Files\Epic Games\UE_5.7`)
   - `UE_5_7_ROOT_MAC` (for example `/Users/Shared/Epic Games/UE_5.7`)
4. Or set runner environment variables with the same names.

Trigger:

- Manual: `workflow_dispatch`
- Auto on `main` changes under `Plugins/LoomleMcpBridge/**`
- On tag `v*`: builds both platforms and publishes GitHub Release assets:
  - `LoomleMcpBridge-Win64.zip`
  - `LoomleMcpBridge-Mac.zip`

Note: CI is optional. If runners are not configured, installation still works via local source build.

## Verification Checklist

Installation is successful only if all checks pass:

1. `LoomleMcpBridge` compiles successfully.
2. Bridge endpoint exists:
   - macOS/Linux: `<Project>/Intermediate/loomle-mcp.sock`
   - Windows: `\\.\\pipe\\loomle-mcp`
3. `tools/list` includes:
   - `loomle`
   - `context`
   - `selection`
   - `live`
   - `execute`
4. UE Python exposes:
   - `unreal.BlueprintGraphBridge`

## File Roles

- `README.md`: user-facing install contract and success criteria.
- `AGENTS.md`: Codex execution rules and conventions.

## Boundaries

- Keep LOOMLE-contained assets under `./Loomle`.
- Do not modify or overwrite the project's root `AGENTS.md`.
