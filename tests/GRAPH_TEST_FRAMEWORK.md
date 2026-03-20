# Graph Test Framework

This document is the top-level design overview for `LOOMLE` graph testing in `0.4`.

The goal is not "one handcrafted test per node." The goal is:

- every graph node is inventoried
- every graph node has a declared testing strategy
- every graph type shares one common execution framework
- Blueprint, Material, and PCG keep their own semantic test logic
- high-frequency editing workflows are protected by strong semantic regressions

## Core Principle

Use one unified graph test framework with three graph-specific adapters:

- Blueprint
- Material
- PCG

The common framework should own:

- fixture lifecycle
- tool calling
- assertions
- case generation
- result collection

Each graph adapter should own:

- graph-specific fixtures
- node classification details
- semantic assertions
- validation rules
- context recipes

## Why Not One Flat Test Suite

Blueprint, Material, and PCG share a common graph editing protocol, but they do not share:

- node semantics
- success criteria
- context requirements
- pin models
- validation meaning

So the right split is:

- common execution framework
- graph-type-specific test logic

## Coverage Model

Graph testing should be organized as a coverage matrix, not a pile of bespoke tests.

Each node should be assigned at least one testing strategy. Some nodes will be covered directly by generated tests. Others will be covered through context recipes or semantic workflow regressions.

The framework should be able to answer:

- which nodes exist
- which nodes can be directly created
- which nodes require context
- which nodes support trustworthy readback
- which nodes are covered through family workflows instead of isolated node tests

Phase 2 adds a second view on top of the basic plan:

- not just whether a node is `ready`
- but which coverage dimensions are currently exercised

The first useful dimensions are:

- `inventory`
- `construct`
- `query_structure`
- `engine_truth`
- `dynamic_shape`
- `recipe_context`
- `workflow`

This lets the framework distinguish broad green coverage from deep truth coverage.

Phase 3 then turns part of that visibility into hard failure signals and gap taxonomy:

- `missingPins`
- `unsurfacedFields`
- `mismatchedFields`

So the framework can say not just that `graph.query` is weak, but exactly how it is weak.

## The Six Test Methods

These are the primary testing methods. Not every node should use every method.

### 1. Construction Test

Purpose:

- confirm a node can be created
- confirm `graph.query` can see it

### 2. Structural Read Test

Purpose:

- confirm the node shape is represented correctly

### 3. Read/Write Roundtrip Test

Purpose:

- confirm a high-value field can be written and then truthfully read back

### 4. Dynamic Pin Test

Purpose:

- confirm pin topology changes are surfaced truthfully

### 5. Context Recipe Test

Purpose:

- test nodes that are not meaningful or legal in isolation

### 6. Semantic Workflow Test

Purpose:

- validate a real user-facing editing workflow, not just a single node

See:

- [GRAPH_TEST_PROFILES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PROFILES.md)
- [GRAPH_TEST_RECIPES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_RECIPES.md)
- [GRAPH_TEST_FIXTURES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FIXTURES.md)

## Framework Layers

The graph test framework should be layered.

### Layer 1: Inventory Coverage

Every node in the database must be classified.

This layer proves:

- the node is known
- the node has a family
- the node has a testing strategy
- the node is not silently unowned

### Layer 2: Construction Coverage

For nodes that should be directly creatable, verify they can be instantiated and queried.

### Layer 3: Structural Read/Write Coverage

For high-value nodes, verify at least one meaningful field can round-trip.

### Layer 4: Semantic Family Coverage

For each graph family, maintain a small number of strong workflow regressions.

## Common Framework Responsibilities

The shared framework should provide:

- temporary asset creation
- graph bootstrap helpers
- cleanup
- deterministic naming
- `graph.query`
- `graph.mutate`
- `graph.verify`
- shared JSON parsing helpers
- node/edge/pin assertion helpers
- diagnostics helpers
- "query truth vs engine truth" comparison helpers
- catalog-driven case generation

## Graph Adapter Responsibilities

### Blueprint Adapter

Owns:

- exec/data pin semantics
- variable and function fixtures
- event/context recipes
- compile expectations

### Material Adapter

Owns:

- root sink rules
- expression-chain assertions
- function call recipes
- material-specific validation

### PCG Adapter

Owns:

- pipeline fixtures
- source/filter/route/spawn recipes
- selector and synthetic pin validation
- settings truth vs query truth comparisons
- strict query-truth assertions when generated cases can prove Unreal-side truth but `graph.query` does not surface it

## Recommended First Priorities

### PCG

Prioritize:

- construction coverage
- read/write roundtrip coverage
- selector coverage
- dynamic pin coverage

Reason:

- PCG currently has the clearest structural read/write drift risk

### Material

Prioritize:

- construction coverage
- root-chain structural coverage
- representative parameter and math nodes

### Blueprint

Prioritize:

- node classification
- context recipes
- exec/data structural coverage
- strong workflow regressions

Blueprint should not begin with a false assumption that every node is directly constructible.

## Non-Goals

This framework should not assume:

- every node deserves a bespoke handwritten regression
- every node must support full read/write fidelity on day one
- graph tools must fully mirror every Unreal object state

The framework should instead ensure:

- every node is accounted for
- every node has a declared testing strategy
- high-frequency graph workflows are strongly protected

## Related Design Docs

- [GRAPH_TEST_PROFILES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PROFILES.md)
- [GRAPH_TEST_RECIPES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_RECIPES.md)
- [GRAPH_TEST_FIXTURES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FIXTURES.md)
- [GRAPH_TEST_CATALOG_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_CATALOG_SCHEMA.md)
- [GRAPH_TEST_PLAN_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PLAN_SCHEMA.md)
- [GRAPH_TEST_ROADMAP.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_ROADMAP.md)
