# Graph Test Roadmap

This document captures the intended upgrade path for the `LOOMLE 0.4` graph test system.

The roadmap is intentionally staged:

- first make the system runnable
- then make its coverage visible
- then deepen truth coverage where bugs are most likely
- then scale the strongest patterns into broader graph-type and long-tail coverage

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
- multi-surface truth matrices across `mutate`, `queryStructure`, `queryTruth`, `engineTruth`, `verify`, and `diagnostics`
- negative and boundary suites for contract and diagnostic surfaces
- negative and boundary suite expansion across operations and family-specific surfaces
- stability and repeatability suites for repeated query, repeated verify, and fresh-session reproduction
- selector-heavy nodes
- dynamic pin truth
- blocked recipe expansion
- semantic workflow regressions

This phase answers:

- where the graph surface disagrees with Unreal truth
- which disagreements now fail the generated test runner directly
- which workflow edits preserve structure and verification health but still fail query truth
- which surface is breaking first when a case spans mutate, readback, verify, and engine truth
- which contract failures and diagnostic details are stable versus missing or inconsistent
- whether the same query, verify, and failure surfaces stay stable across repeated runs and fresh sessions
- which workflow families have strong regression protection
- which remaining gaps are product limits versus test-system limits

Current PCG end-state for this phase:

- generated node plans with hard query-truth failures
- coverage reports and truth-gap taxonomy
- workflow-truth suites across:
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
- negative and boundary suites across:
  - `setPinDefault`
  - `removeNode`
  - `connectPins`
  - `disconnectPins`
- stability and repeatability suites for:
  - repeated query snapshots
  - repeated verify surfaces
  - fresh-session workflow repeatability
- first selector-truth lane for selector-backed readback

## Phase 4: Scale and Long-Tail Strengthening

Goal:

- take the strongest Phase 3 patterns and scale them outward without weakening signal quality

Priorities:

- bring Material up through the same staged ladder:
  - runnable baseline
  - coverage visibility
  - truth and workflow strengthening
- then do the same for Blueprint, with recipe-heavy and context-heavy adaptations
- add more long-tail PCG workflow families only when they expand coverage shape rather than duplicate existing signal
- add structured selector truth as a first-class coverage lane for selector-backed fields instead of treating them as scalar one-off cases
- extend the first PCG selector-truth suite from attribute/property selectors into more selector-backed families and richer selector reports
- add query-surface classification as a first-class test dimension:
  - `pin_default`
  - `effective_settings`
  - `child_graph_ref`
  - `residual_gap`
- add dedicated surface suites for:
  - generic `pin_default` coverage expansion
  - `effective_settings` presence, shape, and truth
  - `child_graph_ref` traversal and graph-boundary validation
  - `residual_gap` accounting and fallback verification
- deepen serializer-surface awareness:
  - generic pin defaults
  - synthetic writable pins
  - settings/effective-settings surfaces
  - selector-specific surfaces
- expand recipe support for currently blocked graph families
- add stronger generated reporting for:
  - workflow-family completeness
  - blocked-node aging
  - surface drift across fresh sessions and reused sessions
- turn the most stable high-value patterns into reusable cross-graph templates

This phase answers:

- how quickly new graph types can inherit the PCG testing architecture
- which long-tail surfaces are still uncovered after strong family-level protection exists
- which blocked nodes stay blocked because of test-system limits versus product-surface limits
- where the next meaningful coverage gains come from after the core families are already under pressure
- which node families still rely on fallback because they lack a primary query surface
- which `effective_settings` families are only present versus truly complete
- which graph-boundary cases are query-native versus incorrectly treated as fallback

Current Material status entering this phase:

- runnable baseline and coverage visibility are active
- first workflow-truth suite is example-backed and covers:
  - root sink creation
  - root-chain insertion
  - leg insertion before a multi-input node
  - unary replacement
  - multi-input replacement
- first negative and boundary suite now probes:
  - stale `expectedRevision` conflicts
  - duplicate `clientRef` rejection
  - unsupported `setPinDefault`
  - bad output-pin handling for `connectPins` and `disconnectPins`
- first stability suite now probes:
  - repeated material query snapshots
  - repeated verify surfaces on a workflow graph
  - fresh-session workflow repeatability

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
- staged scale-out only after one graph type has completed the deeper truth loop

## Backbone During Transition

Keep using the current suites as the execution backbone:

- `tests/e2e/test_bridge_smoke.py`
- `tests/e2e/test_bridge_regression.py`

The new framework should grow around them instead of replacing them all at once.
