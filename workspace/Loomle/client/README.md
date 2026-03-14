# LOOMLE Client

This directory is the install target for the project-local LOOMLE client.

Target installed runtime:

```text
Loomle/client/loomle(.exe)
```

The client is the project-local entrypoint for agent and user workflows. It belongs to the workspace layer, not the Unreal plugin layer.

Supported commands:

- `loomle doctor`
- `loomle server-path`
- `loomle run-server`

`loomle run-server` launches the MCP server installed under:

```text
Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)
```
