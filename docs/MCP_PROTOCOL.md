# LOOMLE MCP Protocol (Standard-Aligned, Minimal Naming)

## 1. Scope

Defines the MCP-facing contract exposed by `LOOMLE MCP Server`.

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

Graph mutate note:
- `graph.mutate` may return operation-specific structured fields such as `opResults[*].details` and `opResults[*].movedNodeIds`; clients should preserve unknown fields on `opResults[]`.
- `graph.mutate` `layoutGraph` currently supports `scope="touched"` and `scope="all"` for Blueprint, Material, and PCG graphs; `scope="touched"` consumes the graph-local pending touched-node set built from earlier successful mutate ops.
- `graph.mutate` batches execute in order and are not transactional today; if a later op fails after an earlier op already changed the graph, the top-level result returns `applied=false` and `partialApplied=true`.

4. Tool name style
- Minimal names kept as project convention.

5. Schema discovery
- `tools/list` is the runtime source of truth for MCP tool names, descriptions, and `inputSchema`.
- Clients should prefer `tools/list` over hardcoded argument examples when validating or rendering tool call shapes.

## 3. Tool Catalog

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.ops`
- `graph.ops.resolve`
- `graph.query`
- `graph.mutate`
- `graph.verify`
- `diag.tail`

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
    "status": { "type": "string", "enum": ["ok", "degraded", "blocked", "error"] },
    "domainCode": { "type": "string" },
    "message": { "type": "string" },
    "runtime": {
      "type": "object",
      "required": ["rpcConnected", "listenerReady", "rpcHealth"],
      "properties": {
        "rpcConnected": { "type": "boolean" },
        "listenerReady": { "type": "boolean" },
        "isPIE": { "type": "boolean" },
        "editorBusyReason": { "type": "string" },
        "acceptsRuntimeTools": { "type": "boolean" },
        "rpcHealth": {
          "type": "object",
          "required": ["status", "rpcVersion", "timestamp"],
          "properties": {
            "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
            "rpcVersion": { "type": "string" },
            "timestamp": { "type": "string" },
            "probeError": { "type": "string" },
            "isPIE": { "type": "boolean" },
            "editorBusyReason": { "type": "string" }
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
        "editorType": { "type": "string", "enum": ["blueprint", "material", "pcg", "unknown"] },
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
    "graphType": { "type": "string", "enum": ["blueprint", "material", "pcg"], "default": "blueprint" }
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
    "status": { "type": "string", "enum": ["ok", "degraded", "blocked", "error"] },
    "graphType": { "type": "string", "enum": ["blueprint", "material", "pcg"] },
    "version": { "type": "string" },
    "domainCode": { "type": "string" },
    "message": { "type": "string" },
    "ops": { "type": "array", "items": { "type": "string" } },
    "layoutCapabilities": {
      "type": "object",
      "properties": {
        "canReadPosition": { "type": "boolean" },
        "canReadSize": { "type": "boolean" },
        "canReadBounds": { "type": "boolean" },
        "canMoveNode": { "type": "boolean" },
        "canBatchMove": { "type": "boolean" },
        "supportsMeasuredGeometry": { "type": "boolean" },
        "positionSource": { "type": "string" },
        "sizeSource": { "type": "string" }
      },
      "additionalProperties": false
    },
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
        "isPIE": { "type": "boolean" },
        "editorBusyReason": { "type": "string" },
        "acceptsRuntimeTools": { "type": "boolean" },
        "rpcHealth": {
          "type": "object",
          "required": ["status", "rpcVersion", "timestamp"],
          "properties": {
            "status": { "type": "string", "enum": ["ok", "degraded", "error"] },
            "rpcVersion": { "type": "string" },
            "timestamp": { "type": "string" },
            "probeError": { "type": "string" },
            "isPIE": { "type": "boolean" },
            "editorBusyReason": { "type": "string" }
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

## 4.5 Runtime Tools

For `graph.list`, `graph.query`, `graph.verify`, `graph.ops`, `graph.ops.resolve`, `graph.mutate`, `diag.tail`:

- Input/output schemas are exposed directly through MCP `tools/list` and should be treated as the live contract.
- `RPC_INTERFACE.md` section 5 documents the same tool payloads at the Unreal RPC boundary.
- Execution uses `rpc.invoke` with `tool` equal to MCP tool name.
- Current server behavior performs a runtime preflight using `rpc.health` with a short cache TTL (`200ms`) shared across runtime-tool calls.
- If preflight reports PIE, runtime tools fail fast with `EDITOR_BUSY` (`retryable=true`) and skip `rpc.invoke`.
- On Windows named-pipe transport, open failures with OS error `231` (`all pipe instances are busy`) are treated as transient and retried with bounded backoff before returning an error.

Practical client rule:

- Use `tools/list` when you need the current accepted `inputSchema`.
- Use `RPC_INTERFACE.md` when you need lower-level transport and payload shape details.
- Prefer `graph.ops` and `graph.ops.resolve` for new semantic planning flows.
- `graph.ops.resolve` context may evolve beyond graph-level scope; callers should be prepared for narrower context fields such as pin- or edge-scoped inputs.
- unresolved `graph.ops.resolve` results may include structured remediation metadata that tells the caller which context or follow-up is missing.
- resolved `graph.ops.resolve` plans may include richer machine-usable metadata such as `pinHints`, `verificationHints`, and `executionHints`; callers should preserve unknown fields instead of assuming a minimal fixed shape.
- For `graph.mutate`, individual `opResults[]` entries may include operation-specific `details` objects on failure; clients should preserve and inspect them rather than relying only on top-level `message`.
- For graph layout-aware flows, prefer `graph.layoutCapabilities` and `graph.query.meta.layoutCapabilities` over hardcoded assumptions. Current runtime support focuses on position reads plus basic move operations.
- `graph.query` accepts `layoutDetail=basic|measured`. Callers may request `measured`, but should inspect `meta.layoutDetailApplied` and diagnostics before assuming measured geometry was actually returned.

## 4.6 `graph.verify`

Use this as the graph verification primitive after `graph.query`, `graph.ops.resolve`, or `graph.mutate`.

Execution rule:

- Forward via `rpc.invoke` with `tool=graph.verify`.
- `mode="health"` summarizes graph diagnostics from the latest semantic snapshot.
- `mode="compile"` runs explicit compile/refresh verification for Blueprint, Material, or PCG graphs.
- `graph.verify` is graph-scoped only. It does not inspect scene instances, selected actors/components, or generated runtime output.

## 4.7 `diag.tail`

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "fromSeq": { "type": "integer", "minimum": 0 },
    "limit": { "type": "integer", "minimum": 1, "maximum": 1000, "default": 200 },
    "filters": {
      "type": "object",
      "properties": {
        "severity": { "type": "string", "enum": ["error", "warning", "info"] },
        "category": { "type": "string", "minLength": 1 },
        "source": { "type": "string", "minLength": 1 },
        "assetPathPrefix": { "type": "string", "minLength": 1 }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["items", "nextSeq", "hasMore", "highWatermark"],
  "properties": {
    "items": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
    "nextSeq": { "type": "integer", "minimum": 0 },
    "hasMore": { "type": "boolean" },
    "highWatermark": { "type": "integer", "minimum": 0 }
  },
  "additionalProperties": false
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=diag.tail`.
- `fromSeq` is exclusive: returned events satisfy `seq > fromSeq`.
- `fromSeq` must be a non-negative integer, otherwise returns `INVALID_ARGUMENT`.
- `limit` must be `>= 1`; values larger than `1000` are capped to `1000`.
- `nextSeq` is the largest returned `seq` (or echoes `fromSeq` when no items returned).
- `highWatermark` is the current latest known `seq` at read time.

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
- RPC `1012` -> `GRAPH_REF_INVALID`
- RPC `1013` -> `GRAPH_REF_ASSET_NOT_LOADED`
- RPC `1014` -> `GRAPH_REF_NOT_COMPOSITE`
