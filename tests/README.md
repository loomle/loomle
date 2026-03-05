# Loomle Tests

## Layout

- `../mcp_server/`: MCP protocol and routing tests (UE-independent, Rust).

## Notes

- MCP contract tests moved to `./Loomle/mcp_server` as the single source of truth.
- Keep Unreal-independent tests there so they can run in CI without launching Unreal Editor.
