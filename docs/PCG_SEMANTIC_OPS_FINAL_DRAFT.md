# PCG Semantic Ops Final Draft

This document is the proposed final semantic taxonomy for LOOMLE PCG ops.

It is intended to be the behavior-first naming contract that sits above:

- Epic's editor-facing node categories
- UE source class names
- ad hoc historical op names

This draft is meant to answer one question clearly:

`What should a LOOMLE PCG op mean, and how should it be named?`

## Design Goal

The LOOMLE PCG semantic surface should be:

- understandable to agents
- stable across editor details
- behavior-first rather than menu-shaped
- explicit about graph semantics
- narrow enough that `graph.ops.resolve` can provide good templates and hints

## Core Rule

Do not group nodes by how Epic labeled them in the editor if that label hides materially different behavior.

In particular:

- not every Epic `Filter` node should become `pcg.filter.*`
- not every metadata operation should become `pcg.meta.*`
- names should reflect actual graph behavior, not UI habit

## Final Family Tree

The recommended semantic families are:

1. `pcg.source.*`
2. `pcg.create.*`
3. `pcg.sample.*`
4. `pcg.transform.*`
5. `pcg.meta.*`
6. `pcg.predicate.*`
7. `pcg.branch.*`
8. `pcg.select.*`
9. `pcg.route.*`
10. `pcg.filter.*`
11. `pcg.spawn.*`
12. `pcg.struct.*`

These families are intentionally behavior-oriented.

## Family Definitions

### 1. `pcg.source.*`

Meaning:

- retrieve or materialize data from the world, actors, components, or external objects

Examples:

- `pcg.source.actor_data`
- `pcg.source.actor_property`

Good future examples:

- `pcg.source.component_data`
- `pcg.source.spline_data`
- `pcg.source.primitive_data`

### 2. `pcg.create.*`

Meaning:

- create new synthetic PCG data directly from settings rather than sampling existing world data

Examples:

- `pcg.create.points`

Good future examples:

- `pcg.create.points_grid`
- `pcg.create.points_sphere`
- `pcg.create.spline`

### 3. `pcg.sample.*`

Meaning:

- derive a subset or generated point set by sampling a source distribution or geometry

Examples:

- `pcg.sample.surface`
- `pcg.sample.spline`

Good future examples:

- `pcg.sample.points_ratio`
- `pcg.sample.volume`
- `pcg.sample.mesh`

Important note:

- `Select Points` belongs here, not under `filter`

### 4. `pcg.transform.*`

Meaning:

- alter spatial state or project existing data into a different spatial representation

Examples:

- `pcg.transform.points`
- `pcg.project.surface`

Good future examples:

- `pcg.transform.bounds`
- `pcg.transform.copy_points`

### 5. `pcg.meta.*`

Meaning:

- create, mutate, rename, partition, annotate, or otherwise manipulate metadata

Examples:

- `pcg.meta.add_tag`

Good future examples:

- `pcg.meta.noise`
- `pcg.meta.partition`
- `pcg.meta.rename`
- `pcg.meta.create_attribute`

Important note:

- not all metadata nodes belong here
- if a node's primary purpose is to compute a predicate, it should be in `pcg.predicate.*`

### 6. `pcg.predicate.*`

Meaning:

- compute a boolean condition or comparison result for later control or filtering

Good examples:

- `pcg.predicate.compare`
- `pcg.predicate.boolean`

Why this family exists:

- these nodes are the missing semantic layer between raw attributes and branch/select/route behavior
- they are often how a graph should make conditions explicit instead of burying them inside a route/filter node

### 7. `pcg.branch.*`

Meaning:

- route one input stream to one of several outputs based on a control selection

Good examples:

- `pcg.branch.bool`
- `pcg.branch.switch`

Semantics:

- one input
- multiple outputs
- control-flow node

### 8. `pcg.select.*`

Meaning:

- choose one among multiple incoming candidate streams and forward it to a single output

Good examples:

- `pcg.select.bool`
- `pcg.select.multi`

Semantics:

- multiple inputs
- one output
- control-flow node

### 9. `pcg.route.*`

Meaning:

- inspect a whole input data object or collection and send that whole object to one branch or another

This family exists specifically to capture nodes that Epic groups under `Filter`, but that behave more like data-driven branching.

Good examples:

- `pcg.route.data_by_tag`
- `pcg.route.data_by_index`
- `pcg.route.data_by_type`
- `pcg.route.data_if_attribute_exists`
- `pcg.route.data_if_attribute_value`
- `pcg.route.data_if_attribute_in_range`

Semantics:

- whole-data routing
- not element-level filtering
- often equivalent to `branch on data condition`

### 10. `pcg.filter.*`

Meaning:

- split elements inside a dataset based on element-level predicates

Good examples:

- `pcg.filter.elements_compare`
- `pcg.filter.elements_in_range`
- `pcg.filter.elements_by_index`
- `pcg.filter.points_by_density`

Semantics:

- point or attribute-set-entry granularity
- produces actual subsets
- this is the true filtering family

### 11. `pcg.spawn.*`

Meaning:

- instantiate or emit runtime-facing artifacts downstream from points or attributes

Examples:

- `pcg.spawn.static_mesh`
- `pcg.spawn.actor`

Good future examples:

- `pcg.spawn.component`
- `pcg.spawn.spline`

### 12. `pcg.struct.*`

Meaning:

- change graph structure, composition, or nested execution rather than primarily changing data values

Good examples:

- `pcg.struct.subgraph`
- `pcg.struct.loop`

## Canonical Naming Rules

1. Use lowercase dotted identifiers.
2. Prefer behavior terms over engine class names.
3. Prefer semantic intent over menu wording.
4. Only use `filter` when the node actually filters elements.
5. Use `route` for whole-data conditional forwarding.
6. Use `branch` and `select` only for explicit control-flow nodes.
7. Split overloaded UE nodes into multiple semantic ops when their modes have different mental models.

## Canonical Mappings for Confusing UE Nodes

### `Filter Data By Attribute`

UE node:

- `UPCGFilterByAttributeSettings`

Canonical LOOMLE semantic split:

- `pcg.route.data_if_attribute_exists`
- `pcg.route.data_if_attribute_value`
- `pcg.route.data_if_attribute_in_range`

Reason:

- one UE node contains three materially different behaviors
- it routes whole input data, not individual elements
- `filter` is misleading here

### `Filter Attribute Elements`

UE node:

- `UPCGAttributeFilteringSettings`

Canonical LOOMLE semantic split:

- `pcg.filter.elements_compare_constant`
- `pcg.filter.elements_compare_input`

Possible simplification:

- expose only `pcg.filter.elements_compare` first if resolve can clearly represent the threshold source mode

### `Filter Attribute Elements by Range`

UE node:

- `UPCGAttributeFilteringRangeSettings`

Canonical LOOMLE semantic split:

- `pcg.filter.elements_in_range_constant`
- `pcg.filter.elements_in_range_input`

Possible simplification:

- expose only `pcg.filter.elements_in_range` first if resolve can carry constant vs input threshold clearly

### `Filter Data By Tag`

UE node:

- `UPCGFilterByTagSettings`

Canonical LOOMLE semantic name:

- `pcg.route.data_by_tag`

### `Filter Data By Index`

UE node:

- `UPCGFilterByIndexSettings`

Canonical LOOMLE semantic name:

- `pcg.route.data_by_index`

### `Filter Data By Type`

UE node:

- `UPCGFilterByTypeSettings`

Canonical LOOMLE semantic name:

- `pcg.route.data_by_type`

### `Density Filter`

UE node:

- `UPCGDensityFilterSettings`

Canonical LOOMLE semantic name:

- `pcg.filter.points_by_density`

Reason:

- despite the generic Epic category, this is a specialized point-level filter

### `Select Points`

UE node:

- `UPCGSelectPointsSettings`

Canonical LOOMLE semantic name:

- `pcg.sample.points_ratio`

Reason:

- probabilistic point selection is sampling semantics, not predicate filtering

## Current Ops Mapped into the Final Taxonomy

Current or in-progress LOOMLE ops should be understood as follows:

| Existing op | Final family interpretation |
| --- | --- |
| `pcg.create.points` | `pcg.create.*` |
| `pcg.meta.add_tag` | `pcg.meta.*` |
| `pcg.route.data_by_tag` | `pcg.route.*` |
| `pcg.route.data_if_attribute_exists` | `pcg.route.*` |
| `pcg.route.data_if_attribute_value` | `pcg.route.*` |
| `pcg.route.data_if_attribute_in_range` | `pcg.route.*` |
| `pcg.sample.surface` | `pcg.sample.*` |
| `pcg.transform.points` | `pcg.transform.*` |
| `pcg.source.actor_property` | `pcg.source.*` |
| `pcg.sample.spline` | `pcg.sample.*` |
| `pcg.source.actor_data` | `pcg.source.*` |
| `pcg.project.surface` | `pcg.transform.*` |
| `pcg.spawn.static_mesh` | `pcg.spawn.*` |
| `pcg.spawn.actor` | `pcg.spawn.*` |

## Recommended Canonical First-Wave Set

This is the set that should exist first if we want a coherent public PCG surface.

### Source

- `pcg.source.actor_data`
- `pcg.source.actor_property`

### Create

- `pcg.create.points`
- `pcg.create.points_grid`
- `pcg.create.points_sphere`

### Sample

- `pcg.sample.surface`
- `pcg.sample.spline`
- `pcg.sample.points_ratio`

### Transform

- `pcg.transform.points`
- `pcg.project.surface`

### Meta

- `pcg.meta.add_tag`
- `pcg.meta.noise`

### Predicate

- `pcg.predicate.compare`
- `pcg.predicate.boolean`

### Branch / Select

- `pcg.branch.bool`
- `pcg.branch.switch`
- `pcg.select.bool`
- `pcg.select.multi`

### Route

- `pcg.route.data_by_tag`
- `pcg.route.data_by_type`
- `pcg.route.data_by_index`
- `pcg.route.data_if_attribute_exists`
- `pcg.route.data_if_attribute_value`
- `pcg.route.data_if_attribute_in_range`

### Filter

- `pcg.filter.elements_compare`
- `pcg.filter.elements_in_range`
- `pcg.filter.elements_by_index`
- `pcg.filter.points_by_density`

### Spawn

- `pcg.spawn.static_mesh`
- `pcg.spawn.actor`

### Struct

- `pcg.struct.subgraph`
- `pcg.struct.loop`

## Why This Is the Right Final Shape

This taxonomy does three things better than Unreal's raw node naming:

1. it makes the unit of behavior explicit
   - whole data
   - element
   - control flow
   - sampling

2. it makes agent planning easier
   - agents can pick by semantics without reverse-engineering UE naming quirks

3. it makes `graph.ops.resolve` more honest
   - each op can have tighter templates, pin hints, and verification hints

## Relationship to Other Docs

This document should be read after:

- [PCG_NODE_CATALOG_VS_OPS.md](/Users/xartest/dev/loomle/docs/PCG_NODE_CATALOG_VS_OPS.md)
- [PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md](/Users/xartest/dev/loomle/docs/PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md)

Those explain:

- catalog vs semantic ops vs resolve
- official categories vs actual behavior

This document is the final naming proposal to build on top of those conclusions.
