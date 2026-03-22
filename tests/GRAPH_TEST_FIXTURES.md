# Graph Test Fixtures

This document defines the standard fixture registry for the `LOOMLE` graph test framework.

A fixture is:

- a standard reusable graph testing base

A fixture is not:

- a recipe
- a full workflow
- a node-specific script

Fixtures provide stable bottoms for tests. Recipes add the minimal extra context that specific nodes require.

Fixtures should normally be selected by rule, not stored directly on every node record.

## Fixture Registry Shape

Each fixture in the registry should define:

- `id`
- `graphType`
- `summary`
- `guarantees`
- `bestFor`

## Field Meanings

### `id`

Unique fixture identifier.

### `graphType`

One of:

- `blueprint`
- `material`
- `pcg`

### `summary`

A short human-readable description of the fixture.

### `guarantees`

The most important field.

This lists the conditions the fixture guarantees before a recipe or test begins.

### `bestFor`

A short list of the scenarios the fixture is best suited for.

## Blueprint Fixtures

### `blueprint_function_graph`

- `id`: `blueprint_function_graph`
- `graphType`: `blueprint`
- `summary`: minimal editable Blueprint function graph
- `guarantees`:
  - an editable Blueprint asset exists
  - a legal function graph exists
  - non-event nodes can be placed
  - the graph is queryable, mutable, and compileable
- `bestFor`:
  - variable access
  - function call recipes
  - utility node coverage

### `blueprint_event_graph`

- `id`: `blueprint_event_graph`
- `graphType`: `blueprint`
- `summary`: minimal editable Blueprint event graph
- `guarantees`:
  - an editable Blueprint asset exists
  - a legal event graph exists
  - event entry nodes are allowed
  - exec-chain testing is legal
- `bestFor`:
  - event entry
  - event-driven rewrites
  - entry-node recipes

### `blueprint_local_chain`

- `id`: `blueprint_local_chain`
- `graphType`: `blueprint`
- `summary`: minimal Blueprint exec chain with stable upstream and downstream interfaces
- `guarantees`:
  - a minimal legal exec chain already exists
  - upstream and downstream interfaces are explicit
  - local insert and replace edits are safe to perform
  - the compile baseline is healthy
- `bestFor`:
  - local control-flow rewrite
- branch and sequence insertion
- preserve-interface tests

### `blueprint_actor_execution_graph`

- `id`: `blueprint_actor_execution_graph`
- `graphType`: `blueprint`
- `summary`: actor-based Blueprint execution graph with legal actor-context construct semantics
- `guarantees`:
  - an editable actor-based Blueprint asset exists
  - a legal execution graph exists
  - actor-context construct nodes can be inserted meaningfully
- `bestFor`:
  - actor-bound construct recipes
  - `AddComponentByClass`

### `blueprint_timeline_graph`

- `id`: `blueprint_timeline_graph`
- `graphType`: `blueprint`
- `summary`: timeline-capable Blueprint event graph with legal timeline creation context
- `guarantees`:
  - an editable actor-based Blueprint asset exists
  - a legal event or ubergraph context exists
  - timeline creation is legal in the fixture context
- `bestFor`:
  - timeline recipes
  - embedded-template timeline coverage

### `blueprint_component_template_context`

- `id`: `blueprint_component_template_context`
- `graphType`: `blueprint`
- `summary`: actor-based Blueprint graph with legal component-template side effects
- `guarantees`:
  - an editable actor-based Blueprint asset exists
  - component-template or SCS side effects are legal
  - component-template carrier nodes can be inserted meaningfully
- `bestFor`:
  - `AddComponent`
  - embedded-template component coverage

## Material Fixtures

### `material_graph`

- `id`: `material_graph`
- `graphType`: `material`
- `summary`: minimal editable Material graph with a valid root
- `guarantees`:
  - an editable Material asset exists
  - a legal root exists
  - the graph baseline is healthy
  - ordinary expression nodes can be constructed
- `bestFor`:
  - basic construct coverage
  - math nodes
  - parameter nodes

### `material_root_chain`

- `id`: `material_root_chain`
- `graphType`: `material`
- `summary`: Material graph with a minimal valid expression chain already connected to root
- `guarantees`:
  - a minimal valid input chain is already connected to root
  - insertion or replacement before the root can be tested safely
  - sink correctness has a healthy baseline
- `bestFor`:
  - root-chain insertion
  - local replacement
  - sink-preserving workflows

### `material_function_graph`

- `id`: `material_function_graph`
- `graphType`: `material`
- `summary`: minimal editable Material Function graph with valid function context
- `guarantees`:
  - an editable Material Function asset exists
  - the function graph is legal
  - minimal input and output context exists
- `bestFor`:
  - function-internal coverage
  - function-call recipes
  - child-graph workflows

## PCG Fixtures

### `pcg_graph`

- `id`: `pcg_graph`
- `graphType`: `pcg`
- `summary`: minimal editable PCG graph with clean baseline structure
- `guarantees`:
  - an editable PCG graph exists
  - the graph structure is healthy
  - ordinary nodes can be directly constructed
  - the graph is queryable, mutable, and verifiable
- `bestFor`:
  - construct-only coverage
  - construct-and-query coverage
  - simple settings nodes

### `pcg_pipeline`

- `id`: `pcg_pipeline`
- `graphType`: `pcg`
- `summary`: minimal legal linear PCG dataflow pipeline with upstream and downstream nodes
- `guarantees`:
  - a minimal legal dataflow chain already exists
  - upstream and downstream are explicit
  - pipeline insert and replace edits are safe to perform
  - the verify baseline is healthy
- `bestFor`:
  - pipeline insertion
  - replacement
  - preserve-interface tests

### `pcg_pipeline_with_branch`

- `id`: `pcg_pipeline_with_branch`
- `graphType`: `pcg`
- `summary`: minimal branched PCG pipeline with observable dual-route interfaces
- `guarantees`:
  - a branched dataflow already exists
  - at least two observable route interfaces exist
  - downstream reconnect behavior can be validated
- `bestFor`:
  - route and filter workflows
  - branch-preserving rewrites
  - dual-output validation

### `pcg_graph_with_world_actor`

- `id`: `pcg_graph_with_world_actor`
- `graphType`: `pcg`
- `summary`: PCG graph with stable world actor context available for source nodes
- `guarantees`:
  - an editable PCG graph exists
  - a stable test actor exists in the world
  - graph and world context are both available
- `bestFor`:
  - actor-source recipes
  - property-source recipes
  - world-context source coverage

## Fixture and Recipe Boundaries

Fixtures provide:

- a legal graph or asset base
- a known structural baseline
- reusable testing bottoms

Recipes provide:

- node-specific minimal context
- context additions on top of a fixture
- the smallest legal setup needed before a profile can be exercised

So the intended relationship is:

- fixture registry defines the bottoms
- recipes choose which bottom to use
- profiles define how to test
- concrete cases define the operations and assertions

## Fixture Selection Rule

The first version should not store a per-node `preferredFixture` field in the catalog.

Fixture choice should be derived in this order:

1. if a node has a `recipe`, use the fixture bound by that recipe
2. otherwise, choose the default fixture from `profile + graphType`

This keeps the catalog smaller and avoids repeating information that can already be derived from the testing strategy.

If real exceptions appear later, a small override field can be introduced. It should not be part of the initial schema.

See:

- [GRAPH_TEST_FRAMEWORK.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_RECIPES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_RECIPES.md)
- [GRAPH_TEST_CATALOG_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_CATALOG_SCHEMA.md)
