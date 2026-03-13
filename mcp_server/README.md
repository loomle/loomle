# LOOMLE MCP Server

Rust implementation of LOOMLE MCP protocol defined in:

- `../docs/MCP_PROTOCOL.md`
- `../docs/RPC_INTERFACE.md`
- `../docs/ARCHITECTURE.md`

## Current status

- MCP service routing implemented (`loomle`, `context`, `execute`, `graph`, `graph.list`, `graph.query`, `graph.actions`, `graph.mutate`, `diag.tail`).
- `loomle` and `graph` enforce per-call `rpc.health` probe.
- Runtime tools perform runtime preflight (`rpc.health`) with a shared short TTL cache (`200ms`), then forward through `rpc.invoke`.
- Runtime tools fail fast with `EDITOR_BUSY` during PIE and skip `rpc.invoke`.
- `tools/list` is the supported schema discovery surface for current tool argument contracts.
- RPC error code mapping to MCP domain codes implemented.
- Platform-aware RPC transport skeleton implemented:
  - Windows: Named Pipe (includes bounded retry/backoff for busy open error `231`)
  - macOS/Linux: Unix Socket

## Run tests

```bash
cd mcp_server
cargo test
```

## Run server (stdio MCP)

```bash
cd mcp_server
cargo run -- --project-root "/Users/xartest/Documents/UnrealProjects/Loomle"
```

- `--project-root` is required.
- The directory must contain at least one `.uproject` file.
- Transport endpoint is selected by platform:
  - Windows: `\\\\.\\pipe\\loomle-<fnv64(project_root)>`
  - macOS/Linux: `<Project>/Intermediate/loomle.sock`
