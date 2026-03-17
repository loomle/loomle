# PCG Workflow

Recommended PCG editing rhythm for LOOMLE:

1. query the graph
2. use `graph.ops` and `graph.ops.resolve` when you want a semantic pipeline plan
3. build or extend a pipeline with ordered `graph.mutate` operations
4. call `layoutGraph(scope=\"touched\")`
5. verify the resulting pipeline with `graph.query` and `graph.verify`

Addressing rule:
- when you start from a selected PCG actor or component in the level, use `context` and then `graph.resolve` on the emitted actor or component path
- once LOOMLE returns a PCG `graphRef`, prefer using that `graphRef` for subsequent `graph.query` and `graph.mutate` calls

Planning rule:
- treat `graph.ops` and `graph.ops.resolve` as the default public planning surface for PCG graph edits
- prefer the current semantic baseline: `pcg.create.points`, `pcg.meta.add_tag`, `pcg.filter.by_tag`, `pcg.sample.surface`, `pcg.transform.points`, `pcg.sample.spline`, `pcg.source.actor_data`, `pcg.spawn.static_mesh`
- `pcg.meta.add_tag` and `pcg.filter.by_tag` can emit immediate insertion `steps[]` when `context.fromPin` is supplied
- if you want `pcg.filter.by_tag` to resolve into an immediate insertion plan, pass `context.fromPin` so LOOMLE can generate `steps[]` with the initial `connectPins`
- expect useful PCG plans to carry `settingsTemplate`, `pinHints`, or `verificationHints`; read those fields before filling settings or validating results
- expect PCG plans to need follow-up readback because actual pin names and nested settings may matter more than node topology alone

Readback rule:
- prefer `graph.query` after every meaningful PCG edit, not just for topology but also for node `effectiveSettings` and node-level `diagnostics`
- expect `graph.query` to expose selector/spawner details for common runtime-sensitive nodes such as `Get Actor Property`, `Get Spline Data`, and `Static Mesh Spawner`
- after disconnecting an overridable PCG input pin, you can use `setPinDefault` on that input to write the node setting directly; prefer readback plus `graph.verify` to confirm the resulting graph state
- when a PCG pipeline looks empty, inspect node `diagnostics` first; LOOMLE now emits empty-input hints for actor selectors, component selectors, and mesh-selector misconfiguration
- for selector-backed nodes, read the nested `actorSelector`, `componentSelector`, or `meshSelector` objects before assuming the runtime source is correct

Observability contract:
- expect every PCG edge in `graph.query.semanticSnapshot.edges[]` to carry stable endpoint metadata: `fromNodeId`, `fromPin`, `toNodeId`, and `toPin`
- expect PCG subgraph nodes to expose `childGraphRef`; if the referenced graph asset cannot be resolved, inspect `childLoadStatus`
- expect runtime-sensitive PCG nodes to expose meaningful nested settings in `effectiveSettings`, especially selector-backed fields such as `actorSelector`, `componentSelector`, and `meshSelector`
- treat node-level `diagnostics` as the first explanation surface for empty-input and selector misconfiguration cases before reaching for Python fallback logic

Projection, filter, and spawn pattern:
- for landscape projection flows, read and write projection settings under `effectiveSettings.projectionParams`
- important projection fields such as `attributeMode`, `attributeList`, and `attributeMergeOperation` live inside `projectionParams`, not at the top level
- when you plan a landscape layer filter after projection, make sure the projection step keeps the metadata you need; otherwise later `Filter By Tag` / layer-weight filters may appear broken even though the missing metadata is upstream
- `Filter By Tag` has two meaningful output pins:
  - `InsideFilter` is the primary “matched” path
  - `OutsideFilter` is the unmatched branch
- for mesh spawning, inspect `effectiveSettings.meshSelector` and related diagnostics before assuming the spawner is reading the intended attribute

End-to-end pattern: projection -> filter -> static mesh spawn:
1. start from the actual PCG graph and call `graph.ops.resolve` for semantic nodes such as `pcg.project.surface`, `pcg.filter.by_tag`, and `pcg.spawn.static_mesh`
2. read the returned `preferredPlan.settingsTemplate` before mutating; for projection this is where nested `projectionParams` fields are surfaced, and for spawning this is where mesh-selector defaults appear
3. apply the returned plan with `graph.mutate`, then run `layoutGraph(scope="touched")`
4. verify the resulting topology with `graph.query`
5. confirm node configuration with `effectiveSettings` and node `diagnostics`
6. if you regenerated the PCG component in the level, debug that execution in the scene/editor; `graph.verify` stays graph-scoped

Troubleshooting:
- filter produces zero points:
  - inspect node `diagnostics`
  - confirm the upstream projection kept the metadata you expect
  - confirm you are wiring from `InsideFilter` when you want the matched branch
- volume appears to target the wrong landscape layer:
  - re-read `projectionParams`
  - check whether metadata inclusion/exclusion is stripping the layer fields you want to filter on
  - verify the downstream selector/filter node is still reading the intended attribute names

Layout expectation:
- source nodes appear on the left
- downstream processing stages move to the right
- parallel branches separate vertically
