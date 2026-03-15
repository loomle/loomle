# PCG Workflow

Recommended PCG editing rhythm for LOOMLE:

1. query the graph
2. build or extend a pipeline with ordered `graph.mutate` operations
3. call `layoutGraph(scope=\"touched\")`
4. verify the resulting pipeline with `graph.query`

Addressing rule:
- when you start from a selected PCG actor or component in the level, use `context` and then `graph.resolve` on the emitted actor or component path
- once LOOMLE returns a PCG `graphRef`, prefer using that `graphRef` for subsequent `graph.query` and `graph.mutate` calls

Layout expectation:
- source nodes appear on the left
- downstream processing stages move to the right
- parallel branches separate vertically
