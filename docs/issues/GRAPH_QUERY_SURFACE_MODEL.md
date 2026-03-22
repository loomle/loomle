# LOOMLE Local Issue: Unified `graph.query` Surface Model

## Problem

`LOOMLE` now has stronger graph tests across PCG, Material, and Blueprint, but
the product surface is still easiest to reason about one graph type at a time.

That creates two risks:

- product upgrades drift into graph-type-specific patches
- tests can go green while the shared product model stays unclear

The next stage should define one cross-graph `graph.query` model that explains
which kind of truth belongs on which surface.

## Product Direction

Treat `graph.query` as the primary read contract for graph-native inspection in
`LOOMLE`.

The shared model should be:

1. `pin_default`
- simple editable values
- native pin defaults
- synthetic writable pin defaults when needed

2. `effectiveSettings`
- grouped, structured, high-value node truth
- agent-relevant settings that are too important or too structured to stay
  flattened into pin defaults

3. `childGraphRef`
- graph-boundary truth
- second-hop traversal should stay graph-native

4. `residual_gap`
- explicit remaining product gaps
- narrow, auditable, and intentionally temporary unless promoted into a real
  surface category

This should become the default readback contract across all graph types.

## Shared Design Rules

### 1. Prefer graph-native readback first

Routine inspection should start with `graph.query`, not with `execute`.

Use fallback only when:

- the truth is not yet productized
- the gap is explicit
- the gap is still narrow enough to justify remaining outside the product
  surface

### 2. Keep graph boundaries graph-native

When a node points at another graph:

- surface `childGraphRef`
- let the caller follow with `graph.query(graphRef=...)`
- avoid collapsing cross-graph inspection into fallback scripts

### 3. Keep ordinary values generic

Do not introduce custom serializers for ordinary scalar or selector-like values
unless the node family truly needs grouped semantics.

The default path remains:

- pin defaults
- synthetic writable defaults
- generic query truth expansion

### 4. Reserve structured surfaces for coherent families

`effectiveSettings` should mean:

- the node family has grouped, meaningful semantics
- the grouped truth matters to agent behavior
- a coherent structured object is more trustworthy than a pile of flat values

### 5. Treat `residual_gap` as debt accounting, not success

A green residual-gap suite means:

- the gap is explicit
- the gap is classified correctly

It does **not** mean:

- the product capability is complete
- the node family is done

## Surface Definitions

### `pin_default`

Use this as the default surface for:

- simple create/meta/filter/route/transform families
- ordinary Material parameter-like settings
- ordinary Blueprint defaults that are already stably represented by the node

Success means:

- values are readable
- values are stable
- values are roundtrippable enough for automation to trust

### `effectiveSettings`

Use this when the product needs grouped truth such as:

- selectors
- actor/component targeting
- spawn identity and behavior
- override mappings
- structured grouped settings
- template-backed summaries
- other family-level semantics that are too important to scatter

Success means:

- the object is coherent
- the object is focused on agent-relevant truth
- it is close enough to complete that automation can treat it as the primary
  family readback

### `childGraphRef`

Use this when the node points to another graph asset or embedded graph.

Success means:

- the boundary is surfaced on the node
- `graph.list(includeSubgraphs=true)` agrees with query
- the second hop is queryable through `graphRef`
- graph traversal stays graph-native

### `residual_gap`

Keep this only when:

- the product truth is not yet sufficiently surfaced
- the missing truth is known and documented
- the family has a planned promotion path

Residual-gap nodes should eventually move into one of:

- richer `pin_default`
- fuller `effectiveSettings`
- `childGraphRef`
- a new explicitly named surface category

## Graph-Type Mapping

### PCG

PCG is the current strongest baseline for the full model.

It already demonstrates:

- broad `pin_default`
- structured `effectiveSettings`
- graph-native `childGraphRef`
- explicit `residual_gap`

PCG should continue to act as the proving ground for shared readback rules.

### Material

Material should remain mostly simple and graph-native:

- regular nodes stay primarily `pin_default`
- `MaterialFunctionCall` stays on `childGraphRef`
- only grouped or high-value expression families should move into a richer
  structured surface later

### Blueprint

Blueprint should not force everything into one of the existing buckets.

It now uses the shared model plus promoted Blueprint-native surfaces for the
families that were previously sitting in `residual_gap`.

In practice that means:

- stronger graph-boundary summaries for structure nodes
- a dedicated path for embedded-template truth
- separate treatment for context-sensitive construct nodes

Blueprint is now the first graph type to actually promote some former
`residual_gap` families into newly named product surfaces.

## New Surface Categories

The current four-surface model is enough for shared reasoning, but not
necessarily enough forever.

The first promoted categories are now:

1. `embedded_template`
- for Blueprint nodes whose truth spans both a graph node and a Blueprint-owned
  template object

2. `graph_boundary_summary`
- for Blueprint structure nodes whose main truth is graph-role and boundary
  semantics rather than ordinary settings

3. `context_sensitive_construct`
- for nodes whose creation/query truth depends strongly on actor/class/context
  assumptions

These categories now exist in Blueprint because they represent stable product
surfaces, not temporary test bookkeeping.

## Rollout

### Phase A

- treat this document as the shared `graph.query` contract
- continue upgrading PCG and Material within the existing four-surface model

### Phase B

- promote Blueprint residual-gap families into explicit product categories
- define the first new Blueprint-native surface names

### Phase C

- update node databases and handoff docs so the product model and test model use
  the same vocabulary
- keep the promoted Blueprint categories aligned with runtime query truth

## Acceptance Criteria

This issue is done when:

1. `LOOMLE` has one documented cross-graph `graph.query` surface model
2. the team can explain any tested node as primarily belonging to one surface
3. residual gaps are treated as explicit debt, not silent success
4. new surface categories are added only when they correspond to real product
   capabilities

Blueprint has already crossed the first major promotion threshold:

- `embedded_template`
- `graph_boundary_summary`
- `context_sensitive_construct`

That should be treated as proof that new categories are justified only when
they become real runtime surfaces and real catalog vocabulary together.
