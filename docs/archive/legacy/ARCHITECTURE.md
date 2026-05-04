# LOOMLE Bridge Architecture

## 1. Objective

Build a clean split where:

- `loomle` is the standard MCP client.
- `LoomleBridge` owns the runtime authority and native MCP server path.
- project-scoped socket/pipe transport connects the MCP proxy to the Unreal-hosted runtime.

## 2. Topology

1. MCP Client
- Sends MCP requests.

2. `loomle` MCP Proxy (Rust)
- Implements MCP lifecycle and public tool contracts.
- Maintains a stable runtime RPC session per attached Unreal endpoint.
- Bridges runtime-dependent tools to Unreal via JSON-RPC.

3. `LoomleBridge` Runtime (C++)
- Owns project-scoped socket/pipe lifecycle.
- Executes runtime tool handlers.
- Keeps the client connection open for a stable RPC session.

4. Unreal Runtime Handlers (C++)
- Execute domain operations and return deterministic results.

## 3. Boundary Policy

1. MCP boundary
- Standard MCP only.
- No Unreal transport concepts leak outward.

2. Transport boundary
- JSON-RPC 2.0 + NDJSON framing.
- Windows transport: project-scoped Named Pipe derived from normalized `project_root`.
- macOS/Linux transport: Unix Socket.

3. C++ design scope
- This design specifies runtime protocol contracts only.
- Internal classes/functions in C++ are out of scope.

## 4. Minimal Name Mapping

MCP tools:

- `loomle`
- `context`
- `execute`
- `jobs`
- `editor.open`
- `editor.focus`
- `editor.screenshot`
- `graph`
- `graph.list`
- `graph.resolve`
- `graph.query`
- `graph.mutate`
- `graph.verify`
- `diagnostic.tail`
- `log.tail`

Execution route:

- `loomle`: native MCP tool served directly by `LoomleBridge`.
- `context`: native MCP `tools/call`.
- `execute`: native MCP `tools/call`.
- `execute` is intentionally the Unreal-Python fallback surface for non-graph operations and for graph domains/capabilities not yet exposed through structured `graph.*` tools.
- `execute` supports synchronous execution by default and shared long-running submission through `execution.mode = "job"`.
- `execute` remains available during `PIE`; `PIE` is treated as runtime context rather than as a blanket execute block.
- `jobs`: native MCP `tools/call`.
- `jobs` is the shared lifecycle surface for job-mode submissions. It owns status, logs, result lookup, and job listing.
- `jobs` remains available during `PIE` so long-running task lifecycle can still be observed while gameplay is active.
- `editor.open`: native MCP `tools/call`.
- `editor.focus`: native MCP `tools/call`.
- `editor.screenshot`: native MCP `tools/call`.
- `graph`: native MCP `tools/call`.
- `graph.list`: native MCP `tools/call`.
- `graph.resolve`: native MCP `tools/call`.
- `graph.query`: native MCP `tools/call`.
- `graph.mutate`: native MCP `tools/call`.
- `graph.verify`: native MCP `tools/call`.
- `diagnostic.tail`: native MCP `tools/call`.
- `log.tail`: native MCP `tools/call`.

Windows transport contention handling:

- Named-pipe open failures with OS error `231` are treated as transient and retried with bounded backoff (+ small jitter) before surfacing an error.
- Runtime RPC uses one stable pipe/socket session per attached endpoint. JSON-RPC ids correlate responses; current Unreal-side execution remains ordered per connection.

## 5. Determinism Rules

1. Request/response matching always uses `id`.
2. `graph.mutate` executes operations in order.
3. `dryRun=true` never persists state.
4. All timestamps use UTC RFC3339.

## 6. Runtime Performance Guardrail

For any Unreal project integrating LOOMLE Bridge, set:

`[/Script/UnrealEd.EditorPerformanceSettings] bThrottleCPUWhenNotForeground=False`

Source-of-truth location in the UE project:

- `Config/DefaultEditorSettings.ini`

Reason:

- If Unreal throttles CPU in background, bridge tail latency can increase significantly during agent-driven workflows.

## 7. MCP Server Packaging Contract

- Runtime serves MCP directly from `LoomleBridge` over the project-scoped endpoint.
- Runtime path policy:
  - Darwin client connects to `<ProjectRoot>/Intermediate/loomle.sock`
  - Windows client connects to `\\.\pipe\loomle-<fnv64(project_root)>`
  - `--project-root` remains required for endpoint discovery

## 8. Release Runtime Notes

- Release runtime is source-independent:
  - do not require `cargo`, source tree, or local Rust toolchain.
- Client/runtime launcher must provide direct MCP connectivity:
  - Darwin: connect to `<ProjectRoot>/Intermediate/loomle.sock`
  - Windows: connect to `\\.\pipe\loomle-<fnv64(project_root)>`
- Minimum liveness check sequence:
  - `initialize` -> `loomle` -> `context`
