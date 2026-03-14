# Blueprint Workflow

Recommended Blueprint editing rhythm for LOOMLE:

1. query the graph
2. apply a small batch of `graph.mutate` operations
3. call `layoutGraph(scope=\"touched\")`
4. query again if verification is needed

Layout expectation:
- exec tree stays readable from left to right
- `True` / `False` branches fan out recursively
- global reflow should be explicit, not default
