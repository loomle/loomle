# PCG Official Categories and LOOMLE Semantic Taxonomy

This document aligns three views of Unreal Engine 5.7 PCG nodes:

1. Epic's official Node Reference categories
2. the actual behavior visible in UE 5.7 source
3. a cleaner semantic taxonomy for future LOOMLE PCG ops

The goal is to avoid inheriting confusing node names directly into LOOMLE's semantic surface.

## Primary Reference

Epic official reference:

- [Procedural Content Generation Framework Node Reference in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-framework-node-reference-in-unreal-engine)

Important local source references used here:

- [PCGBranch.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/ControlFlow/PCGBranch.h)
- [PCGBooleanSelect.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/ControlFlow/PCGBooleanSelect.h)
- [PCGSwitch.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/ControlFlow/PCGSwitch.h)
- [PCGMultiSelect.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/ControlFlow/PCGMultiSelect.h)
- [PCGFilterByAttribute.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGFilterByAttribute.h)
- [PCGFilterByTag.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGFilterByTag.h)
- [PCGFilterByIndex.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGFilterByIndex.h)
- [PCGFilterByType.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGFilterByType.h)
- [PCGAttributeFilter.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGAttributeFilter.h)
- [PCGDensityFilter.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGDensityFilter.h)
- [PCGFilterElementsByIndex.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGFilterElementsByIndex.h)
- [PCGSelectPoints.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGSelectPoints.h)
- [PCGMetadataCompareOpElement.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/Metadata/PCGMetadataCompareOpElement.h)
- [PCGMetadataBooleanOpElement.h](/Users/Shared/Epic%20Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public/Elements/Metadata/PCGMetadataBooleanOpElement.h)

## Short Version

Epic's categories are useful, but they are editor-facing buckets, not a semantic contract for agents.

For LOOMLE, the useful distinction is not just `Filter` vs `Control Flow`.
The more important distinctions are:

- who computes the condition
- whether routing happens at whole-data or per-element granularity
- whether the node routes, selects, filters, samples, or computes a predicate

That leads to a cleaner semantic model:

1. `predicate`
2. `branch`
3. `route`
4. `select`
5. `filter`
6. `sample`
7. `structural`

## Epic's Official Categories

From the official UE 5.7 Node Reference, the most relevant categories for this discussion are:

- `Control Flow`
- `Filter`
- `Metadata`
- `Sampler`
- `Spatial`

Relevant examples from Epic's own grouping:

### Control Flow

- `Branch`
- `Select`
- `Select (Multi)`
- `Switch`

Epic's descriptions already frame these as execution-flow nodes rather than filters.

### Filter

- `Attribute Filter`
- `Attribute Filter Range`
- `Density Filter`
- `Filter Data By Attribute`
- `Filter Data By Tag`
- `Filter Data By Index`
- `Filter Data By Type`

This is the category where Epic's naming and grouping become less precise, because it mixes:

- whole-data routing
- per-element filtering
- specialized point filtering

### Metadata

- `Attribute Compare Op`
- `Attribute Boolean Op`
- `Attribute Noise`
- `Attribute Partition`
- `Attribute Rename`

These nodes often compute attributes or predicates rather than routing data by themselves.

### Sampler

- `Select Points`
- `Spline Sampler`
- `Surface Sampler`
- `Volume Sampler`

`Select Points` is especially relevant because it can look like a filter, but it is really a probabilistic sampler.

### Spatial

- `Create Points`
- `Create Points Grid`
- `Create Spline`
- `Union`
- `Loop`
- `Subgraph`

These nodes are more about data construction or graph structure than filtering.

## The Real Semantic Layers

Below is the taxonomy that appears most useful for LOOMLE.

### 1. Predicate Nodes

These nodes compute a boolean-like condition or a comparable value.
They do not primarily route data by themselves.

Examples:

- `Attribute Compare Op`
- `Attribute Boolean Op`

Recommended LOOMLE family:

- `pcg.predicate.compare`
- `pcg.predicate.boolean`

Why this matters:

- these nodes are often the missing upstream step before a `Branch` or `Switch`
- they should not be conflated with routing/filtering nodes

### 2. Branch Nodes

These nodes route one incoming data stream to one of several outgoing branches based on a control value.
They do not compute the condition; they consume it.

Examples:

- `Branch`
- `Switch`

Recommended LOOMLE family:

- `pcg.branch.bool`
- `pcg.branch.switch`

Semantics:

- one input
- multiple outputs
- control-flow-first

### 3. Select Nodes

These nodes choose one among multiple incoming candidates and produce a single outgoing stream.

Examples:

- `Select`
- `Select (Multi)`

Recommended LOOMLE family:

- `pcg.select.bool`
- `pcg.select.multi`

Semantics:

- multiple inputs
- one output
- control-flow-first

### 4. Route Nodes

These are the nodes that Epic often places under `Filter`, but they are better understood as whole-data routers.

They inspect metadata on a data object or collection and then send the entire input object to one route or another.

Examples:

- `Filter Data By Attribute`
- `Filter Data By Tag`
- `Filter Data By Index`
- `Filter Data By Type`

Recommended LOOMLE family:

- `pcg.route.data_if_attribute_exists`
- `pcg.route.data_if_attribute_value`
- `pcg.route.data_if_attribute_in_range`
- `pcg.route.data_by_tag`
- `pcg.route.data_by_index`
- `pcg.route.data_by_type`

Why `route` is better than `filter` here:

- these nodes make a whole-data decision
- they do not necessarily split individual points out of a point set
- for agents, `route` is much less misleading than `filter`

### 5. Filter Nodes

These nodes truly filter elements inside a dataset.
They operate at point or attribute-set-entry granularity.

Examples:

- `Attribute Filter`
- `Attribute Filter Range`
- `Filter Elements By Index`
- `Density Filter`

Recommended LOOMLE family:

- `pcg.filter.elements_compare`
- `pcg.filter.elements_in_range`
- `pcg.filter.elements_by_index`
- `pcg.filter.points_by_density`

Why this family is different:

- these nodes produce inside/outside subsets at element granularity
- they are real filters, not just routers

### 6. Sample Nodes

These nodes produce subsets or distributions by sampling rather than strict logical filtering.

Examples:

- `Select Points`
- `Surface Sampler`
- `Spline Sampler`
- `Volume Sampler`

Recommended LOOMLE family:

- `pcg.sample.points_ratio`
- `pcg.sample.surface`
- `pcg.sample.spline`
- `pcg.sample.volume`

Important distinction:

- `Select Points` belongs with sampling semantics, not filtering semantics

### 7. Structural Nodes

These nodes change graph shape, execution nesting, or subgraph boundaries rather than implementing a single filtering or routing intent.

Examples:

- `Loop`
- `Subgraph`

Recommended LOOMLE family:

- `pcg.struct.loop`
- `pcg.struct.subgraph`

## Side-by-Side: The Most Confusing Families

### `Branch` vs `Filter Data By Attribute`

`Branch`:

- consumes a boolean selection
- routes one input to one branch
- does not inspect attributes itself

`Filter Data By Attribute`:

- computes a condition from the input data's attributes
- then routes the whole input object to `InsideFilter` or `OutsideFilter`
- is semantically closer to `data-driven route` than to `filter`

Conclusion:

- `Filter Data By Attribute` is closer to `branch on data condition` than to `element filtering`

### `Filter Data By Attribute` vs `Attribute Filter`

`Filter Data By Attribute`:

- whole-data route
- can work on existence, single-threshold value, or value range
- includes `AnyOf` / `AllOf` style whole-data aggregation

`Attribute Filter`:

- per-element comparison
- true `A op B`
- threshold can be constant, other input, sampled spatial data, or attribute set

Conclusion:

- these nodes should never share the same semantic op family name

### `Select Points` vs true filters

`Select Points`:

- probabilistic subset sampling
- ratio-driven
- not an exact predicate-based split

`Density Filter` or `Attribute Filter`:

- deterministic predicate-based filtering

Conclusion:

- `Select Points` should live under `sample`, not `filter`

## Proposed LOOMLE Taxonomy Table

| Epic category | UE node | Actual behavior | Recommended LOOMLE family |
| --- | --- | --- | --- |
| Control Flow | `Branch` | one input, bool-routed outputs | `pcg.branch.*` |
| Control Flow | `Select`, `Select (Multi)` | multi-input selection to one output | `pcg.select.*` |
| Control Flow | `Switch` | one input, multi-output routing | `pcg.branch.*` |
| Filter | `Filter Data By Attribute` | whole-data route based on attribute-derived condition | `pcg.route.*` |
| Filter | `Filter Data By Tag` | whole-data route based on tags | `pcg.route.*` |
| Filter | `Filter Data By Index` | whole-data route based on collection index | `pcg.route.*` |
| Filter | `Filter Data By Type` | whole-data route based on data type | `pcg.route.*` |
| Filter | `Attribute Filter` | per-element compare split | `pcg.filter.*` |
| Filter | `Attribute Filter Range` | per-element range split | `pcg.filter.*` |
| Filter | `Filter Elements By Index` | per-element index split | `pcg.filter.*` |
| Filter | `Density Filter` | point-level specialized filter | `pcg.filter.*` |
| Metadata | `Attribute Compare Op` | computes boolean predicate | `pcg.predicate.*` |
| Metadata | `Attribute Boolean Op` | combines boolean predicates | `pcg.predicate.*` |
| Sampler | `Select Points` | probabilistic point sampling | `pcg.sample.*` |
| Sampler | `Surface Sampler`, `Spline Sampler` | creates sampled point sets | `pcg.sample.*` |
| Spatial | `Loop`, `Subgraph` | structural graph composition | `pcg.struct.*` |

## Practical Implication for Semantic Ops

LOOMLE should not expose one flat bag of `pcg.filter.*` names for all these nodes.

That would reproduce Epic's main source of ambiguity.

Instead, semantic ops should follow the behavior-first families:

1. `pcg.predicate.*`
2. `pcg.branch.*`
3. `pcg.select.*`
4. `pcg.route.*`
5. `pcg.filter.*`
6. `pcg.sample.*`
7. `pcg.struct.*`

## Suggested First Wave

If we start turning this taxonomy into real ops, the cleanest first wave is:

1. `pcg.route.data_if_attribute_exists`
2. `pcg.route.data_if_attribute_value`
3. `pcg.route.data_if_attribute_in_range`
4. `pcg.filter.elements_compare`
5. `pcg.filter.elements_in_range`
6. `pcg.branch.bool`
7. `pcg.select.bool`
8. `pcg.sample.points_ratio`

Why these first:

- they resolve the biggest naming ambiguity
- they are highly teachable to agents
- they line up well with both official docs and source behavior

## Relationship to Existing LOOMLE Work

This document complements:


That document explains `catalog vs ops vs resolve`.
This document explains `official categories vs real behavior vs semantic op families`.
