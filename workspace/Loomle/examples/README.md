# LOOMLE Examples

This directory contains small project-local examples for:

- Blueprint graph editing
- Material graph editing
- PCG graph editing

Included examples:

- `blueprint/branch_then_layout.json`
- `material/root_sink_then_layout.json`
- `pcg/pipeline_then_layout.json`

When to use them:

- open the Blueprint example when you want a minimal `graph.mutate` batch with node creation, pin wiring, and `layoutGraph(scope="touched")`
- open the Material example when you want a root-sink expression chain that terminates at `__material_root__`
- open the PCG example when you want a small left-to-right pipeline batch

These examples are starting points. Before reusing them against a real graph, prefer:
- `context` and `graph.resolve` when you are starting from the current editor selection
- `graph.query` to confirm current node ids, pin names, and graph refs
- the workflow guide for the current graph type when the graph has subgraphs or graph-specific addressing rules

Examples here should teach usage patterns, not repository internals.
