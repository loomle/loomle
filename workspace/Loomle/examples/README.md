# LOOMLE Examples

This directory contains small project-local examples for:

- Blueprint graph editing
- Material graph editing
- PCG graph editing

Included examples:

- `blueprint/branch_then_layout.json`
- `blueprint/resolve_branch_from_exec.json`
- `material/root_sink_then_layout.json`
- `material/resolve_texture_sample_to_base_color.json`
- `pcg/pipeline_then_layout.json`
- `pcg/resolve_filter_by_tag_from_pin.json`

When to use them:

- open the Blueprint example when you want a minimal `graph.mutate` batch with node creation, pin wiring, and `layoutGraph(scope="touched")`
- open the Blueprint resolve example when you want `graph.ops.resolve` to produce a pin-context-aware `steps[]` plan
- open the Material example when you want a root-sink expression chain that terminates at `__material_root__`
- open the Material resolve example when you want `graph.ops.resolve` to plan a root-targeted semantic node with `targetRootPin`
- open the PCG example when you want a small left-to-right pipeline batch
- open the PCG resolve example when you want `graph.ops.resolve` to produce an insertion plan plus PCG-specific plan metadata

These examples are starting points. Before reusing them against a real graph, prefer:
- `context` and `graph.resolve` when you are starting from the current editor selection
- `graph.query` to confirm current node ids, pin names, and graph refs
- `graph.ops` and `graph.ops.resolve` when you want a semantic plan before building a mutate batch
- the workflow guide for the current graph type when the graph has subgraphs or graph-specific addressing rules

Examples here should teach usage patterns, not repository internals.
