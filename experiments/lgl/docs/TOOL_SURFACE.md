# LGL Tool Surface Draft

## Intent

Loomle tools should wrap the LGL SDK rather than expose parser internals or
backend graph edit command shapes. The agent-facing surface should be small and
domain-oriented.

## Blueprint Tools

Initial Blueprint-facing tools:

```txt
blueprint_graph_query
blueprint_graph_patch
blueprint_compile
```

These tools are thin wrappers around the SDK plus the Blueprint adapter.

## Tool Responsibilities

`blueprint_graph_query`

- accepts self-describing `query blueprint("/Game/BP_Door"/EventGraph)` text
- returns LGL result documents or snippets
- covers node search, paths, surrounding context, palette discovery, full
  snapshot cache reads, and detailed node output
- is the primary agent read surface

`blueprint_graph_patch`

- accepts self-describing `patch blueprint("/Game/BP_Door"/EventGraph)` text
- parses the patch before any mutation
- resolves palette bindings
- validates pins, aliases, and layout moves
- returns diagnostics and computed changes
- applies the mutation through the Blueprint adapter
- supports `dry run` documents
- returns created alias/id mappings and updated LGL snippets when useful

`blueprint_compile`

- remains separate because compile is an asset validation/action boundary, not a
  graph text operation.

## Backend Tools

Existing tools such as `blueprint_graph_palette`, `blueprint_graph_edit`, and
node inspect behavior may remain available for debugging or advanced usage, but
LGL mode should treat them as adapter backend capabilities.

Full graph snapshots are still useful as internal cache primitives, but they
should be requested through empty-body `query` documents rather than a separate
agent-facing snapshot tool. Large graph snapshots should be written to
cache/workspace files and referenced by path instead of returned inline.

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
  "text": "graph blueprint(\"/Game/BP_Door\"/EventGraph)\n\nprint@C2B0: PrintString({InString: \"Ready\"})\n",
  "diagnostics": []
}
```

Diagnostics should include source spans and actionable suggestions.
