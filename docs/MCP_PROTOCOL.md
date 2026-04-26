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
- `graph.mutate` `runScript` is currently a Blueprint-only graph-scoped fallback for already-resolved target graphs.
- For non-graph operations, Material or PCG graph scripting, unsupported graph types, or graph-domain capabilities not yet covered by `graph.*`, prefer `execute` instead of `runScript`.

4. Tool name style
- Minimal names kept as project convention.

5. Schema discovery
- `tools/list` is the runtime source of truth for MCP tool names, descriptions, and `inputSchema`.
- Clients should prefer `tools/list` over hardcoded argument examples when validating or rendering tool call shapes.

## 3. Tool Catalog

- `loomle`
- `context`
- `execute`
- `jobs`
- `profiling`
- `play`
- `editor.open`
- `editor.focus`
- `editor.screenshot`
- `graph`
- `graph.list`
- `graph.resolve`
- `graph.query`
- `graph.mutate`
- `graph.verify`
- `widget.query`
- `widget.mutate`
- `widget.verify`
- `widget.describe`
- `diagnostic.tail`
- `log.tail`
- `log.subscribe`

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
        "capabilities": {
          "type": "object",
          "properties": {
            "executeAvailable": { "type": "boolean" },
            "jobsAvailable": { "type": "boolean" },
            "graphToolsAvailable": { "type": "boolean" },
            "editorToolsAvailable": { "type": "boolean" }
          },
          "additionalProperties": false
        },
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
- `PIE` should be surfaced through `runtime.isPIE` and `runtime.capabilities`, not collapsed into a blanket runtime-tools deny bit.

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

`execute` runs Python inside the Unreal Editor process. Prefer it for non-graph editor automation and for graph types or graph-domain capabilities not yet covered by `graph.*`. Do not prefer it when a structured `graph.resolve`, `graph.query`, `graph.mutate`, or `graph.verify` path already covers the task.

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["code"],
  "properties": {
    "language": { "type": "string", "default": "python" },
    "mode": { "type": "string", "enum": ["exec", "eval"], "default": "exec" },
    "code": { "type": "string", "minLength": 1 },
    "execution": {
      "type": "object",
      "properties": {
        "mode": { "type": "string", "enum": ["sync", "job"], "default": "sync" },
        "idempotencyKey": { "type": "string", "minLength": 1 },
        "label": { "type": "string", "minLength": 1 },
        "waitMs": { "type": "integer", "minimum": 1 },
        "resultTtlMs": { "type": "integer", "minimum": 1 }
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
  "required": ["ok", "durationMs"],
  "properties": {
    "ok": { "type": "boolean" },
    "stdout": { "type": "string" },
    "result": {},
    "durationMs": { "type": "integer", "minimum": 0 },
    "job": {
      "type": "object",
      "required": ["jobId", "status"],
      "properties": {
        "jobId": { "type": "string" },
        "status": { "type": "string", "enum": ["queued", "running", "succeeded", "failed"] },
        "idempotencyKey": { "type": "string" },
        "acceptedAt": { "type": "string" },
        "pollAfterMs": { "type": "integer", "minimum": 1 }
      },
      "additionalProperties": true
    },
    "runtime": {
      "type": "object",
      "properties": {
        "isPIE": { "type": "boolean" },
        "editorWorld": { "type": "string" },
        "pieWorld": { "type": "string" },
        "activeWorld": { "type": "string" },
        "activeWorldType": { "type": "string" },
        "sessionMode": { "type": "string" }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": true
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=execute`.
- Agent-local Python is separate and does not replace Unreal-side `execute`.
- `execution.mode = "sync"` remains the default path.
- `execution.mode = "job"` requests registration in the shared jobs runtime.
- When job mode is accepted, lifecycle inspection moves to top-level `jobs`.
- `execute` remains available during `PIE`.
- `execute` responses should expose runtime context such as `isPIE`, active world, and world type so callers can distinguish editor-world reads from gameplay-world reads.

## 4.4 `jobs`

Use `jobs` to inspect or collect lifecycle state for long-running submissions created through `execution.mode = "job"`.

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": { "type": "string", "enum": ["status", "result", "logs", "list"] },
    "jobId": { "type": "string", "minLength": 1 },
    "cursor": { "type": "string" },
    "status": { "type": "string", "enum": ["queued", "running", "succeeded", "failed"] },
    "tool": { "type": "string", "minLength": 1 },
    "sessionId": { "type": "string", "minLength": 1 },
    "limit": { "type": "integer", "minimum": 1, "maximum": 1000 }
  },
  "additionalProperties": false
}
```

Output schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "status": { "type": "string", "enum": ["queued", "running", "succeeded", "failed"] },
    "jobId": { "type": "string" },
    "tool": { "type": "string" },
    "acceptedAt": { "type": "string" },
    "startedAt": { "type": "string" },
    "finishedAt": { "type": "string" },
    "heartbeatAt": { "type": "string" },
    "resultAvailable": { "type": "boolean" },
    "message": { "type": "string" },
    "logCursor": { "type": "string" },
    "entries": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
    "nextCursor": { "type": "string" },
    "hasMore": { "type": "boolean" },
    "result": {},
    "stdout": { "type": "string" },
    "error": { "type": "object", "additionalProperties": true },
    "jobs": { "type": "array", "items": { "type": "object", "additionalProperties": true } }
  },
  "additionalProperties": true
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=jobs`.
- `jobs` is a top-level lifecycle surface. Do not model it as `execute.jobs` or `graph.jobs`.
- `status`, `result`, and `logs` operate on one `jobId`; `list` returns currently known jobs.
- `jobs` remains available during `PIE` so long-running task lifecycle is observable during gameplay sessions.

## 4.5 `profiling`

Use `profiling` to bridge official Unreal profiling data families into stable structured payloads.

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": { "type": "string", "enum": ["unit", "game", "gpu", "ticks", "memory", "capture"] },
    "world": { "type": "string", "enum": ["active", "editor", "pie"], "default": "active" },
    "gpuIndex": { "type": "integer", "minimum": 0, "default": 0 },
    "includeRaw": { "type": "boolean", "default": true },
    "includeGpuUtilization": { "type": "boolean", "default": true },
    "includeHistory": { "type": "boolean", "default": false },
    "group": { "type": "string", "minLength": 1 },
    "displayMode": { "type": "string", "enum": ["flat", "hierarchical", "both"] },
    "includeThreadBreakdown": { "type": "boolean" },
    "sortBy": { "type": "string", "enum": ["sum", "call_count", "name"] },
    "maxDepth": { "type": "integer", "minimum": 1, "maximum": 32 },
    "mode": { "type": "string", "enum": ["all", "grouped", "enabled", "disabled"] },
    "kind": { "type": "string" },
    "execution": {
      "type": "object",
      "properties": {
        "mode": { "type": "string", "enum": ["sync", "job"], "default": "sync" },
        "idempotencyKey": { "type": "string", "minLength": 1 },
        "label": { "type": "string", "minLength": 1 },
        "waitMs": { "type": "integer", "minimum": 1 },
        "resultTtlMs": { "type": "integer", "minimum": 1 }
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
  "required": ["runtime", "source"],
  "properties": {
    "runtime": { "type": "object", "additionalProperties": true },
    "source": { "type": "object", "additionalProperties": true },
    "data": { "type": "object", "additionalProperties": true }
  },
  "additionalProperties": true
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=profiling`.
- `profiling` is a data bridge, not an analysis layer.
- Current live actions are:
  - `unit`
  - `game`
  - `gpu`
  - `ticks`
  - `memory` with `kind="summary"`
- `profiling` remains callable during `PIE`.
- `unit`, `game`, and `gpu` may return retryable warmup errors before official engine aggregates become valid.

## 4.6 `play`

Use `play` to inspect and, over time, control Unreal play sessions as a first-class runtime concept.

The first implemented actions are read-only `status`, PIE `start`, idempotent `stop`, and client-side `wait`.

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": { "type": "string", "enum": ["status", "start", "stop", "wait"] },
    "backend": { "type": "string", "enum": ["pie"] },
    "sessionId": { "type": "string" },
    "map": { "type": "string" },
    "ifActive": { "type": "string", "enum": ["error", "returnStatus"] },
    "timeoutMs": { "type": "integer" },
    "until": { "type": "object", "additionalProperties": true },
    "topology": { "type": "object", "additionalProperties": true },
    "defaultClientWindow": { "type": "object", "additionalProperties": true }
  },
  "additionalProperties": false
}
```

Output shape:

```json
{
  "status": "ok",
  "session": {
    "id": "pie-active",
    "backend": "pie",
    "state": "ready",
    "map": "/Game/Maps/TestMap",
    "topology": { "clientCount": 1 }
  },
  "participants": [
    {
      "id": "client:0",
      "role": "client",
      "index": 0,
      "kind": "client",
      "ready": true,
      "world": {
        "name": "UEDPIE_0_TestMap",
        "path": "/Game/Maps/UEDPIE_0_TestMap",
        "worldType": "pie",
        "netMode": "client",
        "pieInstance": 0
      }
    }
  ],
  "observability": {
    "diagnostics": { "tool": "diagnostic.tail", "fromSeq": 120 },
    "logs": { "tool": "log.tail", "fromSeq": 500 }
  },
  "diagnostics": []
}
```

Execution rule:

- Forward `status`, `start`, and `stop` via `rpc.invoke` with `tool=play`.
- `play.status` is the source of truth for the current session and participant IDs.
- `play.start` currently requests an in-process PIE session and returns `state="starting"` until Unreal creates runtime worlds.
- `play.stop` ends the active PIE session when one exists and returns the same status shape.
- `play.wait` is implemented in the MCP client by polling `play.status`, so Unreal's editor thread can continue ticking between polls. It supports session-state waits and participant conditions such as `{ "role": "client", "count": 2, "state": "ready" }`.
- `play` owns lifecycle and topology; it does not replace `profiling`, `execute`, `jobs`, `diagnostic.tail`, or `log.tail`.

## 4.7 `graph`

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

## 4.8 Runtime Tools

For `jobs`, `profiling`, `play`, `editor.open`, `editor.focus`, `editor.screenshot`, `graph.list`, `graph.resolve`, `graph.query`, `graph.verify`, `graph.mutate`, `widget.query`, `widget.mutate`, `widget.verify`, `widget.describe`, `diagnostic.tail`, `log.tail`:

- Input/output schemas are exposed directly through MCP `tools/list` and should be treated as the live contract.
- `RPC_INTERFACE.md` section 5 documents the same tool payloads at the Unreal RPC boundary.
- Execution uses `rpc.invoke` with `tool` equal to MCP tool name.
- Current server behavior performs a runtime preflight using `rpc.health` with a short cache TTL (`200ms`) shared across runtime-tool calls.
- If preflight reports `PIE`, `execute`, `jobs`, `profiling`, and `play` remain callable.
- If preflight reports `PIE`, editor-facing and graph-structured runtime tools continue to fail fast with `EDITOR_BUSY` (`retryable=true`) and skip `rpc.invoke`.
- On Windows named-pipe transport, open failures with OS error `231` (`all pipe instances are busy`) are treated as transient and retried with bounded backoff before returning an error.

Practical client rule:

- Use `tools/list` when you need the current accepted `inputSchema`.
- Use `RPC_INTERFACE.md` when you need lower-level transport and payload shape details.
- Prefer workspace-local guides plus `graph.resolve`, `graph.query`, `graph.mutate`, and `graph.verify` for graph planning and execution.
- For `graph.mutate`, individual `opResults[]` entries may include operation-specific `details` objects on failure; clients should preserve and inspect them rather than relying only on top-level `message`.
- For graph layout-aware flows, prefer `graph.layoutCapabilities` and `graph.query.meta.layoutCapabilities` over hardcoded assumptions. Current runtime support focuses on position reads plus basic move operations.
- `graph.query` accepts `layoutDetail=basic|measured`. Callers may request `measured`, but should inspect `meta.layoutDetailApplied` and diagnostics before assuming measured geometry was actually returned.

## 4.8 `graph.verify`

Use this as the graph verification primitive after `graph.query` or `graph.mutate`.

Execution rule:

- Forward via `rpc.invoke` with `tool=graph.verify`.
- `graph.verify` always performs final compile/refresh-backed verification for Blueprint, Material, or PCG graphs.
- `graph.query` remains the lightweight source of current semantic diagnostics.
- `graph.verify` is graph-scoped only. It does not inspect scene instances, selected actors/components, or generated runtime output.

## 4.10 `widget.describe`

Use `widget.describe` to enumerate the editable properties of a UMG widget class before calling `widget.mutate setProperty`. This tool closes the loop between property discovery and mutation: any property returned here can be set directly with the same name via `setProperty`.

Input schema:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "widgetClass": {
      "type": "string",
      "description": "Short class name (e.g. \"TextBlock\") or full path (e.g. \"/Script/UMG.TextBlock\"). Required if assetPath/widgetName not provided."
    },
    "assetPath": {
      "type": "string",
      "description": "Asset path to a WidgetBlueprint. Used together with widgetName to resolve class and read currentValues."
    },
    "widgetName": {
      "type": "string",
      "description": "Designer name of the widget instance inside the WidgetTree. Required when assetPath is provided."
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
  "required": ["widgetClass", "properties", "slotProperties"],
  "properties": {
    "widgetClass": { "type": "string", "description": "Full UClass path of the described widget type." },
    "properties": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name":     { "type": "string" },
          "type":     { "type": "string" },
          "category": { "type": "string" },
          "writable": { "type": "boolean" }
        }
      }
    },
    "slotProperties": {
      "type": "array",
      "description": "Properties on the widget's current slot (layout/alignment/padding). Only present when a live instance is resolved via assetPath+widgetName.",
      "items": {
        "type": "object",
        "properties": {
          "name":     { "type": "string" },
          "type":     { "type": "string" },
          "writable": { "type": "boolean" }
        }
      }
    },
    "currentValues": {
      "type": "object",
      "description": "Current property values of the live widget instance, keyed by property name. Only present when assetPath+widgetName are provided.",
      "additionalProperties": { "type": "string" }
    }
  },
  "additionalProperties": false
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=widget.describe`.
- Provide `widgetClass` for class-only introspection (no asset needed).
- Provide `assetPath` + `widgetName` to resolve the class from a live instance and attach `currentValues`.
- Both `widgetClass` and `assetPath+widgetName` may be provided simultaneously; the explicit `widgetClass` takes precedence for property enumeration, and the instance is still resolved for `currentValues`.
- Only editor-visible (`CPF_Edit`) or Blueprint-accessible (`CPF_BlueprintVisible`) properties with a non-empty `Category` metadata are included. Internal C++ properties are excluded.
- Property names returned here map directly to the `property` field in `widget.mutate setProperty` ops.

## 4.9 `diagnostic.tail`

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
  "required": ["items", "nextSeq", "nextFromSeq", "hasMore", "latestSeq", "highWatermark"],
  "properties": {
    "items": { "type": "array", "items": { "type": "object", "additionalProperties": true } },
    "fromSeq": { "type": "integer", "minimum": 0 },
    "nextSeq": { "type": "integer", "minimum": 0 },
    "nextFromSeq": { "type": "integer", "minimum": 0 },
    "hasMore": { "type": "boolean" },
    "latestSeq": { "type": "integer", "minimum": 0 },
    "highWatermark": { "type": "integer", "minimum": 0 }
  },
  "additionalProperties": false
}
```

Execution rule:

- Forward via `rpc.invoke` with `tool=diagnostic.tail`.
- `fromSeq` is exclusive: returned events satisfy `seq > fromSeq`.
- `fromSeq` must be a non-negative integer, otherwise returns `INVALID_ARGUMENT`.
- `limit` must be `>= 1`; values larger than `1000` are capped to `1000`.
- `nextSeq` is the largest returned `seq` (or echoes `fromSeq` when no items returned). This field is retained for compatibility.
- `latestSeq` is the current latest known `seq` at read time.
- `highWatermark` is retained as an alias of `latestSeq`.
- `nextFromSeq` is the recommended cursor for the next polling call; it equals `nextSeq` when `hasMore=true`, otherwise it advances to `latestSeq`.

## 4.10 `log.tail`

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
        "minVerbosity": { "type": "string" },
        "category": { "type": "string", "minLength": 1 },
        "categories": { "type": "array", "items": { "type": "string" } },
        "source": { "type": "string", "minLength": 1 },
        "contains": { "type": "string", "minLength": 1 }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

Output and cursor semantics match `diagnostic.tail`.

Execution rule:

- Forward via `rpc.invoke` with `tool=log.tail`.
- Use this for high-volume Unreal Output Log evidence; default agent context should prefer `diagnostic.tail`.

## 4.11 `log.subscribe`

Input schema:

```json
{
  "type": "object",
  "properties": {
    "action": { "type": "string", "default": "subscribe" },
    "subscriptionId": { "type": "string" },
    "filters": {
      "type": "object",
      "properties": {
        "minVerbosity": { "type": "string" },
        "category": { "type": "string" },
        "categories": { "type": "array", "items": { "type": "string" } },
        "source": { "type": "string" },
        "contains": { "type": "string" }
      },
      "additionalProperties": false
    },
    "maxPerSecond": { "type": "integer", "minimum": 1, "maximum": 100, "default": 20 }
  },
  "additionalProperties": false
}
```

Execution rule:

- This is an MCP proxy tool, not an Unreal RPC tool.
- `action=subscribe` creates a filtered server-side stream and emits best-effort `notifications/loomle/log`.
- `action=update` replaces an existing `subscriptionId` stream with new filters.
- `action=unsubscribe` cancels the stream for `subscriptionId`.
- Agent hosts may ignore notifications or keep them out of model context. Treat `log.tail` with `fromSeq` / `nextFromSeq` as the source of truth for consuming logs.
- `diagnostic.tail` also has a default low-volume notification stream through `notifications/loomle/diagnostic`; `diagnostic.tail` remains the recovery source of truth.

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
- RPC `1023` -> `WIDGET_TREE_UNAVAILABLE`
- RPC `1024` -> `WIDGET_PARENT_NOT_PANEL`
- RPC `1025` -> `WIDGET_CLASS_NOT_FOUND`
