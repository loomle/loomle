# Loomle MCP Server

Rust implementation of Loomle MCP protocol defined in:

- `../docs/MCP_PROTOCOL.md`
- `../docs/RPC_INTERFACE.md`
- `../docs/ARCHITECTURE.md`

## Current status

- MCP service routing implemented (`loomle`, `context`, `execute`, `graph`, `graph.list`, `graph.query`, `graph.actions`, `graph.mutate`).
- `loomle` and `graph` enforce per-call `rpc.health` probe.
- Runtime tools forward through `rpc.invoke`.
- RPC error code mapping to MCP domain codes implemented.
- Platform-aware RPC transport skeleton implemented:
  - Windows: Named Pipe
  - macOS/Linux: Unix Socket

## Run tests

```bash
cd mcp_server
cargo test
```

## Run server (stdio MCP)

```bash
cd mcp_server
LOOMLE_PROJECT_ROOT="/Users/xartest/Documents/UnrealProjects/Loomle" cargo run
```

- Transport endpoint is selected by platform:
  - Windows: `\\\\.\\pipe\\loomle`
  - macOS/Linux: `<Project>/Intermediate/loomle.sock`
