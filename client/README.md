# LOOMLE Client

This directory is the canonical home for the global Rust `loomle` command.

## Role

- `loomle mcp`: stdio MCP server for Codex, Claude, and other MCP hosts
- `loomle update`: update the global install under `~/.loomle`, then sync
  registered offline Unreal projects from the active plugin cache
- `loomle doctor`: inspect global install state and print MCP configuration hints

The client is not installed into Unreal projects. Project support is installed
by copying `LoomleBridge` into `<ProjectRoot>/Plugins/LoomleBridge/`.
`loomle update` performs that copy automatically for registered projects that
are not currently online in Unreal Editor. Online projects are skipped so the
loaded plugin is not overwritten; close Unreal Editor and run `loomle update`
again to sync them.

When a copied project plugin already contains `Binaries/<Platform>/UnrealEditor.modules`,
the client reconciles that manifest `BuildId` against the active editor
manifest at `Engine/Binaries/<Platform>/UnrealEditor.modules`. This matches
Unreal's module loading check for installed builds and avoids carrying a stale
release/cache `BuildId` into a project-local plugin.

Project registration is persistent under `~/.loomle/state/projects`. Runtime
records under `~/.loomle/state/runtimes` only indicate currently running Bridge
instances.

## Tool Schema Exposure

The shared manifest remains the source of truth for both input and output
schemas. MCP `tools/list` intentionally exposes only tool names, descriptions,
titles, and input schemas so the default agent context stays small. Full output
schemas stay in `mcp/manifest/manifest.json` and are retrieved on demand with
`schema.inspect` using `include: ["output"]`.

## Build

```bash
cd client
cargo build
```

## Run MCP From A Checkout

```bash
cd client
cargo run -- mcp
```

The MCP session starts unattached. Use `project.list` and `project.attach`
after Unreal Editor has started with `LoomleBridge`.

## Development Validation

From the repository root:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject
```

That command builds the checkout client, syncs the plugin into the dev project,
starts Unreal Editor, and validates through the checkout-built binary.
