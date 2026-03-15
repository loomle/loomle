# LOOMLE Client

This directory is the canonical home for the project-local Rust client.

Target role:
- provide the machine-level `loomle install` entrypoint used after bootstrap
- provide the single supported LOOMLE client entrypoint
- discover the current Unreal project root
- locate the MCP server installed under `Plugins/LoomleBridge/Tools/mcp/...`
- connect to the project-local MCP server/runtime
- live in the installed project under `Loomle/client/`

## Build

```bash
cd mcp/client
cargo build
```

## Commands

```bash
cd mcp/client
cargo run -- install --project-root "/Path/To/Project" --manifest-path "/Path/To/manifest.json"
cargo run -- doctor --project-root "/Path/To/Project"
cargo run -- server-path --project-root "/Path/To/Project"
cargo run -- list-tools --project-root "/Path/To/Project"
cargo run -- call context --project-root "/Path/To/Project"
cargo run -- session --project-root "/Path/To/Project"
```

If `--project-root` is omitted, the client searches upward from the current directory until it finds a `.uproject`.

## Session mode

`loomle session` keeps a single client process open and accepts one JSON request per line on stdin.

Minimal examples:

```json
{"id":1,"method":"tools/list"}
{"id":2,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

Each response is emitted as one JSON line on stdout with the same `id`.
