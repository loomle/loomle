# Blueprint Examples

Blueprint examples are split into two contracts:

- `executable/`
  These payloads are designed to run directly against a live Blueprint graph
  without first substituting existing node ids from `graph.query`.
- `illustrative/`
  These payloads teach a graph-editing pattern, but they assume you will
  re-query the target graph and substitute live node ids, pin names, or local
  rewrite boundaries before execution.

Working rule:
- use `executable/` when you want a copy-runnable mutate batch
- use `illustrative/` when you want a rewrite pattern to adapt to current graph
  reality
