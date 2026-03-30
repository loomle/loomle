# Material Test Handoff

This document is for product engineers who want to use the current `LOOMLE` Material test system to drive product-side fixes.

It is intentionally short and action-oriented.

## What Is Ready

The Material test system is now strong enough to drive product work directly.

It currently covers six complementary surfaces:

- node-plan coverage
- coverage visibility
- workflow truth coverage
- negative and boundary coverage
- stability and repeatability coverage
- child-graph traversal coverage

This means Material is no longer limited to smoke-only validation. The test system can now distinguish:

- broad node coverage
- workflow-level structural correctness
- contract and diagnostics failures
- repeatability across repeated reads and fresh sessions

## What It Can Reveal

The highest-signal Material failures currently fall into these groups.

### 1. Workflow Truth Gaps

These cases prove:

- a local graph edit applies
- `graph.query` still reports the expected nodes and edges
- `graph.verify` remains healthy

This is the main signal for real editing workflows such as insertion, replacement, and root-chain preservation.

### 2. Contract Surface Gaps

These cases prove:

- invalid requests are accepted when they should be rejected
- or unsupported operations are not surfaced consistently enough

The current highest-value Material signal is in this group.

### 3. Diagnostic Surface Gaps

These cases prove:

- the product fails correctly
- but the diagnostic payload is too weak for automation to trust

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

If you want the real Material product-facing signals, run these suites:

```bash
python3 tools/generate_graph_test_plan.py --graph-type material
python3 tools/generate_graph_test_coverage_report.py --graph-type material
python3 tools/run_material_workflow_truth_suite.py --project-root /path/to/MyProject
python3 tools/run_material_negative_boundary_suite.py --project-root /path/to/MyProject
python3 tools/run_material_stability_suite.py --project-root /path/to/MyProject
python3 tools/run_material_child_graph_ref_suite.py --project-root /path/to/MyProject
```

If you want JSON artifacts for inspection or sharing, add `--output <path>.json`.

## Current Coverage Shape

### Plan / Coverage

Current Material baseline:

- `317` total nodes
- `317` ready
- `0` blocked

Coverage dimensions currently include:

- `construct`
- `query_structure`
- `engine_truth`
- `recipe_context`

### Workflow Truth

The workflow suite currently covers:

- root sink creation
- root-chain insertion
- leg insertion before a multi-input node
- unary replacement
- multi-input replacement

Current workflow status:

- `5 pass / 0 fail`

### Negative / Boundary

The negative suite currently covers:

- stale `expectedRevision` conflicts
- duplicate `clientRef` rejection
- unsupported `setPinDefault`
- bad output-pin handling for `connectPins`
- bad output-pin handling for `disconnectPins`

Current negative status:

- `3 pass / 2 fail`

Current failing product signals:

- `connectPins` accepts a clearly missing Material source output pin
- `disconnectPins` accepts a clearly missing Material source output pin

### Stability

The stability suite currently covers:

- repeated query snapshots on a simple roundtrip graph
- repeated verify calls on a workflow graph
- fresh-session workflow repeatability

Current stability status:

- `3 pass / 0 fail`

### Child Graph Ref

The child-graph suite currently covers:

- `MaterialFunctionCall` surfacing `childGraphRef`
- second-hop traversal into the referenced Material Function graph
- agreement between query-by-ref and query-by-name on the child graph

## How To Read Failures

### If workflow truth fails

Interpret that as:

- a local Material editing workflow no longer preserves expected nodes, edges, root bindings, query truth, or verify health

### If negative suite fails with `contract_surface_gap`

Interpret that as:

- the product accepted something it should reject
- or an unsupported operation is not surfaced strongly enough

This is currently the most important Material failure category.

### If negative suite fails with `diagnostic_surface_gap`

Interpret that as:

- the failure exists
- but the diagnostic payload is too weak for automation to rely on

### If stability suite fails

Interpret that as:

- the signal itself is drifting across repeated reads, repeated verify calls, or fresh sessions

## Key Files

Execution:

- [generate_graph_test_plan.py](../tools/generate_graph_test_plan.py)
- [generate_graph_test_coverage_report.py](../tools/generate_graph_test_coverage_report.py)
- [run_material_workflow_truth_suite.py](../tools/run_material_workflow_truth_suite.py)
- [run_material_negative_boundary_suite.py](../tools/run_material_negative_boundary_suite.py)
- [run_material_stability_suite.py](../tools/run_material_stability_suite.py)
- [run_material_child_graph_ref_suite.py](../tools/run_material_child_graph_ref_suite.py)

Design context:

- [GRAPH_TEST_FRAMEWORK.md](GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_ROADMAP.md](GRAPH_TEST_ROADMAP.md)

## Bottom Line

The Material test system is now ready to act as a product-fix radar.

Right now the most actionable product-side work is to fix the two negative-suite contract failures around bad Material output-pin acceptance.
