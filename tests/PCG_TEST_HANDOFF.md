# PCG Test Handoff

This document is for product engineers who want to use the current `LOOMLE` PCG test system to drive product-side fixes.

It is intentionally short and action-oriented.

## What Is Ready

The PCG test system is now strong enough to drive product work directly.

It covers six complementary surfaces:

- node-plan coverage
- workflow truth coverage
- negative and boundary coverage
- stability and repeatability coverage
- selector truth coverage
- effective-settings coverage

The current system is designed to expose real product gaps, not just smoke-level breakage.

## What It Can Reveal

The highest-signal PCG failures currently fall into these groups.

### 1. Query Truth Gaps

These cases prove:

- `graph.mutate` succeeds
- `graph.verify` can still pass
- but `graph.query` does not surface the expected truth

This is currently the dominant PCG correctness signal.

### 2. Contract Surface Gaps

These cases prove:

- invalid requests are accepted when they should be rejected
- or different execution modes disagree on the same contract

Examples:

- `setPinDefault` dry-run contract drift
- missing subgraph asset accepted

### 3. Diagnostic Surface Gaps

These cases prove:

- the product fails correctly
- but does not expose rich enough diagnostics for the failure

Example:

- bad pin target errors without structured details or candidate pin help

### 4. Repeatability Gaps

These cases prove whether the same signal is stable across:

- repeated query reads
- repeated verify calls
- fresh sessions

## Recommended Commands

If you only want one quick readiness check, run:

```bash
python3 /Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py --project-root /Users/xartest/dev/LoomleDevHost
```

If you want the real PCG product-facing signals, run these six suites:

```bash
python3 /Users/xartest/dev/loomle/tools/run_pcg_graph_test_plan.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_pcg_workflow_truth_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_pcg_negative_boundary_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_pcg_stability_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_pcg_selector_truth_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_pcg_effective_settings_suite.py --project-root /Users/xartest/dev/LoomleDevHost
```

If you want JSON artifacts for inspection or sharing, add `--output <path>.json`.

## Current Workflow Truth Coverage

The workflow suite currently covers:

- `branch`
- `create`
- `filter`
- `meta`
- `predicate`
- `route`
- `sample`
- `select`
- `source`
- `spawn`
- `struct`
- `transform`

This means PCG workflow-level failures are no longer limited to one narrow pipeline shape.

## Current Negative / Boundary Coverage

The negative suite currently covers:

- `setPinDefault`
- `removeNode`
- `connectPins`
- `disconnectPins`

Across families including:

- `meta`
- `filter`
- `struct`
- `branch`
- `create`

## Current Selector Truth Coverage

The selector suite currently covers:

- plain attribute selectors surfaced through writable pin defaults
- property-accessor selectors such as `Position.Z`
- actor-selector structure surfaced through `effectiveSettings`
- mesh-selector structure surfaced through `effectiveSettings`

This means selector-backed PCG fields are now tested as a dedicated class instead of being treated as scalar one-off cases.

## Current EffectiveSettings Coverage

The effective-settings suite currently covers:

- truth checks for:
  - `GetActorProperty`
  - `GetSpline`
  - `StaticMeshSpawner`
- presence and shape checks for:
  - `DataFromActor`
  - `ApplyOnActor`
  - `SpawnActor`
  - `SpawnSpline`
  - `SpawnSplineMesh`
  - `SkinnedMeshSpawner`

This means high-value PCG nodes can now be tested against a dedicated `effectiveSettings` surface instead of being reduced to pin-default coverage only.

## How To Read Failures

### If workflow truth fails with `query_truth_unsurfaced`

Interpret that as:

- structure is usually correct
- verify is usually healthy
- the product is still failing to expose truth through `graph.query`

### If negative suite fails with `contract_surface_gap`

Interpret that as:

- the product accepted something it should reject
- or one execution mode disagrees with another on the same contract

### If negative suite fails with `diagnostic_surface_gap`

Interpret that as:

- the failure exists
- but the diagnostic payload is not rich enough yet

### If stability suite fails

Interpret that as:

- the signal itself is drifting across repeated reads, repeated verify calls, or fresh sessions

### If selector truth suite fails

Interpret that as:

- the product and `graph.query` disagree on selector semantics
- or selector structure is not being surfaced deeply enough for automation to trust it

### If effective-settings suite fails

Interpret that as:

- the node is marked as `effective_settings`
- but `graph.query` is either not surfacing that object at all
- or not surfacing enough grouped structure for automation to treat it as a trustworthy primary read surface

## Key Files

Execution:

- [run_pcg_graph_test_plan.py](/Users/xartest/dev/loomle/tools/run_pcg_graph_test_plan.py)
- [run_pcg_workflow_truth_suite.py](/Users/xartest/dev/loomle/tools/run_pcg_workflow_truth_suite.py)
- [run_pcg_negative_boundary_suite.py](/Users/xartest/dev/loomle/tools/run_pcg_negative_boundary_suite.py)
- [run_pcg_stability_suite.py](/Users/xartest/dev/loomle/tools/run_pcg_stability_suite.py)
- [run_pcg_selector_truth_suite.py](/Users/xartest/dev/loomle/tools/run_pcg_selector_truth_suite.py)
- [run_pcg_effective_settings_suite.py](/Users/xartest/dev/loomle/tools/run_pcg_effective_settings_suite.py)

Design context:

- [GRAPH_TEST_FRAMEWORK.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_ROADMAP.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_ROADMAP.md)

## Bottom Line

The PCG test system is ready to act as a product-fix radar.

Product engineers can now run the suites above, take the failures at face value, and work directly against them.
