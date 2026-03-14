# PCG Workflow

Recommended PCG editing rhythm for LOOMLE:

1. query the graph
2. build or extend a pipeline with ordered `graph.mutate` operations
3. call `layoutGraph(scope=\"touched\")`
4. verify the resulting pipeline with `graph.query`

Layout expectation:
- source nodes appear on the left
- downstream processing stages move to the right
- parallel branches separate vertically
