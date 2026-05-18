---
layout: default
title: Blueprint Graphs
parent: Blueprint
nav_order: 2
---

# Blueprint Graphs

Use these tools for Blueprint graph work:

- `blueprint.graph.list`: list graphs in an asset.
- `blueprint.graph.inspect`: inspect graph nodes, links, and compact views.
- `blueprint.graph.edit`: apply explicit local graph edit commands.
- `blueprint.graph.layout`: format selected graph regions.

`blueprint.graph.inspect` defaults to a compact overview. Use richer views only
when needed for connection planning.

For `blueprint.graph.edit`, call `schema.inspect` for command-specific schemas.

## Inspect Views

- `overview`: compact node summaries. Use this first.
- `wiring`: adds compact pins and links for connection planning.

If a node has `hasNodeEditCapabilities: true`, inspect it with
`blueprint.node.inspect` before editing its local pins.

## Edit Boundary

Use `blueprint.graph.edit` for local graph mutations:

- add a node from a palette entry
- connect or disconnect explicit pins
- set a pin default
- remove or move a node
- edit a node comment or enabled state

Do not use graph edit for Blueprint member signatures. Use
`blueprint.member.edit` for variables, functions, macros, dispatchers, events,
and components.

## Layout

Use `blueprint.graph.layout` after edits when the graph region needs visual
cleanup. Layout changes positions only; it does not change Blueprint behavior.
