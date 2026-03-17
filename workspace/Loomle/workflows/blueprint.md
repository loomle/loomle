# Blueprint Workflow

Recommended Blueprint editing rhythm for LOOMLE:

1. query the graph
2. use `graph.ops` and `graph.ops.resolve` when you need semantic node planning
3. apply a small batch of `graph.mutate` operations
4. call `layoutGraph(scope=\"touched\")`
5. call `graph.verify`
6. query again if you need a fresh semantic snapshot after verification

When creating nodes:
- prefer `graph.ops` to discover stable semantic operations for Blueprint
- prefer `graph.ops.resolve` to map those semantic ops into mutate-ready plans
- use `addNode.byClass` directly when you already know the exact class path and do not need semantic planning

Planning rule:
- `core.comment`, `core.reroute`, `bp.flow.branch`, `bp.flow.sequence`, `bp.flow.delay`, `bp.flow.do_once`, `bp.debug.print_string`, `bp.var.get`, and `bp.var.set` are the current Blueprint semantic v1 baseline
- use `graph.ops.resolve` with `context.fromPin` when a semantic op depends on pin context, such as `core.reroute`
- `core.reroute`, `bp.flow.branch`, `bp.flow.sequence`, `bp.flow.delay`, `bp.debug.print_string`, and `bp.var.set` can emit `steps[]` when `context.fromPin` is supplied
- `bp.var.get` and `bp.var.set` need `items[*].hints.variableName`; if that hint is missing, `graph.ops.resolve` should return structured remediation instead of a fake plan
- `bp.flow.do_once` resolves to the standard `DoOnce` macro instance, not a synthetic custom node
- when `preferredPlan.steps[]` is returned, prefer executing that batch shape instead of reconstructing the node and wire sequence by hand
- prefer plans that resolve to `addNode.byClass`

Layout expectation:
- exec tree stays readable from left to right
- `True` / `False` branches fan out recursively
- global reflow should be explicit, not default
