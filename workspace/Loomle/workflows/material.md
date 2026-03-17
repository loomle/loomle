# Material Workflow

Recommended Material editing rhythm for LOOMLE:

1. query the material graph
2. if you see `childGraphRef` on `MaterialFunctionCall` nodes, follow those refs or run `graph.list(includeSubgraphs=true)` to inspect referenced `UMaterialFunction` graphs
3. use `graph.ops` and `graph.ops.resolve` when you want a semantic plan for new expression nodes
4. create and connect expression chains
5. connect terminal expressions into `__material_root__`
6. call `layoutGraph(scope=\"touched\")`
7. call `graph.verify`

Addressing rule:
- if you already have a Material or MaterialFunction asset path, prefer resolving or querying that asset directly
- if `graph.query` returns `childGraphRef`, treat it as the preferred handle for the referenced function subgraph instead of rebuilding the address manually

Planning rule:
- treat `graph.ops` and `graph.ops.resolve` as the default public planning surface
- prefer `mat.constant.scalar`, `mat.constant.vector3`, `mat.math.add`, `mat.math.lerp`, `mat.math.multiply`, `mat.math.one_minus`, `mat.math.saturate`, `mat.param.scalar`, `mat.param.texture`, `mat.param.vector`, `mat.texture.sample`, and `mat.func.call` as the current Material semantic v1 baseline
- if you want `graph.ops.resolve` to emit an immediate root-sink connection plan for `mat.math.add`, `mat.math.lerp`, `mat.math.multiply`, `mat.math.one_minus`, or `mat.math.saturate`, pass `items[*].hints.targetRootPin`
- if you want `mat.texture.sample` or `mat.param.texture` to emit `steps[]`, pass `items[*].hints.targetRootPin`; if you also pass `context.fromPin`, LOOMLE may wire that source into `UVs`
- `mat.func.call` commonly needs `items[*].hints.functionAssetPath`; use the returned `settingsTemplate` as the minimum required shape before wiring follow-up pins
- when a plan includes `pinHints`, use them to decide follow-up wiring rather than guessing unnamed output pins
- use `graph.mutate` to apply the returned plan and then wire the resulting nodes into the target sink

Layout expectation:
- material root is the right-side sink
- sink expressions sit immediately to the left of the root
- upstream expressions expand leftward by dependency depth
