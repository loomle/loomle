# LOOMLE

LOOMLE is a project-local integration layer that lets Codex work with UE5 Editor through MCP and Python bridge APIs.

## What This File Is For

This `README.md` is for humans:

- what to place
- how to trigger setup
- what "success" looks like

Execution rules for Codex are in `./Loomle/AGENTS.md`.

## Minimal Installation Model

1. Put this folder in your UE project root as:
   - `./Loomle`
2. Open Codex in the UE project root.
3. Ask Codex:
   - `Enable LOOMLE in this project and follow Loomle/AGENTS.md`

No global CLI install is required.

## Expected Project Wiring

Codex should ensure your `.uproject` has:

- `AdditionalPluginDirectories: ["./Loomle/Plugins"]`
- plugin enabled: `LoomleMcpBridge` (Editor)

## Success Criteria

A successful setup means all of these pass:

1. `LoomleMcpBridge` compiles.
2. Bridge endpoint is reachable:
   - macOS/Linux: `<Project>/Intermediate/loomle-mcp.sock`
   - Windows: `\\.\\pipe\\loomle-mcp`
3. `tools/list` includes:
   - `get_context`
   - `get_selection_transform`
   - `editor_stream`
   - `execute_python`
4. Python sees:
   - `unreal.BlueprintGraphBridge`

## Scope Rules

- Keep all LOOMLE assets inside `./Loomle`.
- Do not overwrite user's root `AGENTS.md`.
