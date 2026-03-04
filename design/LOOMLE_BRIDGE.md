# Loomle Bridge

## 1. Scope

- Runtime control plane inside UE editor plugin.
- Provides Loomle-compatible tool routing and execution.
- Hosts adapters for graph types.

Naming:

- External protocol/module: `Loomle Graph`
- Internal per-graph execution unit: `Adapter`
- Blueprint adapter canonical name: `LoomleBlueprintAdapter`
- Python canonical adapter symbol (target): `unreal.LoomleBlueprintAdapter`

## 2. Runtime Layers

1. Transport Layer
- Named pipe (Windows), unix socket (macOS/Linux), NDJSON framing.

2. Router Layer
- JSON-RPC parser, request validation, tool dispatch, error normalization.

3. Tool Layer
- Existing tools: `loomle`, `context`, `execute`.
- New graph tools: `graph`, `graph.list`, `graph.query`, `graph.mutate`.

4. State Layer
- Active editor window tracker.
- Snapshot cache.
- Revision and cursor state.

5. Context Detector Layer
- Single in-module detector for active editor context/selection.
- Uses editor-type branches (blueprint/material/pcg) behind one unified tool path.

6. Adapter Layer
- `LoomleBlueprintAdapter` (full mutate/query path).
- Material and PCG adapters (query/addable/mutate/watch path).

7. Execution Layer
- Operation transaction executor.
- Dry-run pipeline.
- Idempotency gate.

8. Event Layer
- Typed graph event bus.
- Cursor-based watch stream.
- Runtime jsonl sink.

## 3. Tool Routing

- `graph.list` -> Adapter graph enumeration path (`blueprint/material/pcg`).
- `graph.query` -> Adapter semantic snapshot query path (`blueprint/material/pcg`).
- `graph.mutate` -> Adapter mutate path (`blueprint/material/pcg`, with graph-type-specific op coverage).
- `graph` -> static + runtime capability + schema descriptor.
- `context` compatibility mode:
  - no graph args -> unified context detector behavior.
  - with `assetPath/graphName/includeNodes/...` -> internally delegate to `graph.query`.

## 4. Standard Request Flow

1. Validate request and normalize graph arguments.
2. Resolve active adapter by `graphType`.
3. Execute adapter operation (`query` or `mutate`).
4. Convert adapter output to canonical response schema.
5. Append diagnostics/telemetry.
6. Emit typed events when mutate changes graph.

## 5. Standard Mutate Flow

1. Validate `idempotencyKey` and `expectedRevision`.
2. Build operation plan from `ops[]`.
3. Resolve `clientRef` dependencies.
4. Execute ops sequentially.
5. Stop on error (default) or continue when enabled.
6. Persist new revision token.
7. Emit watch events.
8. Return operation result list.

Graph mutate event coverage (blueprint):
- `graph.node_added` (`addNode.event|cast|callFunction|branch|variableGet|variableSet`)
- `graph.node_connected` (`connectPins`)
- `graph.links_changed` (`disconnectPins|breakPinLinks`)
- `graph.pin_default_changed` (`setPinDefault`)
- `graph.node_removed` (`removeNode`)
- `graph.node_moved` (`moveNode`)
- `graph.compiled` (`compile`)
- `graph.actor_spawned` (`spawnActor`)
- `graph.component_added` (`addComponent`)

## 6. Error and Diagnostics

- All tool errors use canonical code set from `LOOMLE_GRAPH.md`.
- Adapter-native errors are mapped to canonical codes.
- Response always contains machine-readable `diagnostics` array.

## 7. Compatibility Policy

- Keep `context/execute` available.
- New feature work targets `graph.*` first.
- `execute` remains fallback only.
- No dual-source schemas: canonical graph schema is defined only in `LOOMLE_GRAPH.md`.
- Keep Python adapter access available under canonical symbol; no legacy Python alias is kept.

## 8. Rollout Sequence

1. Ship `graph`.
2. Ship `graph.query` for blueprint.
3. Route compatible `context` graph args to `graph.query`.
4. Ship `graph.mutate` backed by `LoomleBlueprintAdapter`.
5. Freeze new feature additions to legacy-only paths.
