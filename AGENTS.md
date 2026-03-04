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

### Post-Install Performance Guardrail (Required)

- After install/upgrade and editor launch, always disable UE background CPU throttling for this project:
  - setting: `[/Script/UnrealEd.EditorPerformanceSettings] bThrottleCPUWhenNotForeground=False`
  - source of truth file: `./Config/DefaultEditorSettings.ini`
- Reason: when this is `True`, simple bridge calls can show high tail latency in background (for example `loomle` p95 around `~300ms+`).
- Codex should proactively help the user apply/fix this setting right after install/upgrade.
- Quick verify after applying:
  1. keep UE in background (Codex in foreground)
  2. run `python3 ./Loomle/scripts/benchmark_bridge.py --socket "/Users/xartest/Documents/UnrealProjects/Loomle/Intermediate/loomle.sock" --tool loomle --total 200 --concurrency 1 --warmup 20`
  3. expect low-tail latency (typically single-digit milliseconds, not `~300ms+`)

## Build/Load Reliability (Important)

- `BuildPlugin -Package=...` is for packaging validation, not the runtime source of truth for this project.
- To apply plugin code changes for this project, build against the project plugin path:
  - `UnrealBuildTool ... -Project="/Users/xartest/Documents/UnrealProjects/Loomle/Loomle.uproject" -plugin="/Users/xartest/Documents/UnrealProjects/Loomle/Loomle/Plugins/LoomleBridge/LoomleBridge.uplugin" ...`
- After plugin rebuild, always do a full editor restart (no hot-reload assumption):
  1. terminate Unreal Editor process
  2. relaunch project
  3. wait for bridge socket readiness: `/Users/xartest/Documents/UnrealProjects/Loomle/Intermediate/loomle.sock`

## User Command Handling Policy
- Supported bridge commands: `loomle`, `context`, `execute`, `graph`, `graph.list`, `graph.query`, `graph.addable`, `graph.mutate`.
- User-facing commands are usually `loomle`, `context`; `execute` is typically agent-operated.
- For these commands, always call the corresponding bridge tool first, then return concise natural-language output.
- Do not dump raw JSON by default; show raw payload only when explicitly requested.
- JSON-RPC transport note:
  - Clients must match responses by `id` (demux), and must not treat the first received frame as the call response.
- Output policy by command:
  - `loomle`:
    1. Summary line (core bridge status)
    2. Capability list with short descriptions
    3. Current status interpretation
  - `context` / `execute`:
    - Return only the current command result in natural language.
    - Do not repeat overall bridge status or capability list.
- `execute` UX guardrail:
  - If user only types `execute` (without Python code), do not surface raw parameter error first.
  - Explain that `execute` is an internal Python execution command usually operated by Codex.
  - Ask for intent in natural language, then generate and run Python via `execute`.
- Only when abnormal/error:
  - Append transport/protocol/version diagnostics.
  - Append actionable recovery guidance (for example restart Unreal Editor).

## Bridge Interface Playbook

### Tool Routing

- `loomle`: bridge health and capability summary.
- `context`: active editor context + current selection snapshot.
- `execute`: Python fallback for custom reads/writes.
- `graph`: graph capability/schema descriptor.
- `graph.list`: list readable graphs in an asset.
- `graph.query`: read semantic graph snapshot (nodes/edges in `semanticSnapshot`).
- `graph.addable`: list addable right-click actions in current graph/pin context.
- `graph.mutate`: apply graph operations.

### `loomle` Request Template

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"loomle","arguments":{}}}
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

### `graph.list` (Graph Enumeration)

- Required: `arguments.assetPath`.
- Request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.list","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","graphType":"blueprint"}}}
```

- Key response fields: `graphs[]`, `graphType`, `assetPath`, `diagnostics[]`.

### `graph.query` (Read)

- Required: `arguments.assetPath`, `arguments.graphName`.
- `assetPath` must be long package path (example: `/Game/Codex/BP_BouncyPad`), not object path (`/Game/Codex/BP_BouncyPad.BP_BouncyPad`).
- Optional: `filter.nodeClasses`, `limit` (or implementation-supported max node options).
- Base request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.query","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","graphName":"EventGraph","graphType":"blueprint","limit":200}}}
```

- Filtered request:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"graph.query","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","graphName":"EventGraph","filter":{"nodeClasses":["/Script/BlueprintGraph.K2Node_CallFunction"]}}}}
```

- Key response fields: `semanticSnapshot.signature`, `semanticSnapshot.nodes[]`, `semanticSnapshot.edges[]`, `graphName`, `meta`.

### `graph.addable` (Action Enumeration)

- Required: `arguments.assetPath`, `arguments.graphName`.
- Optional: `arguments.graphType`, `arguments.context.fromPin`, `arguments.query`, `arguments.limit`.
- Request:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.addable","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","graphName":"EventGraph","graphType":"blueprint","limit":20}}}
```

- Key response fields: `items[]`, where each item can include `actionToken`, `title`, `categoryPath`, `tooltip`, `keywords`, `compatibility`, `spawn`.
- `actionToken` is short-lived and should be passed as `args.actionToken` in `graph.mutate` `addNode.byAction`.

### `graph.mutate` (Write)

- Required: `arguments.assetPath`, `arguments.ops[]`.
- Optional: `dryRun` for non-committing validation path.
- Typical ops in current bridge version:
  - `addNode.byClass`
  - `addNode.byAction`
  - `connectPins`
  - `disconnectPins`
  - `breakPinLinks`
  - `setPinDefault`
  - `removeNode`
  - `moveNode`
  - `compile`
  - `runScript`
- Dry-run compile template:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph.mutate","arguments":{"assetPath":"/Game/Codex/BP_BouncyPad","dryRun":true,"ops":[{"op":"compile"}]}}}
```

- Key response fields: `applied`, `opResults[]`, `previousRevision`, `newRevision`, `diagnostics[]`.

### Hard Rules For Agents

- Do not expand context capability by modifying Bridge source only to fetch richer data; use `execute` Python for deeper reads.
