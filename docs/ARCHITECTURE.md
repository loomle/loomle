# LOOMLE Bridge Architecture

## 1. Objective

Build a clean split where:

- MCP server is implemented in Rust.
- Unreal plugin exposes internal RPC interfaces.
- MCP server and Unreal runtime communicate through RPC Connector <-> RPC Listener.

## 2. Topology

1. MCP Client
- Sends MCP requests.

2. LOOMLE MCP Server (Rust)
- Implements MCP lifecycle and tool contracts.
- Validates tool inputs/outputs.
- Handles MCP-only descriptor/status tools locally.
- Uses RPC Connector for runtime-dependent tools.

3. RPC Connector (Rust)
- Owns connection lifecycle.
- Sends RPC requests.
- Routes responses by request id.

4. RPC Listener (C++)
- Owns listen/accept lifecycle.
- Validates RPC envelopes.
- Dispatches to runtime handlers.

5. Unreal Runtime Handlers (C++)
- Execute domain operations and return deterministic results.

## 3. Boundary Policy

1. MCP boundary
- Standard MCP only.
- No Unreal transport concepts leak outward.

2. RPC boundary
- JSON-RPC 2.0 + NDJSON framing.
- Windows transport: project-scoped Named Pipe derived from normalized `project_root`.
- macOS/Linux transport: Unix Socket.

3. C++ design scope
- This design specifies RPC method contracts only.
- Internal classes/functions in C++ are out of scope.

## 4. Minimal Name Mapping

MCP tools:

- `loomle`
- `context`
- `execute`
- `graph`
- `graph.list`
- `graph.ops`
- `graph.ops.resolve`
- `graph.query`
- `graph.mutate`
- `diag.tail`

Execution route:

- `loomle`: MCP local response + required `rpc.health` probe on every call.
- `context`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=context`).
- `execute`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=execute`).
- `execute` is intentionally the Unreal-Python fallback surface for non-graph operations and for graph domains/capabilities not yet exposed through structured `graph.*` tools.
- `graph`: MCP local descriptor response + required `rpc.health` probe on every call.
- `graph.list`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=graph.list`).
- `graph.ops`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=graph.ops`).
- `graph.ops.resolve`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=graph.ops.resolve`).
- `graph.query`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=graph.query`).
- `graph.mutate`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=graph.mutate`).
- `diag.tail`: runtime preflight (`rpc.health`, shared short TTL cache) + RPC `rpc.invoke` (`tool=diag.tail`).

Windows transport contention handling:

- Named-pipe open failures with OS error `231` are treated as transient and retried with bounded backoff (+ small jitter) before surfacing an error.

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

- Release packages embed MCP server binary under plugin path:
  - `LoomleBridge/Tools/mcp/darwin/loomle_mcp_server`
  - `LoomleBridge/Tools/mcp/linux/loomle_mcp_server`
  - `LoomleBridge/Tools/mcp/windows/loomle_mcp_server.exe`
- Runtime client config uses:
  - `command=<ProjectRoot>/Plugins/LoomleBridge/<server_binary_relpath>`
  - `args=["--project-root","<ProjectRoot>"]`
- Runtime path policy:
  - `--project-root` is required for server startup.
  - Server does not auto-detect project root from current working directory.

## 8. Release Runtime Notes

- Release runtime is source-independent:
  - do not require `cargo`, source tree, or local Rust toolchain.
- Client/runtime launcher must provide:
  - `command=<ProjectRoot>/Plugins/LoomleBridge/<server_binary_relpath>`
  - `args=["--project-root","<ProjectRoot>"]`
- Minimum liveness check sequence:
  - `initialize` -> `loomle` -> `context`
