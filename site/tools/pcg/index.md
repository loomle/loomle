---
layout: default
title: PCG
parent: Tools
nav_order: 7
---

# PCG Tools

PCG tools operate on UE PCG graph assets.

- `pcg.palette`: find PCG node creation entries.
- `pcg.graph.inspect`: inspect graph nodes, pins, links, and defaults.
- `pcg.graph.edit`: apply explicit graph edits.
- `pcg.graph.layout`: format an explicit node selection.
- `pcg.node.inspect`: inspect node settings and editable properties.
- `pcg.parameter.inspect`: inspect graph user parameters.
- `pcg.parameter.edit`: edit graph user parameters.
- `pcg.compile`: validate and compile-confirm a graph after edits.

PCG graph parameters are separate from graph nodes. Use parameter tools for the
graph's user parameters, and graph/node tools for PCG nodes and settings.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `pcg.graph.inspect` | `assetPath` | `graph`, `view: overview/pins/links/defaults/full`, `filter.nodeIds`, `filter.text`, `page` |
| `pcg.palette` | none | `assetPath`, `graph`, `query`, `elementTypes`, `limit`, `offset` |
| `pcg.node.inspect` | none | Instance mode: `assetPath`, `graph`, `node.id`; class mode: `nodeClass` or `settingsClass` |
| `pcg.parameter.inspect` | none | `assetPath`, `graph`, optional exact `name` |
| `pcg.parameter.edit` | `assetPath`, `operation`, `args` | `operation: create/update/rename/delete/setDefault`; operation args through `schema.inspect` |
| `pcg.graph.edit` | `assetPath`, `commands` | `graph`, command envelopes with `kind`; command-specific args through `schema.inspect` |
| `pcg.graph.layout` | `assetPath`, selection | Formats explicit node selection only. |
| `pcg.compile` | `assetPath` | PCG graph asset path. |

## Recommended Flow

1. Inspect the graph with `pcg.graph.inspect`.
2. Use `pcg.palette` when adding a node.
3. Use `schema.inspect` before `pcg.graph.edit` commands.
4. Use `pcg.node.inspect` before editing node settings.
5. Use `pcg.parameter.inspect` before editing graph parameters.
6. Run `pcg.compile` after meaningful changes.

## Boundary

Graph edits own node creation, removal, movement, links, and pin defaults.

Node inspect explains settings on one node. Graph parameters are separate and
use `pcg.parameter.*`.
