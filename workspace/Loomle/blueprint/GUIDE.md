# Blueprint Guide

This is the primary entrypoint for Blueprint graph work inside the LOOMLE
workspace.

Use this file first. Open `SEMANTICS.md` when you need deeper guidance about
control flow, variable nodes, or node-family boundaries.

## Core Loop

1. Read the current graph with `graph.query`.
2. Decide whether you are changing exec flow, data flow, or both.
3. Use primitive `graph.mutate` operations to make a small local edit.
4. Run `layoutGraph(scope="touched")` when the structure is already correct and
   you want readability.
5. Run `graph.verify`.
6. Re-query when you need exact proof of node ids, pins, or edges.

## First Checks

- confirm the target Blueprint asset path
- confirm the exact graph name, such as `EventGraph`
- confirm the local rewrite boundary before mutating
- confirm which exec and data interfaces must stay stable

## Primary References

- `SEMANTICS.md`
- `catalogs/node-catalog.json`
- `examples/README.md`
- `examples/executable/branch-local-subgraph.json`
- `examples/executable/delay-local-chain.json`
- `examples/executable/sequence-local-fanout.json`
- `examples/illustrative/branch-then-layout.json`
- `examples/illustrative/set-variable-then-print.json`
- `examples/illustrative/sequence-fanout.json`
- `examples/illustrative/delay-then-print.json`
- `examples/illustrative/do-once-then-print.json`
- `examples/illustrative/replace-delay-with-do-once.json`
- `examples/illustrative/replace-branch-with-sequence.json`
- `examples/illustrative/insert-not-before-branch-condition.json`
- `examples/illustrative/insert-delay-on-true-branch.json`

## Execution Style

Prefer explicit primitive edits:

- `addNode.byClass`
- `connectPins`
- `disconnectPins`
- `setPinDefault`
- `removeNode`

Use real Blueprint node names and live graph readback when planning edits. Do
not introduce a second naming layer here.

## Example Contracts

Blueprint examples come in two types:

- `examples/executable/`
  Use these when you want a copy-runnable payload that only depends on new
  nodes created inside the same batch.
- `examples/illustrative/`
  Use these when you want a rewrite pattern against an existing graph. Re-query
  the target graph first and substitute live node ids or exact rewrite
  boundaries before running them.

## Validation Style

Minimum Blueprint validation should confirm:

- the new node exists
- the intended exec edge exists
- the intended data edge exists when relevant
- removed edges are actually gone
- preserved upstream and downstream interfaces still connect where intended
