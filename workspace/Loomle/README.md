# LOOMLE Workspace

This directory is the installed project-local control surface for LOOMLE.

Start here:

1. Run `Loomle/client/loomle doctor` to confirm the project can see the plugin and MCP server.
2. Run `Loomle/client/loomle list-tools` to discover the live tool contract from the installed server.
3. Use the workflow file that matches the current graph type:
   - `workflows/blueprint.md`
   - `workflows/material.md`
   - `workflows/pcg.md`
4. Use `Loomle/client/loomle call <tool-name> --args '<json-object>'` for direct tool execution.

Installed shape:

```text
Loomle/
  README.md
  client/
  workflows/
  examples/
  runtime/
```

Directory roles:

- `client/`: the only supported project-local LOOMLE entrypoint
- `workflows/`: concise operating patterns for Blueprint, Material, and PCG work
- `examples/`: small concrete payload examples
- `runtime/`: machine-written install and health state; do not treat it as documentation

Design rule:

- Agent-facing usage starts from this file and the `loomle` client.
- Repository maintainer docs live under `docs/`; they are not part of the installed workspace contract.
