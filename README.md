# LOOMLE

LOOMLE brings Unreal Engine 5 runtime and graph context into AI-native workflows. It combines `LoomleBridge`, a standard MCP surface, and a project-local agent workspace so agents can collaborate with users directly inside real UE5 projects instead of working from partial context.

## Core Features

- Live UE5 context for AI: expose real-time editor, asset, selection, and runtime health data so agents act on facts, not snapshots.
- MCP-native integration: deliver LOOMLE through a clean, standard MCP surface that plugs directly into agent workflows.
- Graph-to-runtime collaboration: support graph inspection and mutation for Blueprint, Material, and PCG workflows with end-to-end project-local operation.

## Quick Start

LOOMLE is now organized around a single repository and a single installed project shape:

- MCP server shipped with `Plugins/LoomleBridge`
- project-local client and agent workspace shipped under `Loomle/`

For a source checkout, the current path is:

1. Build a local release:

```bash
python3 packaging/release/build_local_release.py \
  --repo-root /path/to/loomle \
  --output-dir /tmp/loomle-release \
  --platform darwin \
  --version 0.0.0-dev
```

2. Or build and install directly into a UE project:

```bash
python3 packaging/install/install_from_checkout.py \
  --repo-root /path/to/loomle \
  --project-root /path/to/MyProject \
  --output-dir /tmp/loomle-install-build \
  --platform darwin \
  --version 0.0.0-dev
```

See [docs/REPO_STRUCTURE.md](/Users/xartest/dev/loomle/docs/REPO_STRUCTURE.md) for the target repository, release, and installed-project structure.

Once setup is complete, you do not need to call MCP tools yourself. You can simply talk to Codex in natural language and ask it to do UE5 work for you through LOOMLE.

Example requests:

- `Look at the current Blueprint, explain why this logic is failing, and fix it.`
- `Inspect the selected material graph and clean up unnecessary nodes and connections.`
- `Create a Blueprint that opens a door when the player enters a trigger box.`

## Agent-Facing MCP Tools

LOOMLE currently exposes these MCP tools for the agent. These are the tools Codex uses behind the scenes; end users normally just work through natural-language requests.

- `loomle`: Check Bridge health, runtime status, and MCP-side availability.
- `context`: Read the current Unreal editor context, including asset and selection information.
- `execute`: Run UE-side code or commands through the Bridge.
- `graph`: Read graph capability metadata, supported operations, and runtime status.
- `graph.list`: List graphs available in the current target asset or context.
- `graph.query`: Inspect graph structure such as nodes, pins, and connections.
- `graph.actions`: Retrieve actionable graph operation candidates for the current graph context.
- `graph.mutate`: Apply graph changes through ordered mutation operations.

For protocol details and deeper technical documentation, see [docs/README.md](/Users/xartest/dev/loomle/docs/README.md).
