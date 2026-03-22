# LOOMLE Local Issue: Blueprint Embedded-Template Nodes

## Problem

Some Blueprint nodes look like ordinary graph nodes, but their real truth is not
fully contained in the node or its pins.

They also own or depend on Blueprint-internal template objects whose lifecycle
matters to creation, query, copy/paste, diff, and validation.

If `LOOMLE` keeps treating these nodes as ordinary `construct_and_query`
baseline nodes, the test system will systematically under-classify them and the
product surface will remain shallower than the real Unreal model.

## Product Direction

Introduce a first-class Blueprint node category:

- `embedded-template nodes`

This category means:

- a graph node is present in the Blueprint graph
- a Blueprint-owned template object also exists behind the node
- important truth lives in both places
- the node should not be modeled as an ordinary graph-only utility node

Short-term:

- move these nodes out of plain `construct_and_query`
- classify them as `context_recipe_required`
- mark their current query surface as `residual_gap`

Long-term:

- give them a stronger structured query surface that exposes the important
  template-backed truth directly

## Current Baseline

Today the following Blueprint nodes are still too shallowly classified in the
test system:

### Confirmed Embedded-Template Nodes

1. `UK2Node_Timeline`
2. `UK2Node_AddComponent`

### Upgrade Candidate, But Not Yet Confirmed As The Same Category

1. `UK2Node_AddComponentByClass`

## Why `UK2Node_Timeline` Belongs Here

`UK2Node_Timeline` is not just a node with pins.

It also depends on a `UTimelineTemplate` owned by the Blueprint:

- node creation allocates or binds a timeline template
- copy/paste duplicates timeline template state
- node query truth is incomplete if it only exposes the node name

Current `LOOMLE` query already hints at this by surfacing:

- `k2Extensions.timeline.timelineName`

But that is only a thin slice of the real truth.

Important template-backed truth still sits behind the node:

- timeline length
- autoplay
- loop
- replicated
- ignore time dilation
- track summaries
- later, possibly track-level details

## Why `UK2Node_AddComponent` Belongs Here

`UK2Node_AddComponent` is similarly not just a graph node.

Its real semantics depend on a Blueprint-owned component template:

- the node carries a hidden `TemplateName`
- the Blueprint stores a component template object behind that name
- copy/paste and reconstruction manage component template lifecycle
- node diffing already treats component template properties as meaningful truth

So this node also has dual truth:

- node truth
- embedded template truth

## Why `UK2Node_AddComponentByClass` Is Different

`UK2Node_AddComponentByClass` is complex, but its complexity is currently closer
to a dynamic construct/expand node than a stable embedded-template carrier.

It should probably still be upgraded from the current shallow baseline, but not
automatically grouped with `Timeline` and `AddComponent`.

Current recommendation:

- move it to `context_recipe_required`
- test it through actor-context workflow and negative coverage
- do not yet classify it as an embedded-template node

## Testing-System Direction

### Target Classification

For confirmed embedded-template nodes:

- `profile = context_recipe_required`
- `querySurface.kind = residual_gap`

This says:

- these nodes require richer Blueprint context than baseline graph fixtures
- their real query truth is still only partially surfaced today
- the residual gap must remain explicit and auditable

### Required Recipes

#### `blueprint_timeline_graph`

For:

- `UK2Node_Timeline`

Guarantees:

- actor-based Blueprint
- valid event or ubergraph context
- Blueprint supports timelines
- stable insertion point for timeline node creation

#### `blueprint_component_template_context`

For:

- `UK2Node_AddComponent`

Guarantees:

- actor-based Blueprint
- valid component-template or SCS-capable Blueprint context
- stable execution graph insertion point
- legal component-template side effects after node creation

### Coverage Layers

Embedded-template nodes should eventually be covered across these layers:

1. `recipe/context`
- invalid graph context should not be treated as success

2. `construct/query`
- node exists
- pins exist
- basic extension data exists

3. `negative`
- invalid context fails clearly and consistently

4. `residual-gap accounting`
- template-backed truth not yet surfaced by query remains explicit

5. `template truth`
- later phase
- structured query for template-backed state, not just node identity

## Query-Surface Direction

Short-term:

- keep embedded-template nodes under `residual_gap`

Long-term:

- consider adding a dedicated Blueprint query surface for embedded template truth

Possible future surface name:

- `embedded_template`

This would be distinct from:

- `pin_default`
- `effective_settings`
- `child_graph_ref`
- `residual_gap`

## Scope

### In Scope

- define the category
- confirm current members
- align testing strategy
- align recipe strategy
- explicitly separate `AddComponentByClass` from the confirmed set

### Out Of Scope

- implementing new Blueprint query serializers right now
- promoting `AddComponentByClass` into the embedded-template class without
  stronger evidence
- full track-level Timeline serialization
- full component-template object mirroring

## Recommended Rollout

### Phase A

- reclassify `UK2Node_Timeline`
- reclassify `UK2Node_AddComponent`
- add the two dedicated recipes

### Phase B

- add Blueprint residual-gap coverage specifically for embedded-template nodes
- make invalid-context behavior explicit

### Phase C

- design and implement a structured Blueprint query surface for embedded-template
  truth

## Acceptance Criteria

This local issue is done when:

1. `Timeline` and `AddComponent` are no longer treated as ordinary baseline
   utility nodes
2. both are covered through dedicated recipe-backed testing
3. their current query limitations are explicit in the residual-gap model
4. `AddComponentByClass` is upgraded appropriately without being incorrectly
   folded into the embedded-template category

## Current Recommendation Summary

Treat these as confirmed embedded-template nodes:

- `UK2Node_Timeline`
- `UK2Node_AddComponent`

Treat this as a nearby but distinct node:

- `UK2Node_AddComponentByClass`
