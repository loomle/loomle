# LOOMLE Client

This directory is the canonical home for the global Rust `loomle` command.

## Role

- `loomle mcp`: stdio MCP server for Codex, Claude, and other MCP hosts
- `loomle update`: update the global install under `~/.loomle`
- `loomle doctor`: inspect global install state and print MCP configuration hints

The client is not installed into Unreal projects. Project support is installed
by copying `LoomleBridge` into `<ProjectRoot>/Plugins/LoomleBridge/`.

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
