# Loomle RPC Interface (C++ External Boundary)

## 1. Scope

Defines the RPC methods exposed by Unreal-side `RPC Listener`.

- Protocol: JSON-RPC 2.0
- Framing: NDJSON
- Version: `rpcVersion = 1.0`

## 2. Common Envelope

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
    "dryRun": true
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
  "assetPath": "/Game/..."
}
```

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphs": [
    {
      "graphName": "string",
      "graphKind": "string",
      "graphClassPath": "string"
    }
  ],
  "diagnostics": []
}
```

## 5.4 tool=`graph.query`

`args`:

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

`payload`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
  "revision": "opaque-token",
  "semanticSnapshot": {
    "signature": "string",
    "nodes": [],
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

## 5.5 tool=`graph.actions`

`args`:

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
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

```json
{
  "graphType": "blueprint|material|pcg",
  "assetPath": "/Game/...",
  "graphName": "EventGraph",
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
