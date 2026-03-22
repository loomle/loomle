# LOOMLE Local Issue: Upgrade Blueprint Gaps Into Product Capabilities

## Problem

Blueprint currently has green residual-gap accounting, but that does not mean
the product surface is complete.

What it really means is:

- the missing areas are classified correctly
- the test system is no longer pretending those areas are fully covered

This is good progress, but it is still debt accounting.

The next product step should be to convert Blueprint residual gaps into explicit
capability lanes rather than leaving them as one undifferentiated bucket.

## Product Direction

Blueprint should promote its current residual gaps into three distinct product
capabilities:

1. `embedded_template`
2. `graph_boundary_summary`
3. `context_sensitive_construct`

These are not testing labels only. They should become real Blueprint query
concepts.

## Capability 1: `embedded_template`

### Meaning

Use this category when:

- a Blueprint graph node exists
- a Blueprint-owned template object also exists behind the node
- important truth lives across both

### Confirmed Members

1. `UK2Node_Timeline`
2. `UK2Node_AddComponent`

These are already documented in
[BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md](/Users/xartest/dev/loomle/docs/issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md).

### Product Goal

Do not mirror arbitrary UObject state.

Do expose a stable, structured summary of the template-backed truth that an
agent actually needs.

### Proposed Query Shape

#### `UK2Node_Timeline`

Target summary:

- `timelineName`
- `length`
- `autoPlay`
- `loop`
- `replicated`
- `ignoreTimeDilation`
- `trackSummary`

#### `UK2Node_AddComponent`

Target summary:

- `templateName`
- `componentClassPath`
- `attachPolicy`
- `relativeTransformSummary`
- `templatePropertySummary`

### Product Rule

These nodes should stop being treated as ordinary baseline utility nodes.

Their long-term home should be:

- `querySurface.kind = embedded_template`

## Capability 2: `graph_boundary_summary`

### Meaning

Use this category when the node’s main truth is structural and graph-role
oriented, not ordinary property settings.

### Target Members

1. `UK2Node_Composite`
2. `UK2Node_FunctionEntry`
3. `UK2Node_FunctionResult`
4. `UK2Node_Tunnel`
5. `UK2Node_TunnelBoundary`
6. some `UK2Node_MacroInstance` scenarios when the important truth is graph
   boundary identity

### Current Product Status

`LOOMLE` now has first-pass product surfaces for:

- `UK2Node_Composite`
- `UK2Node_FunctionEntry`
- `UK2Node_FunctionResult`
- `UK2Node_Tunnel`
- `UK2Node_MacroInstance`

These nodes already expose a stable `graphBoundarySummary` readback, and the
first four now have recipe-backed plan metadata rather than remaining blocked.

`UK2Node_TunnelBoundary` remains in this capability lane, but it behaves more
like an editor/compiler-generated structural helper than a reliably authored
asset node. It should be supported when encountered, without assuming it will
always appear in asset-backed recipes.

### Product Goal

These nodes should not remain “execute fallback by default”.

Instead, they should surface a graph-native structural summary.

### Proposed Query Shape

Target fields should focus on:

- `structureRole`
- `boundaryKind`
- `owningGraphRef`
- `boundGraphRef`
- `entryExitSemantics`
- `pinSignature`

### Product Rule

This category is about graph semantics, not template truth and not ordinary
node settings.

Its long-term home should be:

- `querySurface.kind = graph_boundary_summary`

## Capability 3: `context_sensitive_construct`

### Meaning

Use this category when:

- node legality depends on Blueprint context
- creation semantics depend on actor/class/environment assumptions
- query should still surface the stable part of truth even if full behavior is
  context-shaped

### Current Main Member

1. `UK2Node_AddComponentByClass`

### Current Product Status

`LOOMLE` now has first-pass product support for this lane:

- `UK2Node_AddComponentByClass` supports direct `addNode.byClass` creation
- `graph.query` surfaces a stable `contextSensitiveConstruct` object
- the Blueprint node database marks it as
  `querySurface.kind = context_sensitive_construct`

This means the category is no longer only a design placeholder. It is now a
real Blueprint product surface with a concrete runtime readback path.

### Why It Is Separate

It is complex, but it is not yet the same as the confirmed embedded-template
nodes.

It behaves more like a context-shaped construct node whose meaning depends on
class choice and graph context.

### Proposed Query Shape

Target fields should focus on:

- `selectedClassPath`
- `constructionMode`
- `exposedDynamicPins`
- `contextAssumptions`

### Product Rule

This node should not stay a vague residual gap, but it also should not be
forced into the embedded-template bucket.

Its long-term home should be:

- `querySurface.kind = context_sensitive_construct`

## Relationship To Existing Surface Model

These Blueprint capabilities sit on top of the shared graph-query model in
[GRAPH_QUERY_SURFACE_MODEL.md](/Users/xartest/dev/loomle/docs/issues/GRAPH_QUERY_SURFACE_MODEL.md).

They should be treated as Blueprint-specific promotions out of `residual_gap`,
not as exceptions to the shared model.

## Rollout

### Phase A

- keep current residual-gap accounting in place
- treat it as explicit debt, not completion

### Phase B

- formalize the three capability lanes in product vocabulary
- align node databases and handoff docs to those lanes

### Phase C

- implement the first real surface promotion:
  - `embedded_template` for `Timeline`
  - `embedded_template` for `AddComponent`

### Phase D

- implement `graph_boundary_summary`
- implement `context_sensitive_construct`

### Current Status

All three Blueprint promotion lanes now exist in product form:

- `embedded_template`
- `graph_boundary_summary`
- `context_sensitive_construct`

The remaining work is no longer to invent the lanes. It is to deepen recipe
coverage and tighten the summaries without collapsing them back into generic
fallback behavior.

## Recommended Implementation Order

1. `UK2Node_Timeline`
2. `UK2Node_AddComponent`
3. `UK2Node_Composite`
4. `UK2Node_FunctionEntry` / `UK2Node_FunctionResult`
5. `UK2Node_Tunnel` / `UK2Node_TunnelBoundary`
6. `UK2Node_AddComponentByClass`

This order is best because:

- the embedded-template category is already the clearest
- structure nodes form a coherent second family
- context-sensitive construct nodes should be designed carefully rather than
  rushed into the wrong bucket

## Acceptance Criteria

This issue is done when:

1. Blueprint residual gaps are no longer treated as one undifferentiated class
2. the product has explicit vocabulary for the three capability lanes
3. `Timeline` and `AddComponent` have a real promotion path out of residual-gap
   accounting
4. `AddComponentByClass` remains intentionally separate until its product
   surface is clearly defined

The current Blueprint catalog now satisfies the classification goal:

- `embedded_template`: 2 nodes
- `graph_boundary_summary`: 6 nodes
- `context_sensitive_construct`: 1 node
- `residual_gap`: 0 documented Blueprint cases

Within `graph_boundary_summary`, the current ready set is:

- `UK2Node_Composite`
- `UK2Node_FunctionEntry`
- `UK2Node_FunctionResult`
- `UK2Node_Tunnel`
- `UK2Node_MacroInstance`

`UK2Node_TunnelBoundary` remains intentionally conservative until a stable
asset-backed or recipe-backed authoring path is worth formalizing.
