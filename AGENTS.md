# LOOMLE Agent Rules

## Purpose

This file defines execution policy for Codex.
Human-oriented explanation lives in `./Loomle/README.md`.

## Hard Constraints

- Keep all LOOMLE content under `./Loomle`.
- Do not create extra top-level folders outside `./Loomle`.
- Do not overwrite user's root `AGENTS.md`.

## Install And Upgrade

- Install entrypoint: `./Loomle/scripts/install_loomle.sh`.
- Upgrade entrypoint: `./Loomle/scripts/upgrade_loomle.sh`.
- At the start of each Codex thread/session, check Loomle source update status in `./Loomle` (git upstream compare).
- If a newer upstream revision exists, remind the user they can ask in natural language (for example: "upgrade Loomle" / "update Loomle"), and Codex will run the upgrade flow.
- Do not auto-upgrade without explicit user confirmation.

## User Command Handling Policy
- Supported user commands: `loomle`, `context`, `live`, `execute`.
- For these commands, always call the corresponding MCP tool first, then return concise natural-language output.
- Do not dump raw JSON by default; show raw payload only when explicitly requested.
- Output policy by command:
  - `loomle`:
    1. Summary line (core bridge status)
    2. Capability list with short descriptions
    3. Current status interpretation
  - `context` / `live` / `execute`:
    - Return only the current command result in natural language.
    - Do not repeat overall bridge status or capability list.
- `execute` UX guardrail:
  - If user only types `execute` (without Python code), do not surface raw parameter error first.
  - Explain that `execute` is an internal Python execution command usually operated by Codex.
  - Ask for intent in natural language, then generate and run Python via `execute`.
- Only when abnormal/error:
  - Append transport/protocol/version diagnostics.
  - Append actionable recovery guidance (for example restart Unreal Editor).
- Live policy (intent-based, not unconditional):
  - If the user request depends on current editor state or recent user actions in UE (selection, actor/map/PIE/property changes, "I just did X in editor"), call `live` before reasoning.
  - If the user request is pure knowledge/chat and does not depend on live editor state, skip `live`.
  - Use incremental live cursor:
    - first pull in a session: `cursor=0`
    - subsequent pulls: use previous `nextCursor`
  - Keep pull bounded (`limit` <= 100) and prefer small windows unless user asks for full history.
  - If live pull fails or cursor is stale, recover from `Loomle/runtime/live_events.jsonl` as fallback context.

## Context Capability Playbook

- Use `context` as the first read path for active editor state and current selection.
- Keep `items` lightweight. Use `resolvedValues` for detail.
- Call order:
  - `context {}` first
  - then `context {"resolveIds":[...]}` for detail
  - optionally add `resolveFields` to reduce payload

### `context` Request Templates

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"context","arguments":{"resolveIds":["<id1>","<id2>"]}}}
```

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"context","arguments":{"resolveIds":["<id1>"],"resolveFields":["id","name","class","nodeTitle","pins","callFunction","dynamicCast"]}}}
```

### `context` Response Shape (Selection)

- `selection.items[]` (lightweight): `id,name,class,path,nodePosX,nodePosY,graphName,graphPath`
- `selection.resolvedValues[id]` (detailed, when resolved):
  - common: `nodeGuid,nodeTitle,nodeTitleFull,tooltip,isNodeEnabled,pins`
  - `K2Node_CallFunction`: `callFunction{name,path,ownerClass,isPure,isConst,isStatic,isEvent}`
  - `K2Node_DynamicCast`: `dynamicCast{sourcePin,resultPin,targetClass}`

### When To Use `execute` (Python)

- Use `execute` when any of these apply:
  - cross-graph or cross-asset traversal
  - fields not present in `context`
  - custom aggregation/output schema
  - any write action (create/connect/update graph elements)
- Do not rely on `context` for C++ function body source text. Use reflection data only.

### Minimal Python Read Example (`execute`)

```python
import unreal, json

node = unreal.load_object(None, "/Game/Codex/BP_BouncyPad.BP_BouncyPad:EventGraph.K2Node_CallFunction_0")
print(json.dumps({
    "class": node.get_class().get_path_name(),
    "name": node.get_name(),
    "full_name": node.get_full_name()
}, ensure_ascii=False))
```

### Hard Rules For Agents

- Do not expand context capability by modifying Bridge source only to fetch richer data; use `execute` Python for deeper reads.
