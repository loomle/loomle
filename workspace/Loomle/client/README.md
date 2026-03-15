# LOOMLE Client

This directory is the install target for the project-local LOOMLE client.

Target installed runtime:

```text
Loomle/client/loomle(.exe)
```

The client is the project-local entrypoint for agent and user workflows. It belongs to the workspace layer, not the Unreal plugin layer.

Preferred commands:

- `loomle doctor`
- `loomle list-tools`
- `loomle call <tool-name> --args '<json-object>'`

Advanced commands:

- `loomle install`
- `loomle server-path`
- `loomle run-server`
- `loomle session`

Guidance:

- Prefer `list-tools` over hardcoded tool assumptions.
- Prefer `call` over manually starting the MCP server.
- Use `run-server` and `session` only for debugging, protocol inspection, or custom integrations.

`loomle run-server` launches the MCP server installed under:

```text
Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)
```
