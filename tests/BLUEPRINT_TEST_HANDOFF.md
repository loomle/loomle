# Blueprint Test Handoff

This document is for product engineers who want to use the current `LOOMLE` Blueprint test system to drive product-side fixes.

It is intentionally short and action-oriented.

## What Is Ready

The Blueprint test system is now strong enough to drive product work directly.

It currently covers five complementary surfaces:

- node-plan coverage
- coverage visibility
- workflow truth coverage
- negative and boundary coverage
- stability and repeatability coverage

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
python3 /Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py --project-root /Users/xartest/dev/LoomleDevHost
```

If you want the real Blueprint product-facing signals, run these five suites:

```bash
python3 /Users/xartest/dev/loomle/tools/generate_graph_test_plan.py --graph-type blueprint
python3 /Users/xartest/dev/loomle/tools/generate_graph_test_coverage_report.py --graph-type blueprint
python3 /Users/xartest/dev/loomle/tools/run_blueprint_workflow_truth_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_blueprint_negative_boundary_suite.py --project-root /Users/xartest/dev/LoomleDevHost
python3 /Users/xartest/dev/loomle/tools/run_blueprint_stability_suite.py --project-root /Users/xartest/dev/LoomleDevHost
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

Residual test-runner signal to fix on the test side:

- fresh-session Blueprint workflow runs can still hit an editor overwrite modal if a temporary fixture asset already exists
- when that modal blocks the editor game thread, the runner only sees `execute` fail with `EXECUTION_TIMEOUT`
- the actionable fix belongs in the Blueprint fixture lifecycle: make fixture creation idempotent by loading an existing temp asset or deleting it before `create_asset(...)`

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

- product signal: stable under `dev_verify`
- residual runner risk: standalone fresh-session reruns can still fail if temporary Blueprint fixture creation triggers an overwrite modal

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
- or the runner is hitting a temporary asset lifecycle issue that surfaces as an `execute` timeout rather than a Blueprint graph semantic failure

## Key Files

Execution:

- [generate_graph_test_plan.py](/Users/xartest/dev/loomle/tools/generate_graph_test_plan.py)
- [generate_graph_test_coverage_report.py](/Users/xartest/dev/loomle/tools/generate_graph_test_coverage_report.py)
- [run_blueprint_workflow_truth_suite.py](/Users/xartest/dev/loomle/tools/run_blueprint_workflow_truth_suite.py)
- [run_blueprint_negative_boundary_suite.py](/Users/xartest/dev/loomle/tools/run_blueprint_negative_boundary_suite.py)
- [run_blueprint_stability_suite.py](/Users/xartest/dev/loomle/tools/run_blueprint_stability_suite.py)

Design context:

- [GRAPH_TEST_FRAMEWORK.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_ROADMAP.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_ROADMAP.md)

## Bottom Line

The Blueprint test system is now ready to act as a product-fix radar.

Current product-side Blueprint workflow semantics are in good shape.

The next highest-value follow-up is on the test side: make temporary Blueprint fixture creation idempotent so fresh-session stability runs do not block on overwrite modals.
