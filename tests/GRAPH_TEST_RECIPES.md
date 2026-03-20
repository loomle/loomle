# Graph Test Recipes

This document defines the role of recipes in the `LOOMLE` graph test framework.

A recipe is not a full test script. A recipe is:

- a minimal legal context template

Recipes exist so context-bound nodes can be tested in meaningful and valid environments.

Recipes should reference standard fixture bottoms from the fixture registry.

## What a Recipe Is For

A recipe answers:

- what minimal graph fixture is needed
- what minimal context must exist
- what setup must be applied before the node becomes meaningfully testable

A recipe does not answer:

- the full assertion strategy
- the full workflow regression
- the full mutation sequence for every test

Those are owned by:

- the test profile
- the graph adapter
- the concrete generated or handwritten test case

## Fixed Recipe Structure

Each recipe should use a small fixed structure.

### `id`

Unique identifier used by node testing metadata.

### `graphType`

One of:

- `blueprint`
- `material`
- `pcg`

### `fixture`

The base graph fixture or asset shape required by the recipe.

Examples:

- `blueprint_function_graph`
- `blueprint_event_graph`
- `material_graph`
- `pcg_graph_with_world_actor`

See:

- [GRAPH_TEST_FIXTURES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FIXTURES.md)

### `preconditions`

The conditions that must be true for the recipe to be valid.

Examples:

- target Blueprint must be editable
- graph type must allow event nodes
- function asset must exist
- world actor must be available

### `setup`

The smallest set of setup actions required to make the context legal and meaningful.

Examples:

- create a member variable
- create a Blueprint function
- create a Material Function asset
- place a test actor in the world

## Initial Recipe Set

### `blueprint_variable_access`

- `graphType`: `blueprint`
- `fixture`: `blueprint_function_graph`
- `preconditions`:
  - target Blueprint is editable
  - graph is a function graph
- `setup`:
  - create a member variable on the Blueprint
  - use a simple variable type first
  - ensure a legal insertion location exists

Best for:

- `Get Variable`
- `Set Variable`

### `blueprint_event_entry`

- `graphType`: `blueprint`
- `fixture`: `blueprint_event_graph`
- `preconditions`:
  - target Blueprint has an event graph
  - graph type allows event nodes
- `setup`:
  - open or create a legal event graph
  - avoid conflicting unique event conditions

Best for:

- `BeginPlay`
- other event entry nodes

### `blueprint_function_call`

- `graphType`: `blueprint`
- `fixture`: `blueprint_function_graph`
- `preconditions`:
  - Blueprint allows function definition and call testing
- `setup`:
  - create a minimal local Blueprint function
  - use a simple signature
  - ensure a legal call site exists

Best for:

- local function call nodes

### `material_function_call`

- `graphType`: `material`
- `fixture`: `material_graph`
- `preconditions`:
  - target Material graph is editable
  - project can create Material Function assets
- `setup`:
  - create a minimal legal Material Function
  - provide at least one clear input/output
  - allow the test Material graph to reference that function

Best for:

- `MaterialFunctionCall`

### `pcg_actor_source_context`

- `graphType`: `pcg`
- `fixture`: `pcg_graph_with_world_actor`
- `preconditions`:
  - editable PCG graph exists
  - test world can host a target actor
- `setup`:
  - place a minimal test actor in the world
  - add a simple component or exposed property if needed
  - make the actor stably targetable by the source node

Best for:

- `Get Actor Data`
- `Get Actor Property`
- similar actor-dependent PCG source nodes

## Relationship to Profiles

Profiles answer:

- how this node should be tested

Recipes answer:

- in what minimal context this node should be tested

So the relationship is:

- `profile` chooses the testing method
- `recipe` provides the legal context
- `focus` defines the most important target of the test

Recipes should also be the primary way fixture selection is resolved for context-bound nodes.

See:

- [GRAPH_TEST_PROFILES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PROFILES.md)
- [GRAPH_TEST_FIXTURES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FIXTURES.md)
- [GRAPH_TEST_CATALOG_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_CATALOG_SCHEMA.md)
