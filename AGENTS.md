# LOOMLE Agent Rules

## Purpose

This file is execution policy for Codex.  
Human-oriented explanation lives in `./Loomle/README.md`.

## Trigger
<!--干脆删掉-->
- If user invokes `loomle` or asks to enable/setup Loomle, run the setup flow when needed.

## Hard Constraints

- Keep all LOOMLE content under `./Loomle`.
- Do not create extra top-level folders outside `./Loomle`.
- Do not overwrite user's root `AGENTS.md`.

## Setup Flow (Authoritative)
<!--这部分也可以删掉了，安装和升级脚本都有了，这个流程没有必要了。-->
1. Run `./Loomle/scripts/install_loomle.sh` from UE project root.
2. Treat script output as contract checks:
   - root `AGENTS.md` guidance line is created/updated and verified
   - `.uproject` wiring for `AdditionalPluginDirectories` and `LoomleMcpBridge`
   - prebuilt plugin resolution (local binary -> local build fallback)
   - editor target build (only when prebuilt is unavailable/incompatible)
   - editor launch
   - bridge transport (`loomle-mcp.sock` / named pipe)
   - MCP baseline tools
   - Python bridge `unreal.BlueprintGraphBridge`

## Install And Upgrade

- Install entrypoint stays `./Loomle/scripts/install_loomle.sh`.
- Upgrade entrypoint stays `./Loomle/scripts/upgrade_loomle.sh`.
- At the start of each Codex thread/session for this project, Codex should check Loomle source update status in `./Loomle` (git upstream compare).
- If a newer upstream revision exists, Codex should proactively remind the user that they can ask in natural language (for example: "upgrade Loomle" / "update Loomle") and Codex will run the upgrade flow.
- Do not auto-upgrade without explicit user confirmation.

## User Command Handling Policy
- Command handling + user-facing translation:
  - Supported user commands: `loomle`, `context`, `live`, `execute`.
  - For these commands, always call the corresponding MCP tool first, then translate result into concise, user-friendly text.
  - Do not dump raw JSON by default; only show raw payload when user explicitly asks for it.
  - Output policy by command:
    - `loomle`:
      1. Summary line (core bridge status)
      2. Capability list with short descriptions
      3. Current status interpretation
    - `context` / `live` / `execute`:
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

## Context Capability Playbook

- Use `context` as the default read path for active editor state and current selection.
- Keep `items` as lightweight identifiers. Use `resolvedValues` for detail.
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
