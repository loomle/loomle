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

Release bundles, global plugin caches, and copied project plugins must not
preserve `Binaries/<Platform>/UnrealEditor.modules`. That file is UE-generated
module metadata tied to one editor target BuildId. `loomle update` deletes stale
copies so Unreal Editor or UBT can regenerate metadata for the current project
and editor build.

Project registration is persistent under `~/.loomle/state/projects`. Runtime
records under `~/.loomle/state/runtimes` only indicate currently running Bridge
instances.

## Tool Schema Exposure

The shared manifest remains the source of truth for input, output, and operation
schemas. MCP `tools/list` intentionally exposes only tool names, descriptions,
titles, and thin input schemas so the default agent context stays small. Full
schemas stay in `mcp/manifest/manifest.json` and are retrieved on demand with
`schema.inspect` using `include: ["input"]`, `["output"]`, or `["operation"]`.

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
