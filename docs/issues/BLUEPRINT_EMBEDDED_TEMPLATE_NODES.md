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
- promote their query surface into `embedded_template`

Long-term:

- give them a stronger structured query surface that exposes the important
  template-backed truth directly

## Current Product Status

`LOOMLE` now has first-pass product support for the confirmed embedded-template
members:

- `UK2Node_Timeline`
- `UK2Node_AddComponent`

They now:

- support direct `addNode.byClass` creation through the product surface
- expose `embeddedTemplate` through `graph.query`
- mirror that summary into `effectiveSettings`
- carry `querySurface.kind = embedded_template` in the Blueprint node database

The remaining work in this lane is no longer "surface something at all". It is
to keep the summaries stable, scoped, and recipe-backed.

## Confirmed Members

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

- keep it separate from the embedded-template bucket
- treat it as a `context_sensitive_construct`
- test it through actor-context workflow and negative coverage

## Testing-System Direction

### Target Classification

For confirmed embedded-template nodes:

- `profile = context_recipe_required`
- `querySurface.kind = embedded_template`

This says:

- these nodes require richer Blueprint context than baseline graph fixtures
- their real query truth now has a dedicated Blueprint-native surface
- the remaining work is to strengthen summary depth, not to hide the family in
  `residual_gap`

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

4. `template truth`
- structured query for template-backed state, not just node identity

5. `deeper summary`
- later phase
- only expand beyond the current summary when agent-relevant truth clearly
  benefits

## Query-Surface Direction

Short-term:

- keep embedded-template nodes on `embedded_template`

Long-term:

- `embedded_template`

This is distinct from:

- `pin_default`
- `effective_settings`
- `child_graph_ref`
- `residual_gap`

## Scope

### In Scope

- define the category
- confirm current members
- align testing strategy
- implement a real product surface for the confirmed members
- align recipe strategy
- explicitly separate `AddComponentByClass` from the confirmed set

### Out Of Scope

- promoting `AddComponentByClass` into the embedded-template class
- full track-level Timeline serialization
- full component-template object mirroring

## Recommended Rollout

### Phase A

- reclassify `UK2Node_Timeline`
- reclassify `UK2Node_AddComponent`
- add the two dedicated recipes

### Phase B

- implement `embeddedTemplate` query summaries for both nodes
- mirror those summaries into `effectiveSettings`

### Phase C

- keep recipe-backed validation aligned with the promoted product surface
- deepen summaries only where automation needs more than the current stable
  truth

## Acceptance Criteria

This local issue is done when:

1. `Timeline` and `AddComponent` are no longer treated as ordinary baseline
   utility nodes
2. both are covered through dedicated recipe-backed testing
3. both surface stable template-backed truth through `embedded_template`
4. `AddComponentByClass` is upgraded appropriately without being incorrectly
   folded into the embedded-template category

## Current Recommendation Summary

Treat these as confirmed embedded-template nodes:

- `UK2Node_Timeline`
- `UK2Node_AddComponent`

Treat this as a nearby but distinct node:

- `UK2Node_AddComponentByClass`
