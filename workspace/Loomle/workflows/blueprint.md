# Blueprint Workflow

Recommended Blueprint editing rhythm for LOOMLE:

1. query the graph
2. apply a small batch of `graph.mutate` operations
3. call `layoutGraph(scope=\"touched\")`
4. query again if verification is needed

When creating nodes:
- prefer `graph.actions` followed by `graph.mutate op="addNode.byAction"` when you want editor-native addable actions
- use `addNode.byClass` when you need deterministic construction from a known class path

`actionToken` rules:
- treat it as opaque and short-lived
- use it only on the exact Blueprint graph that returned it
- do not cache it as a stable identifier across repeated `graph.actions` calls
- if `addNode.byAction` reports an action-token error, call `graph.actions` again on that same graph and retry with a fresh token

Layout expectation:
- exec tree stays readable from left to right
- `True` / `False` branches fan out recursively
- global reflow should be explicit, not default
