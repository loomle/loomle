# Loomle Design (vNext)

## Scope

This folder defines the clean-room protocol design for Loomle vNext.

- No legacy compatibility constraints.
- No C++ internal implementation details.
- C++ side is specified only as an RPC boundary contract.
- Naming style uses the minimal tool names selected by project owner.

## Canonical Terms

- `MCP Server`: standalone Rust process exposing standard MCP.
- `MCP Client`: Codex or any MCP client.
- `RPC Listener`: Unreal-side endpoint that accepts internal RPC calls.
- `RPC Connector`: Rust-side endpoint that dials and calls RPC Listener.

## Source of Truth

1. `MCP_PROTOCOL.md`
- MCP-facing tool surface and schemas.

2. `RPC_INTERFACE.md`
- Internal RPC interface exposed by C++ (boundary only).

3. `ARCHITECTURE.md`
- Layer boundaries and call flow.

## Read Order

1. `ARCHITECTURE.md`
2. `RPC_INTERFACE.md`
3. `MCP_PROTOCOL.md`
