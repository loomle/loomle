# Blueprint Test Handoff

This document is for product engineers who want to use the current `LOOMLE` Blueprint test system to drive product-side fixes.

It is intentionally short and action-oriented.

## What Is Ready

The Blueprint test system is now strong enough to drive product work directly.

It currently covers six complementary surfaces:

- node-plan coverage
- coverage visibility
- workflow truth coverage
- negative and boundary coverage
- stability and repeatability coverage
- query-surface accounting

This means Blueprint is no longer limited to smoke-only validation. The test system can now distinguish:

- broad node coverage
- workflow-level structural correctness
- contract and diagnostics failures
- repeatability across repeated reads and fresh sessions

## What It Can Reveal

The highest-signal Blueprint failures currently fall into these groups.

### 1. Workflow Truth Gaps

These cases prove:

- a local graph edit applies
- `graph.query` still reports the expected nodes and edges
- `graph.verify` remains healthy when the workflow succeeds

This is the main signal for real editing workflows such as local insertion, replacement, and control-flow rewrites.

### 2. Contract Surface Gaps

These cases prove:

- invalid requests are rejected consistently
- partial apply behavior is surfaced correctly
- bad `setPinDefault` targets produce actionable diagnostics

### 3. Diagnostic Surface Gaps

These cases prove:

- the product fails correctly
- but the diagnostic payload is still too weak for automation to trust

### 4. Repeatability Gaps

These cases prove whether the same signal stays stable across:

- repeated query reads
- repeated verify calls
- fresh sessions

## Recommended Commands

If you only want one quick readiness check, run:

```bash
python3 tests/e2e/test_bridge_smoke.py --project-root /path/to/MyProject
```

If you want the real Blueprint product-facing signals, run these suites:

```bash
python3 tests/tools/generate_graph_test_plan.py --graph-type blueprint
python3 tests/tools/generate_graph_test_coverage_report.py --graph-type blueprint
python3 tests/tools/run_blueprint_workflow_truth_suite.py --project-root /path/to/MyProject
python3 tests/tools/run_blueprint_negative_boundary_suite.py --project-root /path/to/MyProject
python3 tests/tools/run_blueprint_stability_suite.py --project-root /path/to/MyProject
python3 tests/tools/run_blueprint_residual_gap_suite.py
python3 tests/tools/run_blueprint_embedded_template_suite.py
```

If you want JSON artifacts for inspection or sharing, add `--output <path>.json`.

## Current Coverage Shape

### Plan / Coverage

Current Blueprint baseline:

- `101` total nodes
- `89` ready
- `6` blocked
- `1` workflow-only
- `5` inventory-only

Coverage dimensions currently include:

- `construct`
- `query_structure`
- `recipe_context`
- `workflow`

### Workflow Truth

The workflow suite currently covers:

- local branch subgraph insertion
- delay-chain insertion
- sequence fanout construction
- replacing branch with sequence
- replacing delay with do-once

Current workflow status:

- `5 pass / 0 fail`

Recently resolved product signal:

- `replace_delay_with_do_once`
- product-side Blueprint macro resolution now falls back correctly for standard macro usage such as `DoOnce`

### Negative / Boundary

The negative suite currently covers:

- stale `expectedRevision` conflicts
- duplicate `clientRef` rejection
- bad `setPinDefault` target diagnostics
- partial apply rejection for unsupported ops

Current negative status:

- `4 pass / 0 fail`

### Stability

The stability suite currently covers:

- repeated query snapshots on a simple branch roundtrip
- repeated verify calls on a workflow graph
- fresh-session workflow repeatability

Current stability status:

- `3 pass / 0 fail`

### Query Surface Promotion

Blueprint query-surface promotion is now materially ahead of the old residual-gap model.

The current promoted Blueprint query surfaces are:

- `embedded_template`
- `graph_boundary_summary`
- `context_sensitive_construct`

Blueprint `residual_gap` is currently `0` documented cases.

### Embedded-Template Query Surface

Blueprint now explicitly classifies these nodes as embedded-template query-surface
nodes:

- `UK2Node_Timeline`
- `UK2Node_AddComponent`

The test system now expects:

- `context_recipe_required`
- dedicated recipes
- explicit `embedded_template` query-surface declaration
- live `graph.query` presence/shape coverage for both `embeddedTemplate` and `effectiveSettings`

`UK2Node_AddComponentByClass` is now recipe-backed through actor execution
context and now surfaces `context_sensitive_construct`, but is not treated as
an embedded-template node.

### Graph-Boundary Summary Query Surface

Blueprint now explicitly classifies these nodes as graph-boundary-summary nodes:

- `UK2Node_Composite`
- `UK2Node_FunctionEntry`
- `UK2Node_FunctionResult`
- `UK2Node_MacroInstance`
- `UK2Node_Tunnel`
- `UK2Node_TunnelBoundary`

Current plan state:

- `Composite`, `FunctionEntry`, `FunctionResult`, `MacroInstance`, and `Tunnel`
  are `recipe_case / ready`
- `TunnelBoundary` remains intentionally conservative as `blocked` because no
  stable authored recipe has been promoted yet

## How To Read Failures

### If workflow truth fails

Interpret that as:

- a local Blueprint editing workflow no longer preserves expected nodes, edges, query truth, or verify health

### If negative suite fails with `contract_surface_gap`

Interpret that as:

- the product accepted something it should reject
- or an unsupported operation is not surfaced strongly enough

### If negative suite fails with `diagnostic_surface_gap`

Interpret that as:

- the failure exists
- but the diagnostic payload is too weak for automation to rely on

### If stability suite fails

Interpret that as:

- the signal itself is drifting across repeated reads, repeated verify calls, or fresh sessions

## Key Files

Execution:

- [generate_graph_test_plan.py](tools/generate_graph_test_plan.py)
- [generate_graph_test_coverage_report.py](tools/generate_graph_test_coverage_report.py)
- [run_blueprint_workflow_truth_suite.py](tools/run_blueprint_workflow_truth_suite.py)
- [run_blueprint_negative_boundary_suite.py](tools/run_blueprint_negative_boundary_suite.py)
- [run_blueprint_stability_suite.py](tools/run_blueprint_stability_suite.py)
- [run_blueprint_residual_gap_suite.py](tools/run_blueprint_residual_gap_suite.py)
- [run_blueprint_embedded_template_suite.py](tools/run_blueprint_embedded_template_suite.py)

Design context:

- [GRAPH_TEST_FRAMEWORK.md](GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_ROADMAP.md](GRAPH_TEST_ROADMAP.md)

## Bottom Line

The Blueprint test system is now ready to act as a product-fix radar.

Current product-side Blueprint workflow semantics are in good shape.
