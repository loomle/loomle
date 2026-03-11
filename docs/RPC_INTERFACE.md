# Loomle RPC Interface (C++ External Boundary)

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
  "tools": ["context", "execute", "graph.list", "graph.query", "graph.actions", "graph.mutate"],
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
  "tool": "context|execute|graph.list|graph.query|graph.actions|graph.mutate",
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

- `includeSubgraphs` (optional, default `false`): recursively enumerate subgraphs contained within composite/subgraph nodes.
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

- `graphRef`: present on all entries. For root graphs the server emits an `asset`-kind ref; for inline subgraphs an `inline`-kind ref. Use this value as input to `graph.query`, `graph.actions`, or `graph.mutate` for direct addressing.
- `parentGraphRef`: `null` for root-level graphs; set to the parent's `graphRef` for subgraphs when `includeSubgraphs` is `true`.
- `ownerNodeId`: the `nodeId` of the composite/subgraph node that contains this graph. `null` for root graphs.
- `loadStatus`: present on `kind: "asset"` entries only. `"loaded"` means the asset is in memory; `"loading"` means an async load is in progress; `"not_found"` means the asset path could not be resolved.

## 5.4 tool=`graph.query`

`args`:

Two mutually exclusive addressing modes:

**Mode A — explicit (existing, unchanged):** `assetPath` + `graphName` both required.

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "filter": {
    "nodeClasses": ["optional-class"],
    "nodeIds": ["optional-id"],
    "text": "optional-text"
  },
  "limit": 200
}
```

**Mode B — GraphRef:** supply `graphRef` obtained from a prior `graph.list` or `graph.query` response. `assetPath` and `graphName` must be omitted.

```json
{
  "graphType": "blueprint|material|pcg",
  "graphRef": {
    "kind": "inline",
    "nodeGuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
    "assetPath": "/Game/BP_Foo"
  },
  "filter": {
    "nodeClasses": ["optional-class"],
    "nodeIds": ["optional-id"],
    "text": "optional-text"
  },
  "limit": 200
}
```

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
  "semanticSnapshot": {
    "signature": "string",
    "nodes": [
      {
        "nodeId": "string",
        "type": "string",
        "childGraphRef": {
          "kind": "inline",
          "nodeGuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
          "assetPath": "/Game/BP_Foo"
        },
        "childLoadStatus": "loaded|loading|not_found"
      }
    ],
    "edges": []
  },
  "meta": {
    "totalNodes": 0,
    "returnedNodes": 0,
    "totalEdges": 0,
    "returnedEdges": 0,
    "truncated": false
  },
  "diagnostics": []
}
```

Node field notes:

- `childGraphRef`: present only on nodes that own or reference a subgraph (e.g. `K2Node_Composite`, `UPCGSubgraphNode`, `UMaterialExpressionMaterialFunctionCall`). Absent on ordinary nodes.
- `childLoadStatus`: present only when `childGraphRef.kind == "asset"`. Indicates whether the referenced asset is currently loaded in the editor.
- The `graphRef` at the response root mirrors the effective locator used to resolve this query — clients can store it for later use without reconstructing it.

## 5.5 tool=`graph.actions`

`args`:

Accepts the same two addressing modes as `graph.query` (Mode A: `assetPath` + `graphName`; Mode B: `graphRef`).

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "graphRef": { "kind": "inline|asset", "...": "..." },
  "context": {
    "fromPin": {
      "nodeId": "optional-id",
      "pinName": "optional-pin"
    }
  },
  "query": "optional-text",
  "limit": 100
}
```

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "actions": [
    {
      "actionToken": "opaque-token",
      "title": "string",
      "categoryPath": "string",
      "tooltip": "string",
      "spawn": {
        "nodeClassPath": "string"
      }
    }
  ],
  "meta": {
    "total": 0,
    "returned": 0,
    "truncated": false
  },
  "diagnostics": []
}
```

## 5.6 tool=`graph.mutate`

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
  "ops": [
    {
      "op": "connectPins",
      "clientRef": "optional",
      "args": {}
    }
  ]
}
```

`payload`:

```json
{
  "applied": true,
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "previousRevision": "opaque-token",
  "newRevision": "opaque-token",
  "opResults": [
    {
      "index": 0,
      "op": "connectPins",
      "ok": true,
      "changed": true,
      "nodeId": "optional",
      "error": ""
    }
  ],
  "diagnostics": []
}
```

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
