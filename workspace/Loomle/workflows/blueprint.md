# Blueprint Workflow

Recommended Blueprint editing rhythm for LOOMLE:

1. query the graph
2. use `graph.ops` and `graph.ops.resolve` when you need semantic node planning
3. apply a small batch of `graph.mutate` operations
4. call `layoutGraph(scope=\"touched\")`
5. query again if verification is needed

When creating nodes:
- prefer `graph.ops` to discover stable semantic operations for Blueprint
- prefer `graph.ops.resolve` to map those semantic ops into mutate-ready plans
- use `addNode.byClass` directly when you already know the exact class path and do not need semantic planning

Planning rule:
- `core.comment`, `core.reroute`, and `bp.flow.branch` are the current Blueprint semantic v1 baseline
- use `graph.ops.resolve` with `context.fromPin` when a semantic op depends on pin context, such as `core.reroute`
- `core.reroute` and `bp.flow.branch` can now emit `steps[]` when `context.fromPin` is supplied
- when `preferredPlan.steps[]` is returned, prefer executing that batch shape instead of reconstructing the node and wire sequence by hand
- prefer plans that resolve to `addNode.byClass`

Layout expectation:
- exec tree stays readable from left to right
- `True` / `False` branches fan out recursively
- global reflow should be explicit, not default
