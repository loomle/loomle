# Loomle Graph

## 1. Scope

- Protocol: JSON-RPC 2.0 (bridge tools).
- Graph types (v1): `blueprint`.
- Reserved graph types: `material`, `niagara`.
- Tools: `graph`, `graph.list`, `graph.query`, `graph.mutate`, `graph.watch`.

## 2. Tool Contracts

### 2.1 `graph`

Request:

```json
{"name":"graph","arguments":{"graphType":"blueprint"}}
```

Response:

```json
{
  "version": "1.0",
  "graphType": "blueprint",
  "features": {
    "query": true,
    "mutate": true,
    "watch": true,
    "revision": true,
    "dryRun": true,
    "transactions": true
  },
  "limits": {
    "defaultLimit": 200,
    "maxLimit": 1000,
    "maxOpsPerMutate": 200
  },
  "nodeCoreFields": ["id","classPath","title","graphName","position","enabled","pins"],
  "pinCoreFields": ["name","direction","category","subCategory","subCategoryObject","defaultValue","links"],
  "nodeExtensions": ["callFunction","dynamicCast","comment"],
  "ops": [
    "addNode.event",
    "addNode.cast",
    "addNode.callFunction",
    "addNode.branch",
    "addNode.variableGet",
    "addNode.variableSet",
    "connectPins",
    "disconnectPins",
    "breakPinLinks",
    "setPinDefault",
    "removeNode",
    "moveNode",
    "addComponent",
    "compile",
    "spawnActor"
  ]
}
```

### 2.2 `graph.list`

Request:

```json
{
  "name": "graph.list",
  "arguments": {
    "graphType": "blueprint",
    "assetPath": "/Game/Codex/BP_BouncyPad"
  }
}
```

Response:

```json
{
  "graphType": "blueprint",
  "assetPath": "/Game/Codex/BP_BouncyPad",
  "graphs": [
    {"graphName":"EventGraph","graphKind":"Event","graphClassPath":"/Script/Engine.EdGraph"}
  ],
  "diagnostics": []
}
```

### 2.3 `graph.query`

Request:

```json
{
  "name": "graph.query",
  "arguments": {
    "graphType": "blueprint",
    "assetPath": "/Game/Codex/BP_BouncyPad",
    "graphName": "EventGraph",
    "filter": {
      "nodeClasses": ["/Script/BlueprintGraph.K2Node_DynamicCast"],
      "nodeIds": [],
      "text": "",
      "bbox": null
    },
    "limit": 200
  }
}
```

Response:

```json
{
  "graphType": "blueprint",
  "assetPath": "/Game/Codex/BP_BouncyPad",
  "graphName": "EventGraph",
  "revision": "bp:3f8a9b26",
  "semanticSnapshot": {
    "signature": "string",
    "nodes": [],
    "edges": []
  },
  "nextCursor": "",
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

### 2.4 `graph.mutate`

Request:

```json
{
  "name": "graph.mutate",
  "arguments": {
    "graphType": "blueprint",
    "assetPath": "/Game/Codex/BP_BouncyPad",
    "graphName": "EventGraph",
    "expectedRevision": "bp:3f8a9b26",
    "idempotencyKey": "c2a7d7a8-8877-4d96-a522-f15ed1eb1d81",
    "dryRun": false,
    "continueOnError": false,
    "ops": []
  }
}
```

Response:

```json
{
  "applied": true,
  "graphType": "blueprint",
  "assetPath": "/Game/Codex/BP_BouncyPad",
  "graphName": "EventGraph",
  "previousRevision": "bp:3f8a9b26",
  "newRevision": "bp:727bd4f1",
  "opResults": [],
  "diagnostics": []
}
```

### 2.5 `graph.watch`

Request:

```json
{
  "name": "graph.watch",
  "arguments": {
    "graphType": "blueprint",
    "assetPath": "/Game/Codex/BP_BouncyPad",
    "cursor": 0,
    "limit": 100
  }
}
```

Response:

```json
{
  "cursor": 210,
  "nextCursor": 224,
  "count": 0,
  "running": true,
  "dropped": false,
  "events": []
}
```

## 3. Data Schema

### 3.1 Node

```json
{
  "id": "string-guid",
  "nodeClassPath": "string",
  "title": "string",
  "graphName": "string",
  "position": {"x": 0, "y": 0},
  "enabled": true,
  "memberReference": {},
  "functionReference": {},
  "pins": [],
  "k2Extensions": {}
}
```

### 3.2 Pin

```json
{
  "name": "string",
  "direction": "input|output",
  "type": {"category":"string","subCategory":"string","object":"string","container":"none|array|set|map"},
  "default": {"value":"string","object":"string","text":"string"},
  "links": [
    {"toNodeId":"string-guid","toPin":"string"}
  ]
}
```

### 3.3 Edge

```json
{
  "fromNodeId": "string-guid",
  "fromPin": "string",
  "toNodeId": "string-guid",
  "toPin": "string"
}
```

## 4. Error Model

Error shape:

```json
{
  "code": "string",
  "message": "string",
  "retryable": false,
  "details": {}
}
```

Canonical codes:

- `INVALID_ARGUMENT`
- `UNSUPPORTED_GRAPH_TYPE`
- `ASSET_NOT_FOUND`
- `GRAPH_NOT_FOUND`
- `NODE_NOT_FOUND`
- `PIN_NOT_FOUND`
- `FUNCTION_NOT_FOUND`
- `CLASS_NOT_FOUND`
- `COMPONENT_NOT_FOUND`
- `CONNECTION_FAILED`
- `COMPILE_FAILED`
- `REVISION_CONFLICT`
- `LIMIT_EXCEEDED`
- `INTERNAL_ERROR`

## 5. Compatibility

- `context` remains available.
- `live` remains available.
- `execute` remains available as fallback.
- Internal mapping:
  - `context` -> `graph.query` (when explicit graph args are provided)
  - `graph.watch` -> graph-scoped view from unified event bus
  - `live` -> editor + graph scopes from unified event bus

## 6. Rules

- `idempotencyKey` required for external mutate callers.
- `expectedRevision` required for mutating existing assets.
- `dryRun=true` must not persist graph changes.
- `ops[]` execute in order.
- `clientRef` may be used by later ops as `nodeRef`.
