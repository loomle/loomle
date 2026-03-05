# Loomle MCP Server (vNext)

Rust implementation of Loomle MCP protocol defined in:

- `../design/MCP_PROTOCOL.md`
- `../design/RPC_INTERFACE.md`
- `../design/ARCHITECTURE.md`

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
cd ./Loomle/mcp_server
cargo test
```

## Run server (stdio MCP)

```bash
cd ./Loomle/mcp_server
LOOMLE_PROJECT_ROOT="/Users/xartest/Documents/UnrealProjects/Loomle" cargo run
```

- Transport endpoint is selected by platform:
  - Windows: `\\\\.\\pipe\\loomle`
  - macOS/Linux: `<Project>/Intermediate/loomle.sock`
