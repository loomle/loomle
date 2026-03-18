# Material Guide

This is the primary entrypoint for Material graph work inside the LOOMLE
workspace.

Use this file first. Open `SEMANTICS.md` when you need deeper guidance about
root sinks, function subgraphs, or node-family boundaries.

## Core Loop

1. Read the current graph with `graph.query`.
2. If `childGraphRef` appears on `MaterialFunctionCall`, decide whether you are
   editing the root Material graph or a referenced function graph.
3. Use primitive `graph.mutate` operations to make a small local edit.
4. Connect terminal expressions into `__material_root__` when the edit targets
   a root property.
5. Run `layoutGraph(scope="touched")` when the structure is already correct and
   you want readability.
6. Run `graph.verify`.
7. Re-query when you need exact proof of node ids, pins, edges, or child refs.

## First Checks

- confirm the target Material asset path
- confirm whether the edit belongs in the root graph or a function subgraph
- confirm the target root property, if any
- confirm the local expression-chain boundary before mutating

## Primary References

- `SEMANTICS.md`
- `examples/root-sink-then-layout.json`
- `examples/texture-sample-to-base-color.json`
- `examples/scalar-to-roughness.json`
- `examples/scalar-one-minus-to-roughness.json`
- `examples/texture-times-scalar-to-base-color.json`

## Execution Style

Prefer explicit primitive edits:

- `addNode.byClass`
- `connectPins`
- `disconnectPins`
- `setPinDefault`
- `removeNode`

Use real Material node names and live graph readback when planning edits. Do
not introduce a second naming layer here.

## Validation Style

Minimum Material validation should confirm:

- the new node exists
- the intended expression edge exists
- the intended root sink edge exists when relevant
- removed edges are actually gone
- referenced function graphs still resolve as intended
