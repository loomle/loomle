# Graph Test Roadmap

This document captures the intended upgrade path for the `LOOMLE 0.4` graph test system.

The roadmap is intentionally staged:

- first make the system runnable
- then make its coverage visible
- then deepen truth coverage where bugs are most likely

## Phase 1: Runnable Baseline

Goal:

- catalog-backed node classification
- `testing` metadata in node databases
- generated JSON test plans
- graph-type fixtures and recipes
- first runnable plan runner
- smoke validation for plan shape

This phase answers:

- can the system generate a plan
- can it run ready cases deterministically
- are `pass`, `fail`, `skip`, and `blocked` trustworthy

PCG is the first target because it is the most mature graph type for this baseline.

## Phase 2: Coverage Visibility

Goal:

- report not just execution status, but coverage depth
- distinguish shallow green coverage from strong truth coverage
- show which families are still blocked or workflow-only

This phase adds:

- coverage dimensions such as:
  - `inventory`
  - `construct`
  - `query_structure`
  - `engine_truth`
  - `dynamic_shape`
  - `recipe_context`
  - `workflow`
- generated coverage reports
- blocked-reason summaries
- family-level coverage summaries

This phase answers:

- how much of the graph type is only lightly covered
- which families already reach engine-truth coverage
- where blocked nodes are clustering

## Phase 3: Truth and Workflow Strengthening

Goal:

- expand from runnable node coverage to high-value correctness coverage

Priorities:

- query-truth audits that can be promoted into hard failures
- `graph.query` vs engine-truth comparisons
- family-level breakdown of `missingPins`, `unsurfacedFields`, and `mismatchedFields`
- workflow-truth suites for representative source/filter/route/spawn pipelines
- selector-heavy nodes
- dynamic pin truth
- blocked recipe expansion
- semantic workflow regressions

This phase answers:

- where the graph surface disagrees with Unreal truth
- which disagreements now fail the generated test runner directly
- which workflow edits preserve structure and verification health but still fail query truth
- which workflow families have strong regression protection
- which remaining gaps are product limits versus test-system limits

## Recommended Order by Graph Type

### 1. PCG

Start here because:

- family defaults are already useful
- settings truth drift is a known risk
- selector and dynamic pin issues are common and valuable

### 2. Material

Move here next because:

- node regularity is high
- fixture needs are simpler
- root-chain workflows are strong representatives

### 3. Blueprint

Do this after the first two because:

- context-bound nodes are more common
- recipe density is higher
- addability and graph legality are more sensitive to context

## Practical Guardrails

Do not start by assuming:

- every node needs a handwritten regression
- every field needs full roundtrip support immediately
- every graph type should advance in lockstep

Do start with:

- complete classification
- generated baseline coverage
- visible coverage depth
- targeted truth strengthening

## Backbone During Transition

Keep using the current suites as the execution backbone:

- `tests/e2e/test_bridge_smoke.py`
- `tests/e2e/test_bridge_regression.py`

The new framework should grow around them instead of replacing them all at once.
