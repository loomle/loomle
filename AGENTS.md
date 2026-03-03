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
- Before launching Unreal Editor, terminate any existing Unreal Editor processes for this project to avoid multiple concurrent editor instances.

## User Command Handling Policy
- Supported user commands: `loomle`, `context`, `live`, `execute`.
- Agent-internal tools that should be used when needed: `graph`, `graph.query`, `graph.mutate`, `graph.watch`.
- For these commands, always call the corresponding bridge tool first, then return concise natural-language output.
- Do not dump raw JSON by default; show raw payload only when explicitly requested.
- JSON-RPC transport note:
  - Bridge may actively push `notifications/live` on the same connection while handling `tools/call`.
  - Clients must match responses by `id` (demux), and must not treat the first received frame as the call response.
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

## Bridge Interface Playbook

### Tool Routing

- `loomle`: bridge health and capability summary.
- `context`: active editor context + current selection snapshot.
- `live`: incremental editor event stream pull.
- `execute`: Python fallback for custom reads/writes.
- `graph`: graph capability/schema descriptor.
- `graph.query`: read graph nodes/edges.
- `graph.mutate`: apply graph operations.
- `graph.watch`: graph-oriented event pull.

### `loomle` Request Template

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"loomle","arguments":{}}}
```

### `live` Request Template

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"live","arguments":{"cursor":0,"limit":20}}}
```

### `execute` Request Template

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"execute","arguments":{"mode":"exec","code":"import unreal\nprint('ok')"}}}
```

## Context Capability Playbook

- Use `context` as the first read path for active editor state and current selection.
- `context` is implemented as a single unified detector path (no provider registry).
- Keep `items` lightweight (id/path/class/position).
- Deep graph details should come from `graph.query` (preferred) or `execute`.
- Call order:
  - `context {}` first

### `context` Request Templates

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

### `context` Response Shape (Selection)

- `selection.items[]` (lightweight): `id,name,class,path,nodePosX,nodePosY,graphName,graphPath`

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

## Graph Capability Playbook

### `graph` (Descriptor)

- Use first when agent needs to confirm supported graph ops/fields.
- Request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph","arguments":{"graphType":"blueprint"}}}
```

### `graph.query` (Read)

- Required: `arguments.assetPath`.
- `assetPath` must be long package path (example: `/Game/Codex/BP_BouncyPad`), not object path (`/Game/Codex/BP_BouncyPad.BP_BouncyPad`).
- Optional: `graphName` (default `EventGraph`), `filter.nodeClasses`, `limit` (or implementation-supported max node options).
- Base request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.query","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","graphType":"blueprint","limit":200}}}
```

- Filtered request:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"graph.query","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","filter":{"nodeClasses":["/Script/BlueprintGraph.K2Node_CallFunction"]}}}}
```

- Key response fields: `nodes[]`, `edges[]`, `graphName`, `truncated`, `nextCursor`, `meta`.

### `graph.mutate` (Write)

- Required: `arguments.assetPath`, `arguments.ops[]`.
- Optional: `dryRun` for non-committing validation path.
- Typical ops in current bridge version include `compile`, `connectPins`, `setPinDefault`, `addNode.*`, and actor/component helpers.
- Dry-run compile template:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.mutate","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","dryRun":true,"ops":[{"op":"compile"}]}}}
```

- Key response fields: `applied`, `opResults[]`, `previousRevision`, `newRevision`, `diagnostics[]`.

### `graph.watch` (Event Pull)

- Used for graph-focused event polling.
- Request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.watch","arguments":{"cursor":0,"limit":20}}}
```

- Key response fields: `events[]`, `cursor`, `nextCursor`, `count`, `dropped`, `running`.

### Hard Rules For Agents

- Do not expand context capability by modifying Bridge source only to fetch richer data; use `execute` Python for deeper reads.
