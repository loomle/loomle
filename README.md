# LOOMLE

LOOMLE brings Unreal Engine 5 runtime and graph context into AI-native workflows. It combines `LoomleBridge`, a standard MCP surface, and a companion Loomle Skill so agents can collaborate with users directly inside real UE5 projects instead of working from partial context.

## Core Features

- Live UE5 context for AI: expose real-time editor, asset, selection, and runtime health data so agents act on facts, not snapshots.
- MCP-native integration: deliver LOOMLE through a clean, standard MCP surface that plugs directly into agent workflows.
- Graph-to-runtime collaboration: support graph inspection and mutation for Blueprint, Material, and PCG workflows with end-to-end project-local operation via Loomle Skill.

## Quick Start

The fastest way to start with LOOMLE is from the root of your local UE5 project.

1. Open Codex in your UE5 project root.
2. Tell Codex:

```text
install Loomle Skill from loomle.ai/i
```

That flow installs or updates the Loomle Skill, sets up `LoomleBridge`, applies the required project configuration, starts the service path, and runs the basic verification checks so the MCP connection is ready to use.

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
