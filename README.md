# LOOMLE

LOOMLE is a project-local bridge that lets Codex operate UE5 Editor through MCP + Python.

## One-Line Prompt (for users)

Use this exact prompt in Codex from your UE project root:

```text
Install Loomle in this UE5 project from https://github.com/loomle/loomle and follow the repository README automatically.
```

## Install Contract (for Codex)

When asked to install LOOMLE, Codex should:

1. Ensure repo exists at `./Loomle` (clone if missing).
2. Ensure `<Project>.uproject` contains:
   - `AdditionalPluginDirectories: ["./Loomle/Plugins"]`
   - plugin enabled: `LoomleMcpBridge`
3. Generate project files and build editor target so `LoomleMcpBridge` is compiled.
4. Launch UE Editor and verify bridge availability.
5. Report final status with pass/fail checks.

No global installer is required.

## Verification Checklist

Installation is successful only if all checks pass:

1. `LoomleMcpBridge` compiles successfully.
2. Bridge endpoint exists:
   - macOS/Linux: `<Project>/Intermediate/loomle-mcp.sock`
   - Windows: `\\.\\pipe\\loomle-mcp`
3. `tools/list` includes:
   - `get_context`
   - `get_selection_transform`
   - `editor_stream`
   - `execute_python`
4. UE Python exposes:
   - `unreal.BlueprintGraphBridge`

## File Roles

- `README.md`: user-facing install contract and success criteria.
- `AGENTS.md`: Codex execution rules and conventions.

## Boundaries

- Keep LOOMLE-contained assets under `./Loomle`.
- Do not modify or overwrite the project's root `AGENTS.md`.
