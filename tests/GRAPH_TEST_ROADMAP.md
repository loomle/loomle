# Graph Test Roadmap

This document captures the intended implementation order for the `LOOMLE 0.4` graph test framework.

The goal is to build a scalable testing system without pretending every node needs a bespoke handwritten regression on day one.

## Phase 1: Design and Catalog Shape

Establish:

- framework overview
- test profiles
- recipes
- fixtures
- database testing metadata shape

This phase should produce:

- a stable profile vocabulary
- a stable recipe vocabulary
- a stable `testing` schema inside node databases
- a stable generated test plan schema

## Phase 2: Catalog Classification

Attach testing metadata to node databases.

Focus:

- family defaults
- exception overrides
- minimal reasons for special cases

This phase should answer:

- every node has a testing strategy
- every node is either directly testable, recipe-bound, or workflow-covered

## Phase 3: Fixture and Adapter Foundations

Build:

- common test harness abstractions
- Blueprint adapter fixtures
- Material adapter fixtures
- PCG adapter fixtures

Keep this phase focused on:

- stable bootstrapping
- deterministic naming
- cleanup
- shared assertions

## Phase 4: Generated Baseline Coverage

Generate first-pass tests for:

- inventory coverage
- construction coverage
- basic structural read coverage

This is where broad automatic coverage should start paying off.

## Phase 5: High-Value Roundtrip and Dynamic Coverage

Prioritize:

- PCG representative settings nodes
- selector-heavy nodes
- dynamic pin nodes
- Material parameter nodes

This phase should close the highest-value truth gaps first.

## Phase 6: Semantic Workflow Regressions

Maintain a smaller hand-authored layer that protects:

- local rewrites
- route/filter chains
- root-chain edits
- exec/data preserving edits

These should remain compact but strong.

## Recommended Implementation Order by Graph Type

### 1. Material

Start here because:

- defaults are relatively regular
- exception density is lower
- broad construct/query coverage should be easier

### 2. PCG

Then move here because:

- family defaults are strong
- selector and settings truth need focused work
- read/write drift risk is already known

### 3. Blueprint

Do this after the first two because:

- context-bound nodes are more common
- recipe coverage matters more
- direct addability assumptions are more fragile

## Practical Guardrails

Do not start with:

- every node needing a handwritten test
- every family needing deep workflow coverage immediately
- every field requiring perfect roundtrip support

Start with:

- complete classification
- generated baseline coverage
- targeted high-risk truth tests
- a smaller semantic regression layer

## Backbone During Transition

Keep using the current suites as the backbone:

- `tests/e2e/test_bridge_smoke.py`
- `tests/e2e/test_bridge_regression.py`

The new framework should grow around them, not replace them all at once.
