# LOOMLE Blueprint Query Surface Sync Handoff

Status:

- product-side promotion: complete
- test-side sync: complete

## Summary

Blueprint query-surface promotion is now materially ahead of the current test
expectations.

This is not a partial "gap accounting only" change anymore. Product-side
runtime, catalog metadata, and plan generation now agree on new Blueprint query
surface categories.

## Promoted Surface Categories

Blueprint now uses these product surfaces:

- `embedded_template`
- `graph_boundary_summary`
- `context_sensitive_construct`

Blueprint `residual_gap` is now intentionally `0` documented cases.

## Current Catalog State

### `embedded_template`

- `UK2Node_Timeline`
- `UK2Node_AddComponent`

### `context_sensitive_construct`

- `UK2Node_AddComponentByClass`

### `graph_boundary_summary`

- `UK2Node_Composite`
- `UK2Node_FunctionEntry`
- `UK2Node_FunctionResult`
- `UK2Node_MacroInstance`
- `UK2Node_Tunnel`
- `UK2Node_TunnelBoundary`

## Current Plan State

These Blueprint nodes are now `recipe_case / ready`:

- `UK2Node_AddComponentByClass`
  - recipe: `blueprint_actor_execution_graph`
  - querySurface: `context_sensitive_construct`
- `UK2Node_MacroInstance`
  - recipe: `blueprint_actor_execution_graph`
  - querySurface: `graph_boundary_summary`
- `UK2Node_Composite`
  - recipe: `blueprint_actor_execution_graph`
  - querySurface: `graph_boundary_summary`
- `UK2Node_FunctionEntry`
  - recipe: `blueprint_function_graph`
  - querySurface: `graph_boundary_summary`
- `UK2Node_FunctionResult`
  - recipe: `blueprint_function_graph`
  - querySurface: `graph_boundary_summary`
- `UK2Node_Tunnel`
  - recipe: `blueprint_actor_execution_graph`
  - querySurface: `graph_boundary_summary`

This node remains intentionally conservative:

- `UK2Node_TunnelBoundary`
  - querySurface: `graph_boundary_summary`
  - plan state: `blocked`
  - reason: no stable authored recipe has been promoted yet

## Runtime Evidence Already Verified

The following product-side behaviors were probed directly:

- `K2Node_Timeline`
  - `addNode.byClass` works
  - `graph.query` returns `embeddedTemplate`
  - `effectiveSettings` mirrors `embeddedTemplate`
- `K2Node_AddComponent`
  - `addNode.byClass` works
  - `graph.query` returns `embeddedTemplate`
  - `effectiveSettings` mirrors `embeddedTemplate`
- `K2Node_AddComponentByClass`
  - `addNode.byClass` works
  - `graph.query` returns `contextSensitiveConstruct`
- `K2Node_Composite`
  - `addNode.byClass` works
  - root query returns `childGraphRef + graphBoundarySummary`
- `K2Node_MacroInstance`
  - workflow probe returns `graphBoundarySummary`
- `K2Node_FunctionEntry`
  - function-graph query returns `graphBoundarySummary`
- `K2Node_FunctionResult`
  - function-graph `addNode.byClass` works
  - query returns `graphBoundarySummary`
- `K2Node_Tunnel`
  - inline composite subgraph query returns `graphBoundarySummary`

## Test-Side Sync Status

The earlier stale expectations have now been synchronized:

1. `UK2Node_MacroInstance` is still treated as `residual_gap`
It should now be `graph_boundary_summary` and `recipe_case / ready`.

2. `UK2Node_AddComponentByClass` is still treated as having no `querySurface`
It should now be `context_sensitive_construct` and `recipe_case / ready`.

3. Blueprint residual-gap suite still assumes a non-zero residual bucket
It should now expect:
- `totalCases = 0`
- no Blueprint residual-gap entries
- no `UK2Node_MacroInstance` residual-gap case

4. Blueprint plan assertions should recognize these ready graph-boundary nodes:
- `UK2Node_Composite`
- `UK2Node_FunctionEntry`
- `UK2Node_FunctionResult`
- `UK2Node_MacroInstance`
- `UK2Node_Tunnel`

## Completed Test Update Order

1. Updated smoke/regression metadata expectations for:
- `MacroInstance`
- `AddComponentByClass`
- Blueprint residual-gap suite summary

2. Updated generated Blueprint plan expectations for:
- `Composite`
- `FunctionEntry`
- `FunctionResult`
- `Tunnel`

3. Kept `TunnelBoundary` conservative.

## Important Boundary

This handoff does not ask the test side to invent new product behavior.

It only asks them to sync expectations to product behavior that is already
present in runtime query readback, node catalog metadata, and plan generation.
