# Loomle MCP Protocol (Standard-Aligned, Minimal Naming)

## 1. Scope

Defines the MCP-facing contract exposed by `Loomle MCP Server`.

Protocol baseline: MCP specification `2025-11-05` tools semantics.

## 2. Standard Compliance Decisions

1. Tool registration
- Every tool includes `name`, `title`, `description`, `inputSchema`, `outputSchema`.

2. Tool result
- Primary payload in `structuredContent` matching `outputSchema`.
- Optional human-readable summary in `content` text.

3. Error model
- Protocol-level malformed calls return MCP protocol error.
- Domain execution failures return tool result with `isError=true` and structured error payload.

4. Tool name style
- Minimal names kept as project convention.

## 3. Tool Catalog

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.query`
- `graph.actions`
- `graph.mutate`

## 4. MCP Tool Contracts

## 4.1 `loomle`

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {},
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["status", "runtime"],
  "properties": {
    "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
    "runtime": {
      "type": "object",
      "required": ["rpcConnected", "listenerReady", "rpcHealth"],
      "properties": {
        "rpcConnected": { "type": "boolean" },
        "listenerReady": { "type": "boolean" },
        "rpcHealth": {
          "type": "object",
          "required": ["status", "rpcVersion", "timestamp"],
          "properties": {
            "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
            "rpcVersion": { "type": "string" },
            "timestamp": { "type": "string" },
            "probeError": { "type": "string" }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

Execution rule:

- MCP local response.
- `rpc.health` probe is mandatory on every `loomle` call.
- Returned payload must include probe data in `runtime.rpcHealth`.

## 4.2 `context`

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "resolveIds": { "type": "array", "items": { "type": "string" } },
    "resolveFields": { "type": "array", "items": { "type": "string" } }
  },
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["timestamp", "context", "selection"],
  "properties": {
    "timestamp": { "type": "string" },
    "context": {
      "type": "object",
      "required": ["editorType"],
      "properties": {
        "editorType": { "type": "string", "enum": ["k2", "material", "pcg", "unknown"] },
        "assetPath": { "type": "string" }
      },
      "additionalProperties": false
    },
    "selection": {
      "type": "object",
      "required": ["count", "items"],
      "properties": {
        "count": { "type": "integer", "minimum": 0 },
        "items": { "type": "array", "items": { "type": "object" } }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=context`.

## 4.3 `execute`

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["code"],
  "properties": {
    "language": { "type": "string", "default": "ue-script" },
    "mode": { "type": "string", "enum": ["exec", "eval"], "default": "exec" },
    "code": { "type": "string", "minLength": 1 }
  },
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["ok", "durationMs"],
  "properties": {
    "ok": { "type": "boolean" },
    "stdout": { "type": "string" },
    "result": {},
    "durationMs": { "type": "integer", "minimum": 0 }
  },
  "additionalProperties": false
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=execute`.

## 4.4 `graph`

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "graphType": { "type": "string", "enum": ["k2", "material", "pcg"], "default": "k2" }
  },
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["status", "graphType", "version", "ops", "limits", "runtime"],
  "properties": {
    "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
    "graphType": { "type": "string", "enum": ["k2", "material", "pcg"] },
    "version": { "type": "string" },
    "ops": { "type": "array", "items": { "type": "string" } },
    "limits": {
      "type": "object",
      "required": ["defaultLimit", "maxLimit", "maxOpsPerMutate"],
      "properties": {
        "defaultLimit": { "type": "integer" },
        "maxLimit": { "type": "integer" },
        "maxOpsPerMutate": { "type": "integer" }
      },
      "additionalProperties": false
    },
    "runtime": {
      "type": "object",
      "required": ["rpcHealth"],
      "properties": {
        "rpcHealth": {
          "type": "object",
          "required": ["status", "rpcVersion", "timestamp"],
          "properties": {
            "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
            "rpcVersion": { "type": "string" },
            "timestamp": { "type": "string" },
            "probeError": { "type": "string" }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

Execution rule:

- MCP local descriptor response.
- `rpc.health` probe is mandatory on every `graph` call.
- Returned payload must include probe data in `runtime.rpcHealth` so callers can see real runtime status.

## 4.5 Graph Runtime Tools

For `graph.list`, `graph.query`, `graph.actions`, `graph.mutate`:

- Input/output schemas are the same as `RPC_INTERFACE.md` section 5 for corresponding tool payloads.
- Execution uses `rpc.invoke` with `tool` equal to MCP tool name.

## 5. MCP <-> RPC Error Mapping

Tool error payload shape (`structuredContent` when `isError=true`):

```json
{
  "domainCode": "string",
  "message": "string",
  "retryable": false,
  "detail": "string"
}
```

Mapping:

- RPC `1000` -> `INVALID_ARGUMENT`
- RPC `1001` -> `METHOD_NOT_FOUND`
- RPC `1002` -> `TOOL_NOT_FOUND`
- RPC `1003` -> `UNSUPPORTED_GRAPH_TYPE`
- RPC `1004` -> `ASSET_NOT_FOUND`
- RPC `1005` -> `GRAPH_NOT_FOUND`
- RPC `1006` -> `NODE_NOT_FOUND`
- RPC `1007` -> `PIN_NOT_FOUND`
- RPC `1008` -> `REVISION_CONFLICT`
- RPC `1009` -> `LIMIT_EXCEEDED`
- RPC `1010` -> `EXECUTION_TIMEOUT`
- RPC `1011` -> `INTERNAL_ERROR`
