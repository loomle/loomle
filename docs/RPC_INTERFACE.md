# LOOMLE RPC Interface (C++ External Boundary)

## 1. Scope

Defines the RPC methods exposed by Unreal-side `RPC Listener`.

- Protocol: JSON-RPC 2.0
- Framing: NDJSON
- Version: `rpcVersion = 1.0`

## 2. Common Envelope

## 2.1 Shared Types

### GraphRef

`GraphRef` is a self-contained subgraph locator. Two variants:

**Inline** — identifies a subgraph owned by a node within an already-loaded asset (e.g. Blueprint `K2Node_Composite`, inline PCG subgraph node):

```json
{
  "kind": "inline",
  "nodeGuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
  "assetPath": "/Game/BP_Foo"
}
```

**Asset** — identifies a graph that is a standalone UE asset or a named root graph within a multi-graph asset (Blueprint):

```json
// Single-graph asset (Material, external PCG)
{ "kind": "asset", "assetPath": "/Game/FX/MyMaterialFunction" }

// Multi-graph asset (Blueprint — graphName required)
{ "kind": "asset", "assetPath": "/Game/BP_Foo", "graphName": "EventGraph" }
```

Key contract rules:

- `GraphRef` is always emitted by the server (in `graph.list` and `graph.query` responses) with all fields filled in. Clients treat it as an opaque token and pass it back verbatim.
- When a client supplies a `GraphRef` as input, it replaces the `assetPath` + `graphName` pair entirely — no separate `assetPath` field is needed.
- `assetPath` embedded inside `GraphRef.inline` is the asset that contains the composite node, not the subgraph itself (subgraphs are not standalone assets).



Request:

```json
{
  "jsonrpc": "2.0",
  "id": "req-1",
  "method": "rpc.invoke",
  "params": {}
}
```

Success:

```json
{
  "jsonrpc": "2.0",
  "id": "req-1",
  "result": {}
}
```

Error:

```json
{
  "jsonrpc": "2.0",
  "id": "req-1",
  "error": {
    "code": 1000,
    "message": "INVALID_ARGUMENT",
    "data": {
      "retryable": false,
      "detail": "field graphName is required"
    }
  }
}
```

## 3. Infra Methods

## 3.1 `rpc.health`

Request params:

```json
{}
```

Result:

```json
{
  "status": "ok|degraded|error",
  "service": "loomle-rpc-listener",
  "rpcVersion": "1.0",
  "timestamp": "2026-03-05T12:00:00Z",
  "isPIE": false,
  "editorBusyReason": ""
}
```

## 3.2 `rpc.capabilities`

Request params:

```json
{}
```

Result:

```json
{
  "rpcVersion": "1.0",
  "methods": ["rpc.health", "rpc.capabilities", "rpc.invoke"],
  "tools": ["context", "execute", "graph.list", "graph.resolve", "graph.query", "graph.ops", "graph.ops.resolve", "graph.mutate", "pcg.inspectRuntime", "diag.tail"],
  "graphTypes": ["blueprint", "material", "pcg"],
  "features": {
    "revision": true,
    "idempotency": true,
    "dryRun": true,
    "subgraphRef": true
  }
}
```

## 4. Core Method

## 4.1 `rpc.invoke`

Request params:

```json
{
  "tool": "context|execute|graph.list|graph.resolve|graph.query|graph.ops|graph.ops.resolve|graph.mutate|pcg.inspectRuntime|diag.tail",
  "args": {},
  "meta": {
    "requestId": "external-id",
    "traceId": "trace-id",
    "rpcVersion": "1.0",
    "deadlineMs": 10000
  }
}
```

Success result:

```json
{
  "ok": true,
  "payload": {},
  "diagnostics": []
}
```

## 5. Tool Payload Contracts (via `rpc.invoke`)

Naming note:

- Runtime graph domains are `blueprint`, `material`, and `pcg`.
- Historical aliases such as `k2` and `shader` are no longer part of the public contract and should not be emitted by clients or docs.

## 5.1 tool=`context`

`args`:

```json
{
  "resolveIds": ["optional-id"],
  "resolveFields": ["optional-field"]
}
```

`payload`:

```json
{
  "timestamp": "RFC3339",
  "context": {
    "editorType": "blueprint|material|pcg|unknown",
    "assetPath": "string"
  },
  "selection": {
    "count": 0,
    "items": []
  }
}
```

## 5.2 tool=`execute`

`args`:

```json
{
  "language": "ue-script",
  "mode": "exec|eval",
  "code": "string"
}
```

`payload`:

```json
{
  "ok": true,
  "stdout": "string",
  "result": "string-or-json",
  "durationMs": 12
}
```

## 5.3 tool=`graph.list`

`args`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "includeSubgraphs": false,
  "maxDepth": 1
}
```

- `includeSubgraphs` (optional, default `false`): recursively enumerate subgraphs contained within composite/subgraph nodes. Supported across all three graph types:
  - `blueprint`: enumerates `K2Node_Composite` inline subgraphs (emits `kind: "inline"` refs).
  - `pcg`: enumerates PCG subgraph nodes and emits `kind: "asset"` refs to the external `UPCGGraph` assets.
  - `material`: enumerates `MaterialFunctionCall` expressions and emits `kind: "asset"` refs to the referenced `MaterialFunction` assets.
- `maxDepth` (optional, default `1`, max `8`): maximum recursion depth when `includeSubgraphs` is `true`. Depth `1` returns only direct children; `0` is equivalent to `includeSubgraphs: false`.

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphs": [
    {
      "graphName": "string",
      "graphKind": "root|function|macro|subgraph",
      "graphClassPath": "string",
      "graphRef": { "kind": "inline|asset", "..." : "..." },
      "parentGraphRef": null,
      "ownerNodeId": "string",
      "loadStatus": "loaded|loading|not_found"
    }
  ],
  "diagnostics": []
}
```

Field notes:

- `graphRef`: present on all entries. For root graphs the server emits an `asset`-kind ref; for inline subgraphs an `inline`-kind ref. Use this value as input to `graph.query`, `graph.ops.resolve`, or `graph.mutate` for direct addressing.
- `parentGraphRef`: `null` for root-level graphs; set to the parent's `graphRef` for subgraphs when `includeSubgraphs` is `true`.
- `ownerNodeId`: the `nodeId` of the composite/subgraph node that contains this graph. `null` for root graphs.
- `loadStatus`: present on `kind: "asset"` entries only. `"loaded"` means the asset is in memory; `"loading"` means an async load is in progress; `"not_found"` means the asset path could not be resolved.
- For `graphType: "material"`, top-level queries are intentionally non-recursive. Use `graph.list(includeSubgraphs=true)` or follow `childGraphRef` from `MaterialFunctionCall` nodes to inspect referenced `UMaterialFunction` graphs.

## 5.4 tool=`graph.query`

`args`:

Two mutually exclusive addressing modes:

**Mode A — explicit (existing, unchanged):** `assetPath` + `graphName` both required.

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "layoutDetail": "basic|measured",
  "filter": {
    "nodeClasses": ["optional-class"],
    "nodeIds": ["optional-id"],
    "text": "optional-text"
  },
  "limit": 200,
  "cursor": "opaque-pagination-cursor",
  "path": ["CompositeA_Guid", "CompositeB_Guid"]
}
```

`path` (optional, Blueprint only, 1–8 entries): ordered `K2Node_Composite` GUIDs to traverse before querying. The server resolves the subgraph of the **last** GUID in a single round-trip. Use this to reach deeply nested composites without chaining multiple `graph.query` calls. Compatible with both Mode A and Mode B (asset-kind `graphRef`). The response `graphRef` reflects the effective leaf subgraph.

**Mode B — GraphRef:** supply `graphRef` obtained from a prior `graph.list` or `graph.query` response. `assetPath` and `graphName` must be omitted.

Inline ref (Blueprint `K2Node_Composite`):
```json
{
  "graphType": "blueprint",
  "graphRef": {
    "kind": "inline",
    "nodeGuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
    "assetPath": "/Game/BP_Foo"
  },
  "layoutDetail": "basic|measured",
  "filter": { "nodeClasses": ["optional-class"], "nodeIds": ["optional-id"], "text": "optional-text" },
  "limit": 200,
  "cursor": "opaque-pagination-cursor"
}
```

Asset ref (external PCG graph, MaterialFunction, or another Blueprint graph):
```json
{
  "graphType": "pcg|material",
  "graphRef": {
    "kind": "asset",
    "assetPath": "/Game/PCG/MySubgraph"
  },
  "layoutDetail": "basic|measured",
  "limit": 200,
  "cursor": "opaque-pagination-cursor"
}
```

Note: `graphType: "material"` accepts both `UMaterial` and `UMaterialFunction` asset paths.

- If both `graphRef` and `graphName` are present, the server returns `1000 INVALID_ARGUMENT`.
- If neither is present, the server returns `1000 INVALID_ARGUMENT`.

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "revision": "opaque-token",
  "nextCursor": "opaque-pagination-cursor",
  "semanticSnapshot": {
    "signature": "string",
    "nodes": [
      {
        "nodeId": "string",
        "type": "string",
        "layout": {
          "position": { "x": 0, "y": 0 },
          "source": "model|estimated|unsupported",
          "reliable": true,
          "sizeSource": "model|estimated|unsupported",
          "boundsSource": "model|estimated|unsupported"
        },
        "childGraphRef": {
          "kind": "inline",
          "nodeGuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
          "assetPath": "/Game/BP_Foo"
        },
        "childLoadStatus": "loaded|not_found"
      }
    ],
    "edges": []
  },
  "meta": {
    "totalNodes": 0,
    "returnedNodes": 0,
    "totalEdges": 0,
    "returnedEdges": 0,
    "truncated": false,
    "layoutCapabilities": {
      "canReadPosition": true,
      "canReadSize": false,
      "canReadBounds": false,
      "canMoveNode": true,
      "canBatchMove": true,
      "supportsMeasuredGeometry": false,
      "positionSource": "model",
      "sizeSource": "partial|unsupported"
    },
    "layoutDetailRequested": "basic|measured",
    "layoutDetailApplied": "basic|measured"
  },
  "diagnostics": []
}
```

Pagination notes:

- `nextCursor` is empty when the current page is complete.
- When `meta.truncated` is `true`, `nextCursor` contains an opaque continuation token.
- To continue reading, call `graph.query` again with the same graph address, filters, and `limit`, and pass the returned `cursor`.

Node field notes:

- `childGraphRef`: present only on nodes that own or reference a subgraph. Absent on ordinary nodes.
  - Blueprint `K2Node_Composite` → `kind: "inline"` (subgraph is embedded in the same asset)
  - PCG subgraph nodes (`UPCGSubgraphSettings`) → `kind: "asset"` (points to an external `UPCGGraph` asset)
  - Material `UMaterialExpressionMaterialFunctionCall` → `kind: "asset"` (points to an external `UMaterialFunction` asset)
- `childLoadStatus`: present when `childGraphRef` is set. `"loaded"` if the referenced asset is currently in memory; `"not_found"` if it could not be resolved.
- Material queries are intentionally current-asset only. When a material uses function calls, LOOMLE returns `childGraphRef` on those nodes and may emit a `MATERIAL_SUBGRAPH_REFS_PRESENT` diagnostic to remind callers to traverse into referenced `UMaterialFunction` graphs.
- The `graphRef` at the response root mirrors the effective locator used to resolve this query — clients can store it for later use without reconstructing it. Present on all three graph types.
- `layoutDetail`: optional query hint. `basic` requests lightweight geometry that should be cheap to compute. `measured` asks the runtime for richer layout data when supported.
- `meta.layoutDetailRequested` / `meta.layoutDetailApplied`: let callers distinguish what they asked for from what the runtime actually returned.
- If the runtime downgrades a measured request to basic layout data, diagnostics may include `LAYOUT_DETAIL_DOWNGRADED`.
- Current LOOMLE support guarantees node positions and move operations. Size/bounds support is partial today; for Blueprint, comment nodes may include model-derived size/bounds even when ordinary nodes do not.

## 5.5 tool=`graph.ops`

`args`:

```json
{
  "graphType": "blueprint|material|pcg",
  "query": "optional-text",
  "stability": "stable|experimental",
  "limit": 1000
}
```

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "ops": [
    {
      "opId": "string",
      "stability": "stable|experimental",
      "scope": "cross-graph|blueprint|material|pcg",
      "summary": "string"
    }
  ],
  "meta": {
    "source": "loomle_catalog|mixed",
    "coverage": "curated|partial"
  },
  "diagnostics": []
}
```

Notes:

- `graph.ops` lists LOOMLE's stable semantic operation catalog for the requested graph domain.
- This is a planning inventory surface, not an editor action-menu export.
- `graph.ops` results may be curated rather than exhaustive.

## 5.6 tool=`graph.ops.resolve`

`args`:

```json
{
  "graphType": "blueprint|material|pcg",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "context": {
    "fromPin": {
      "nodeId": "optional-id",
      "pinName": "optional-pin"
    },
    "toPin": {
      "nodeId": "optional-id",
      "pinName": "optional-pin"
    },
    "edge": {
      "fromPin": {
        "nodeId": "optional-id",
        "pinName": "optional-pin"
      },
      "toPin": {
        "nodeId": "optional-id",
        "pinName": "optional-pin"
      }
    }
  },
  "items": [
    {
      "opId": "string",
      "clientRef": "optional",
      "hints": {
        "targetRootPin": "optional-material-root-pin"
      }
    }
  ]
}
```

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "results": [
    {
      "opId": "string",
      "clientRef": "optional",
      "resolved": true,
      "compatibility": {
        "isCompatible": true,
        "reasons": []
      },
      "remediation": {
        "requiredContext": ["optional-context-kind"],
        "missingFields": ["optional.path"],
        "nextAction": "optional-guidance",
        "fallbackKind": "none|direct_mutate|manual_readback"
      },
      "preferredPlan": {
        "realizationKind": "spawn_node",
        "preferredMutateOp": "addNode.byClass",
        "args": {
          "nodeClassPath": "string"
        },
        "executionHints": [
          {
            "kind": "pipeline_insert",
            "preserveUpstream": true,
            "preserveDownstream": true,
            "composeMode": "independent|pipeline_segment"
          }
        ],
        "source": "typed_discovery|loomle_catalog|generic_fallback|mixed",
        "coverage": "contextual|curated|partial",
        "determinism": "stable|context_sensitive|ephemeral"
      },
      "alternatives": []
    }
  ],
  "diagnostics": []
}
```

Notes:

- `graph.ops.resolve` is a planning surface. It does not mutate the graph by itself.
- `graphRef` is required.
- `items[]` is batch-first so callers can compare several semantic operations under the same graph context.
- `context` may narrow resolution from graph scope to pin scope or edge scope.
- If an item cannot be resolved in the supplied context, it remains structured inside `results[]` with `resolved=false` and compatibility reasons.
- `remediation` may appear on unresolved items to say what narrower context or follow-up is required.
- `preferredPlan.pinHints` may identify important pin roles for follow-up wiring.
- `preferredPlan.settingsTemplate` may appear when a semantic op commonly needs key settings filled before the node is useful.
- `preferredPlan.verificationHints` may appear when readback is especially important after apply, for example on PCG flows.
- `preferredPlan.executionHints` may appear when a plan needs explicit insertion/composition semantics, especially for PCG.
- richer context, remediation, and execution hints are forward-compatible contract fields; callers should preserve unknown fields rather than assuming the current MVP exhausts the schema.
- Current MVP step generation is intentionally narrow:
  - Blueprint `core.reroute` and `bp.flow.branch` can emit `steps[]` when `context.fromPin` is supplied.
  - Material `mat.math.multiply` can emit `steps[]` when `items[*].hints.targetRootPin` is supplied.
  - Material `mat.texture.sample` can emit `steps[]` when `items[*].hints.targetRootPin` is supplied, and may also connect `context.fromPin` into `UVs`.
  - PCG `pcg.meta.add_tag` can emit `steps[]` when `context.fromPin` is supplied.
  - PCG `pcg.filter.by_tag` can emit `steps[]` when `context.fromPin` is supplied.

## 5.7 tool=`graph.mutate`

`args`:

Accepts the same two addressing modes as `graph.query` (Mode A: `assetPath` + `graphName`; Mode B: `graphRef`).

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "expectedRevision": "opaque-token",
  "idempotencyKey": "uuid",
  "dryRun": false,
  "continueOnError": false,
  "executionPolicy": {
    "stopOnError": true,
    "maxOps": 200
  },
  "ops": [
    {
      "op": "connectPins",
      "clientRef": "optional",
      "targetGraphName": "optional-override",
      "targetGraphRef": { "kind": "inline|asset", "...": "..." },
      "args": {}
    }
  ]
}
```

Notes:

- At the request root, `graphRef` and `graphName` are mutually exclusive.
- At the op level, `targetGraphRef`/`args.graphRef` and `targetGraphName` are mutually exclusive.
- `targetGraphRef.assetPath` must match the request-level `assetPath` (or the `assetPath` resolved from request-level `graphRef`) for the current mutate call.
- Runtime move ops currently include `moveNode`, `moveNodeBy`, and `moveNodes`.
- `moveNodeBy` accepts a single target plus either `dx`/`dy` or `delta.{x,y}`.
- `moveNodes` accepts `nodeIds` or `nodes` plus either `dx`/`dy` or `delta.{x,y}` and applies the same delta to every resolved node.
- `layoutGraph` supports Blueprint, Material, and PCG mutate flows with `args.scope="touched"| "all"`.
- For `scope="touched"`, LOOMLE uses the current graph's pending touched-node set accumulated from prior successful mutate ops; callers may also pass explicit `nodeIds` / `nodes` to narrow or supplement the layout set.

`payload`:

```json
{
  "applied": true,
  "partialApplied": false,
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "previousRevision": "opaque-token",
  "newRevision": "opaque-token",
  "opResults": [
    {
      "index": 0,
      "op": "connectPins",
      "ok": true,
      "changed": true,
      "nodeId": "optional",
      "error": "",
      "errorCode": "",
      "errorMessage": "",
      "movedNodeIds": ["optional-node-id"],
      "details": {}
    }
  ],
  "diagnostics": []
}
```

`setPinDefault` target notes:

- `args.target` accepts the same node token forms used elsewhere in Blueprint mutate flows: `nodeId`, `nodeRef`, `nodePath`, `path`, `nodeName`, or `name`.
- Pin name may be supplied as either `pin` or `pinName`.
- When `setPinDefault` fails with `TARGET_NOT_FOUND`, `opResults[*].details` may include `expectedTargetForms`, `requestedTarget`, `matchedNode`, and `candidatePins` to help callers repair the request automatically.

`layoutGraph` notes:

- `scope="touched"` is intended for agent-driven local cleanup after a small batch of graph edits.
- `scope="all"` reflows all resolvable nodes in the current graph.
- Blueprint uses an exec-tree planner that keeps the root event anchored near the left edge and fans `True/False` branches recursively.
- Material uses a dependency planner that places upstream expressions to the left and sink expressions to the right.
- PCG uses a pipeline planner that places source nodes to the left and downstream processing stages to the right.
- Successful `layoutGraph` results may include `opResults[*].movedNodeIds` with the nodes that were actually repositioned.

When `applied=false`:

- top-level `code` mirrors the first failing operation's `opResults[*].errorCode` (falls back to `INTERNAL_ERROR` only when classification is unavailable).
- top-level `message` mirrors the first failing operation's `opResults[*].errorMessage`.
- top-level `partialApplied=true` means one or more earlier operations in the ordered batch already committed with `changed=true` before the later failure was encountered.
- `graph.mutate` is currently ordered but non-transactional: the server does not roll back earlier successful ops when a later op fails.

## 5.9 tool=`pcg.inspectRuntime`

`args`:

```json
{
  "componentPath": "/Game/Maps/MyMap.MyMap:PersistentLevel.PCGVolume_0.PCGComponent0",
  "actorPath": "/Game/Maps/MyMap.MyMap:PersistentLevel.PCGVolume_0"
}
```

Field notes:

- Supply `componentPath` when you already know the exact PCG component to inspect.
- `actorPath`, `objectPath`, and `path` are accepted as fallbacks; when omitted, the server may fall back to the selected PCG actor/component in the editor.
- `managedResources` is the authoritative generated-result summary for spawned actors/components and instance counts.
- `generatedGraphOutput` can still be empty or sparse for common spawner-style graphs even when visible runtime results exist.

`payload`:

```json
{
  "ok": true,
  "resolvedBy": "componentPath|actorPath|selection|objectPath|path",
  "componentPath": "string",
  "actorPath": "string",
  "graphAssetPath": "/Game/...",
  "generated": true,
  "generating": false,
  "managedResourcesAccessible": true,
  "generatedGraphOutput": {
    "taggedDataCount": 0,
    "dataTypes": [],
    "pins": []
  },
  "managedResources": {
    "resourceCount": 1,
    "generatedActorCount": 0,
    "generatedComponentCount": 1,
    "totalInstanceCount": 128,
    "actors": [],
    "components": []
  },
  "inspection": {
    "available": true,
    "executedNodeCount": 3,
    "producedNodeCount": 2,
    "nodes": []
  },
  "diagnostics": []
}
```

## 5.10 tool=`diag.tail`

`args`:

```json
{
  "fromSeq": 0,
  "limit": 200,
  "filters": {
    "severity": "error",
    "category": "runtime",
    "source": "execute",
    "assetPathPrefix": "/Game/UI"
  }
}
```

`payload`:

```json
{
  "items": [],
  "nextSeq": 0,
  "hasMore": false,
  "highWatermark": 0
}
```

Storage note:

- Events are persisted under `<ProjectRoot>/Loomle/runtime/diag/diag.jsonl`.
- Current v1 sources include UE log warnings/errors and Blueprint compile failures.

Cursor semantics:

- `fromSeq` is exclusive (`seq > fromSeq`).
- `fromSeq` must be a non-negative integer, otherwise returns `1000 INVALID_ARGUMENT`.
- `limit` must be `>= 1`; values above `1000` are capped to `1000`.
- `nextSeq` equals the last returned event `seq`, or echoes `fromSeq` when `items` is empty.
- `hasMore=true` means more matching events are available after the returned page.
- `highWatermark` is the latest observed sequence at read time.

## 6. Error Codes

- `1000 INVALID_ARGUMENT`
- `1001 METHOD_NOT_FOUND`
- `1002 TOOL_NOT_FOUND`
- `1003 UNSUPPORTED_GRAPH_TYPE`
- `1004 ASSET_NOT_FOUND`
- `1005 GRAPH_NOT_FOUND`
- `1006 NODE_NOT_FOUND`
- `1007 PIN_NOT_FOUND`
- `1008 REVISION_CONFLICT`
- `1009 LIMIT_EXCEEDED`
- `1010 EXECUTION_TIMEOUT`
- `1011 INTERNAL_ERROR`
- `1012 GRAPH_REF_INVALID` — the supplied `GraphRef` is malformed, missing required fields, or specifies an unrecognized `kind`. Not retryable.
- `1013 GRAPH_REF_ASSET_NOT_LOADED` — the asset referenced by a `kind: "asset"` `GraphRef` is not currently loaded in the editor. Retryable after the asset is opened.
- `1014 GRAPH_REF_NOT_COMPOSITE` — the `nodeGuid` in a `kind: "inline"` `GraphRef` resolves to a node that does not own a subgraph. Not retryable.
