---
layout: default
title: Blueprint
parent: Tools
nav_order: 1
has_children: true
---

# Blueprint Tools

Blueprint tools follow UE's Blueprint boundaries. Pick the tool by the thing you
are changing:

- Asset and class contract: `blueprint.inspect`, `blueprint.class.inspect`,
  `blueprint.class.edit`.
- Members: variables, functions, macros, dispatchers, custom events, and
  components use `blueprint.member.inspect` and `blueprint.member.edit`.
- Graphs: nodes, pins, links, defaults, comments, and layout use
  `blueprint.graph.*`.
- Node-local structure: switch cases, sequence pins, Select options, Format
  Text arguments, and struct field visibility use `blueprint.node.*`.
- Creation: graph node creation starts with `blueprint.palette`.
- Verification: compile with `blueprint.compile`.

Use `blueprint.palette` before graph node creation. Use
`blueprint.node.inspect` when `blueprint.graph.inspect` returns
`hasNodeEditCapabilities: true`.

## Recommended Flow

1. Inspect the asset or graph.
2. Use `blueprint.palette` when creating graph nodes.
3. Use `schema.inspect` when an edit tool has operation-specific arguments.
4. Apply one explicit edit.
5. Compile if behavior changed.
