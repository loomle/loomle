# PCG Node Catalog vs LOOMLE Ops

This document clarifies the boundary between:

1. the `pcg-weaver` node catalog
2. LOOMLE's curated PCG semantic ops
3. generic graph construction via `addNode.byClass` and related graph tools

It exists to keep future PCG op expansion disciplined.

## Short Version

These are not the same layer.

- The node catalog answers: `What PCG node classes exist in UE source?`
- `graph.ops` answers: `Which stable PCG intentions does LOOMLE currently expose as semantic operations?`
- `graph.ops.resolve` answers: `How should a chosen semantic op be realized in this graph, with first-pass defaults, pin hints, and verification hints?`

In other words:

`catalog > ops > resolve plan`

## The Three Layers

### 1. Source-Derived Node Catalog

Sources:

- [/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog.json](/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog.json)
- [/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog.md](/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog.md)
- [/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog-usage.md](/Users/xartest/.codex/skills/pcg-weaver/references/pcg-node-catalog-usage.md)

Current summary:

- total settings classes: `178`
- classes with dynamic pins: `38`

What the catalog is good for:

- class discovery
- node title lookup
- header/source lookup
- editable property discovery
- broad coverage of the UE PCG surface area

What it is not:

- not graph-instance-derived
- not authoritative for dynamic pin layouts
- not a promise that LOOMLE exposes a semantic op for that node
- not enough by itself to wire a graph correctly

### 2. Curated Semantic Ops

Sources:

- [LoomleBridgeGraph.inl](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl#L47)
- [GRAPH_OPS_DESIGN.md](/Users/xartest/dev/loomle/docs/GRAPH_OPS_DESIGN.md)

Current PCG semantic ops:

- `pcg.create.points`
- `pcg.filter.elements_compare`
- `pcg.filter.elements_in_range`
- `pcg.filter.points_by_density`
- `pcg.meta.add_tag`
- `pcg.route.data_if_attribute_exists`
- `pcg.route.data_if_attribute_value`
- `pcg.route.data_if_attribute_in_range`
- `pcg.route.data_by_tag`
- `pcg.sample.points_ratio`
- `pcg.sample.surface`
- `pcg.transform.points`
- `pcg.source.actor_property`
- `pcg.sample.spline`
- `pcg.source.actor_data`
- `pcg.project.surface`
- `pcg.spawn.static_mesh`
- `pcg.spawn.actor`

What this layer is:

- curated
- stable
- intentionally small
- focused on common high-value graph intents

This is why `graph.ops` reports:

- `source = loomle_catalog`
- `coverage = curated`

Source:

- [LoomleBridgeGraph.inl](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl#L4335)

### 3. Resolve Plans

Source:

- [LoomleBridgeGraph.inl](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl#L5004)

`graph.ops.resolve` adds the missing execution layer on top of semantic ops:

- `settingsTemplate`
- `pinHints`
- `verificationHints`
- sometimes multi-step `steps`

This is the key distinction:

- the catalog tells us a node exists
- the resolve plan tells us how LOOMLE currently wants to realize the corresponding semantic stage

## Direct Mappings: Existing Ops vs Catalog

These existing semantic ops all map cleanly to known catalog entries.

| Semantic op | UE settings class | Catalog node title |
| --- | --- | --- |
| `pcg.create.points` | `UPCGCreatePointsSettings` | `Create Points` |
| `pcg.filter.elements_compare` | `UPCGAttributeFilteringSettings` | `Filter Attribute Elements` |
| `pcg.filter.elements_in_range` | `UPCGAttributeFilteringRangeSettings` | `Filter Attribute Elements by Range` |
| `pcg.filter.points_by_density` | `UPCGDensityFilterSettings` | `Density Filter` |
| `pcg.meta.add_tag` | `UPCGAddTagSettings` | `Add Tags` |
| `pcg.route.data_if_attribute_exists` | `UPCGFilterByAttributeSettings` | `Filter Data By Attribute` |
| `pcg.route.data_if_attribute_value` | `UPCGFilterByAttributeSettings` | `Filter Data By Attribute` |
| `pcg.route.data_if_attribute_in_range` | `UPCGFilterByAttributeSettings` | `Filter Data By Attribute` |
| `pcg.route.data_by_tag` | `UPCGFilterByTagSettings` | no friendly title recorded in catalog entry |
| `pcg.sample.points_ratio` | `UPCGSelectPointsSettings` | `Select Points` |
| `pcg.sample.surface` | `UPCGSurfaceSamplerSettings` | `Surface Sampler` |
| `pcg.transform.points` | `UPCGTransformPointsSettings` | `Transform Points` |
| `pcg.sample.spline` | `UPCGSplineSamplerSettings` | `Spline Sampler` |
| `pcg.source.actor_property` | `UPCGGetActorPropertySettings` | `Get Actor Property` |
| `pcg.source.actor_data` | `UPCGDataFromActorSettings` | `Get Actor Data` |
| `pcg.project.surface` | `UPCGProjectionSettings` | `Projection` |
| `pcg.spawn.static_mesh` | `UPCGStaticMeshSpawnerSettings` | catalog entry exists; title not normalized in our quick sample |
| `pcg.spawn.actor` | `UPCGSpawnActorSettings` | `Spawn Actor` |

## Important Non-Mapping Cases

These nodes exist in the catalog and are already relevant to LOOMLE usage, but they are not yet semantic ops.

### Already Useful via Generic Graph Editing

These are good examples of `catalog node != missing support`.

| UE settings class | Catalog title/name | Current LOOMLE status |
| --- | --- | --- |
| `UPCGFilterByAttributeSettings` | `FilterDataByAttribute` | no semantic op, but query and `setPinDefault` support exist |
| `UPCGGetActorPropertySettings` | `Get Actor Property` | no semantic op, but query/effective settings diagnostics already exist |
| `UPCGCreatePointsSphereSettings` | `Create Points Sphere` | no semantic op, but add-by-class regression coverage exists |
| `UPCGSubgraphSettings` | `Subgraph` | no semantic op, but query/list/childGraphRef support exists |

Evidence:

- [LoomleBridgeModule.cpp](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeModule.cpp#L66)
- [LoomleBridgeGraph.inl](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl#L226)
- [LoomleBridgeGraph.inl](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl#L337)
- [test_bridge_regression.py](/Users/xartest/dev/loomle/tests/e2e/test_bridge_regression.py#L3869)
- [test_bridge_regression.py](/Users/xartest/dev/loomle/tests/e2e/test_bridge_regression.py#L3537)

### Good Candidates for Future Semantic Ops

These look promising because they are common, high-signal, or structurally important:

- `UPCGFilterByAttributeSettings`
- `UPCGGetActorPropertySettings`
- `UPCGDifferenceSettings`
- `UPCGDistanceSettings`
- `UPCGAttributeNoiseSettings`
- `UPCGCreatePointsGridSettings`
- `UPCGCreatePointsSphereSettings`
- `UPCGCreateSplineSettings`
- `UPCGSubgraphSettings`
- `UPCGLoopSettings`

## Why LOOMLE Should Not Turn the Whole Catalog into Ops

Reasons to stay selective:

- many nodes are rare or highly specialized
- some nodes have unstable or context-heavy pin behavior
- some nodes are better handled through generic class construction plus readback
- every semantic op adds protocol, tests, default templates, and maintenance cost

The catalog is discovery breadth.
The op layer is productized intent.

## Current Recommended Routing

This matches the updated `pcg-weaver` skill.

1. If the requested stage already exists in `graph.ops`, prefer `graph.ops.resolve`.
2. If it does not exist in `graph.ops`, check the catalog for the node class and properties.
3. Use `addNode.byClass` for long-tail nodes.
4. Re-query immediately for actual pin names and graph-instance behavior.
5. Use `graph.verify` as the graph-level acceptance check.

Relevant skill references:

- [/Users/xartest/.codex/skills/pcg-weaver/SKILL.md](/Users/xartest/.codex/skills/pcg-weaver/SKILL.md)
- [/Users/xartest/.codex/skills/pcg-weaver/references/loomle-pcg-workflow.md](/Users/xartest/.codex/skills/pcg-weaver/references/loomle-pcg-workflow.md)

## Practical Rule of Thumb for New Ops

A catalog node should become a semantic op only if most of these are true:

- the intent is common and recognizable
- the first-pass settings can be templated
- the important pins can be hinted reliably
- the node is useful across many graphs, not just one project
- the op is more helpful than simply exposing the class path
- we can add regression coverage without brittle editor-specific behavior

## Suggested Next Batch to Evaluate

If we want to expand PCG ops next, the strongest candidates are:

1. `FilterByAttribute`
2. `GetActorProperty`
3. `Difference`
4. `Attribute Noise`
5. `Create Points Grid`
6. `Create Points Sphere`
7. `Subgraph`

Why these first:

- they are broadly useful
- they are understandable as semantic stages
- several already have partial support or test coverage in generic LOOMLE graph editing
- they help bridge the gap between the rich source catalog and the still-small curated op surface
