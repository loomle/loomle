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

1. Run `./Loomle/scripts/install_loomle.sh` from UE project root.
2. Treat script output as contract checks:
   - `.uproject` wiring for `AdditionalPluginDirectories` and `LoomleMcpBridge`
   - prebuilt plugin resolution (local binary -> local build fallback)
   - editor target build (only when prebuilt is unavailable/incompatible)
   - editor launch
   - bridge transport (`loomle-mcp.sock` / named pipe)
   - MCP baseline tools
   - Python bridge `unreal.BlueprintGraphBridge`

## Runtime Policy

- Preferred automation path: `execute` + `unreal.BlueprintGraphBridge`.
- Apply Anti-Entropy Principle:
  - remove superseded paths quickly
  - keep one source of truth per capability
- Command handling + user-facing translation:
  - Supported user commands: `loomle`, `context`, `selection`, `live`, `execute`.
  - For these commands, always call the corresponding MCP tool first, then translate result into concise, user-friendly text.
  - Do not dump raw JSON by default; only show raw payload when user explicitly asks for it.
  - Output policy by command:
    - `loomle`:
      1. Summary line (core bridge status)
      2. Capability list with short descriptions
      3. Current status interpretation
    - `context` / `selection` / `live` / `execute`:
      - only return the current command result in natural language
      - do not repeat overall bridge status or capability list
  - `execute` UX guardrail:
    - If user only types `execute` (without Python code), do not surface raw parameter error first.
    - Explain that `execute` is an internal Python execution command usually operated by Codex.
    - Ask user to describe intent in natural language (what they want to do in editor), then Codex should generate and run Python via `execute`.
  - Only when abnormal/error:
    - append transport/protocol/version diagnostics
    - append actionable recovery guidance (for example restart Unreal Editor)
- Live policy (intent-based, not unconditional):
  - If the user request depends on current editor state or recent user actions in UE (selection, actor/map/PIE/property changes, "I just did X in editor"), call `live` before reasoning.
  - If the user request is pure knowledge/chat and does not depend on live editor state, skip `live`.
  - Use incremental live cursor:
    - first pull in a session: `cursor=0`
    - subsequent pulls: use previous `nextCursor`
  - Keep pull bounded (`limit` <= 100) and prefer small windows unless user asks for full history.
  - If live pull fails or cursor is stale, recover from `Loomle/runtime/live_events.jsonl` as fallback context.
