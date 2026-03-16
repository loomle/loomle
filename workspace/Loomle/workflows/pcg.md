# PCG Workflow

Recommended PCG editing rhythm for LOOMLE:

1. query the graph
2. use `graph.ops` and `graph.ops.resolve` when you want a semantic pipeline plan
3. build or extend a pipeline with ordered `graph.mutate` operations
4. call `layoutGraph(scope=\"touched\")`
5. verify the resulting pipeline with `graph.query`

Addressing rule:
- when you start from a selected PCG actor or component in the level, use `context` and then `graph.resolve` on the emitted actor or component path
- once LOOMLE returns a PCG `graphRef`, prefer using that `graphRef` for subsequent `graph.query` and `graph.mutate` calls

Planning rule:
- treat `graph.ops` and `graph.ops.resolve` as the default public planning surface for PCG graph edits
- prefer the current semantic v1 baseline: `pcg.create.points`, `pcg.meta.add_tag`, `pcg.filter.by_tag`, `pcg.sample.surface`
- `pcg.meta.add_tag` and `pcg.filter.by_tag` can emit immediate insertion `steps[]` when `context.fromPin` is supplied
- if you want `pcg.filter.by_tag` to resolve into an immediate insertion plan, pass `context.fromPin` so LOOMLE can generate `steps[]` with the initial `connectPins`
- expect useful PCG plans to carry `settingsTemplate`, `pinHints`, or `verificationHints`; read those fields before filling settings or validating results
- expect PCG plans to need follow-up readback because actual pin names and nested settings may matter more than node topology alone

Readback rule:
- prefer `graph.query` after every meaningful PCG edit, not just for topology but also for node `effectiveSettings` and node-level `diagnostics`
- expect `graph.query` to expose selector/spawner details for common runtime-sensitive nodes such as `Get Actor Property`, `Get Spline Data`, and `Static Mesh Spawner`
- when a PCG pipeline looks empty, inspect node `diagnostics` first; LOOMLE now emits empty-input hints for actor selectors, component selectors, and mesh-selector misconfiguration
- for selector-backed nodes, read the nested `actorSelector`, `componentSelector`, or `meshSelector` objects before assuming the runtime source is correct

Runtime inspection rule:
- when you need generated-result evidence after a regenerate, call `pcg.inspectRuntime`
- prefer passing `componentPath` from `graph.resolve(actorPath=...)`, `graph.resolve(componentPath=...)`, or `context.selection`
- treat `managedResources` as the authoritative runtime summary for spawned actors/components and instance counts
- expect `generatedGraphOutput` to be informative but not always complete for spawner-style graphs
- if `generatedGraphOutput` is empty while visible spawned results exist, trust `managedResources` and `inspection` instead of assuming generation failed

Layout expectation:
- source nodes appear on the left
- downstream processing stages move to the right
- parallel branches separate vertically
