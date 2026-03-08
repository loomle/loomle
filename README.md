# LOOMLE

LOOMLE brings Unreal Engine 5 runtime and graph context into AI-native workflows. It combines `LoomleBridge`, a standard MCP surface, and a companion Loomle Skill so agents can collaborate with users directly inside real UE5 projects instead of working from partial context.

## Core Features

- Real UE5 runtime context for AI: expose live editor, asset, selection, and runtime health data instead of relying on static guesses.
- Standard MCP integration: present LOOMLE capabilities through a clean MCP tool surface that works naturally with agent workflows.
- Graph-aware collaboration: support graph discovery, inspection, action planning, and mutation for UE graph-driven workflows.
- End-to-end automation with Loomle Skill: install, configure, upgrade, verify, and operate LOOMLE from inside a project-local Codex session.
- Built for practical UE development: help users and agents work together on Blueprint, Material, PCG, and related editor tasks with the right runtime context available.

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
