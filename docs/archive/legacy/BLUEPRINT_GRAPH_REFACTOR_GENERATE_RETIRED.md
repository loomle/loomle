# Blueprint Graph Refactor And Generate Retired Design

This note preserves the retired design direction for the former public
Blueprint graph transformation and generation tools:

- `blueprint.graph.refactor`
- `blueprint.graph.generate`
- `blueprint.graph.recipe.*`

These tools were removed from the public MCP surface because they introduced a
second semantic layer over `blueprint.graph.edit` without enough proven value.
The preferred public workflow is now:

1. Inspect the graph with `blueprint.graph.inspect`.
2. Discover node creation entries with `blueprint.palette` when needed.
3. Apply explicit local edits with `blueprint.graph.edit`.
4. Format the result with `blueprint.graph.layout` when useful.

## Retired Refactor Direction

The retired `blueprint.graph.refactor` design attempted to expose structural
transforms such as:

- `insertBetween`
- `replaceNode`
- `wrapWith`
- `fanoutExec`
- `cleanupReroutes`

The practical implementation reduced mostly to macros over graph edit commands
plus internal bulk rewiring helpers such as `rebindMatchingPins` and
`moveInputLinks`. Those helpers remain internal implementation details for now.

Public callers should not depend on automatic pin rebinding or implicit link
movement. They should inspect the current graph and issue explicit
`connect`, `disconnect`, `breakLinks`, `removeNode`, and `addFromPalette`
commands.

## Retired Generate Direction

The retired `blueprint.graph.generate` design attempted to execute built-in,
file, or inline graph recipes and optionally attach generated snippets to an
existing graph. The related `blueprint.graph.recipe.*` tools were intended to
list, inspect, validate, create, update, and delete reusable recipe
definitions.

This over-promised the implementation and overlapped with the clearer
`palette -> graph.edit` authoring path. Reusable examples may still live in
documentation or tests, but they should not be exposed as public graph
generation tools until the product has a stronger reason to promote them.
