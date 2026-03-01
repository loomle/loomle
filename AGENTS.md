# LOOMLE Agent Rules

## Purpose

This file is execution policy for Codex.  
Human-oriented explanation lives in `./Loomle/README.md`.

## Trigger

- If user asks to enable `LOOMLE`, run this flow.

## Hard Constraints

- Keep all LOOMLE content under `./Loomle`.
- Do not create extra top-level folders outside `./Loomle`.
- Do not overwrite user's root `AGENTS.md`.
- Do not reintroduce deprecated MCP tools.

## Setup Flow (Authoritative)

1. Verify UE project root (`*.uproject` exists in cwd).
2. Verify plugin exists at:
   - `./Loomle/Plugins/LoomleMcpBridge`
3. Ensure `.uproject` wiring:
   - `AdditionalPluginDirectories` contains `./Loomle/Plugins`
   - `LoomleMcpBridge` enabled for `Editor`
4. Build editor target to compile plugin changes.
5. Ensure editor is running (or ask user to open/restart).
6. Validate bridge transport:
   - macOS/Linux: `<Project>/Intermediate/loomle-mcp.sock`
   - Windows: `\\.\\pipe\\loomle-mcp`
7. Validate `tools/list` contains exactly required baseline tools:
   - `get_context`
   - `get_selection_transform`
   - `editor_stream`
   - `execute_python`
8. Validate Python bridge:
   - run `execute_python` with `import unreal`
   - assert `hasattr(unreal, "BlueprintGraphBridge")`

## Runtime Policy

- Preferred automation path: `execute_python` + `unreal.BlueprintGraphBridge`.
- Apply Anti-Entropy Principle:
  - remove superseded paths quickly
  - keep one source of truth per capability
