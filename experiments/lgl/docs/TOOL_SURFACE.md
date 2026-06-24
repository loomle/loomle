# LGL Tool Surface

## Intent

Loomle tools should wrap the LGL SDK rather than expose parser internals or
backend graph edit command shapes. The agent-facing surface should be small and
domain-oriented.

This is a future MCP/CLI surface. The current experiment implements the
TypeScript SDK facade, not these tools.

## Blueprint Tools

Initial Blueprint tools:

```txt
blueprint_graph_query
blueprint_graph_patch
blueprint_compile
```

These tools are thin wrappers around the SDK plus the Blueprint adapter.

## Tool Responsibilities

### `blueprint_graph_query`

Primary read surface. Accepts self-describing LGL query text and returns LGL
result documents or snippets with diagnostics.

### `blueprint_graph_patch`

Primary mutation surface. Accepts self-describing LGL patch text, resolves and
validates the whole patch, supports dry run, and returns diagnostics plus
updated LGL snippets when useful.

### `blueprint_compile`

Remains separate because compile is an asset validation/action boundary, not a
graph text operation.

## Backend Tools

Existing graph palette/edit/inspect tools may remain available for debugging or
advanced usage, but LGL mode should treat them as adapter backend capabilities.

Full graph snapshots are internal cache primitives. Request them through query
documents rather than a separate agent-facing snapshot tool. Large snapshots
should be written to cache/workspace files and referenced by path.

Normal workflow:

```txt
blueprint_graph_query
blueprint_graph_patch
blueprint_compile
```

## Result Format

Graph content should be returned as LGL text with diagnostics:

```json
{
  "text": "bp = asset(path: \"/Game/BP_Door.BP_Door\", type: blueprint)\ng = graph(domain: blueprint, asset: bp, graph: EventGraph)\nprint = node(graph: g, type: PrintString, id: \"C2B0\", InString: \"Ready\")\n",
  "diagnostics": []
}
```

Diagnostics should include source spans and actionable suggestions.
