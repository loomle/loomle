# Loomle Tests

## Layout

- `mcp_protocol/`: MCP protocol tests that do not depend on Unreal Editor.

## Notes

- These tests validate MCP tool contracts, MCP<->RPC mapping behavior, and deterministic request routing.
- Keep UE-independent tests in this folder so they can run in CI without launching Unreal.
