# PCG Guide

This is the primary entrypoint for PCG graph work inside the LOOMLE workspace.

Use this file first. Open `SEMANTICS.md` when you need deeper guidance about
node-family boundaries, route vs filter behavior, or key parameters and wiring.

## Core Loop

1. Read the current graph with `graph.query`.
2. When you start from a selected PCG actor or component, use `context` and
   then `graph.resolve` to get a reusable PCG `graphRef`.
3. Use primitive `graph.mutate` operations to make a small local edit.
4. Run `layoutGraph(scope="touched")` when the structure is already correct and
   you want readability.
5. Re-read with `graph.query`.
6. Run `graph.verify` when you want compile-backed confirmation.

## First Checks

- confirm the target PCG graph asset or resolved graph ref
- confirm the local pipeline boundary before mutating
- confirm whether the task is about whole-data routing, element filtering,
  sampling, metadata, spawning, or structure

## Primary References

- `SEMANTICS.md`
- `catalogs/node-index.json`
- `catalogs/node-database.json`
- `examples/pipeline-then-layout.json`
- `examples/actor-data-tag-route.json`
- `examples/surface-sample-to-static-mesh.json`
- `examples/attribute-filter-elements.json`
- `examples/points-ratio-to-tag.json`
- `examples/project-surface-from-actor-data.json`
- `examples/replace-tag-with-points-ratio.json`
- `examples/replace-tag-route-with-attribute-route.json`
- `examples/insert-density-filter-before-static-mesh.json`
- `examples/insert-points-ratio-on-inside-filter.json`

## Execution Style

Prefer explicit primitive edits:

- `addNode.byClass`
- `connectPins`
- `disconnectPins`
- `setPinDefault`
- `removeNode`

Use real PCG node names, the PCG semantics guide, and live graph readback when
planning edits. Do not introduce a second naming layer here.

## Validation Style

Minimum PCG validation should confirm:

- the new node exists
- the intended edge exists
- the intended settings wrote through readback
- removed edges are actually gone
- diagnostics and verify output are acceptable
