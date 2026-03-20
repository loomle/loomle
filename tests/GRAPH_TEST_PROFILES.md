# Graph Test Profiles

This document defines the testing profiles used by the `LOOMLE` graph test framework.

The goal of a test profile is to answer one question:

- what is the primary way this node should be tested

Profiles are not meant to describe every possible test. They define the dominant testing strategy for each node.

## Profile Set

### `inventory_only`

Use when a node should be known and classified, but is not yet targeted for direct automated behavior coverage.

Best for:

- internal or edge nodes
- nodes with unclear context rules
- nodes that are currently cataloged before deeper testing exists

### `construct_only`

Use when the main requirement is:

- the node can be created

Best for:

- simple nodes
- broad first-pass coverage
- large families where structure matters more than settings on day one

### `construct_and_query`

Use when the node should:

- be constructible
- be visible through truthful structural readback

Best for:

- nodes where pin or child-graph structure matters
- nodes where settings mutation is not yet the main risk

### `read_write_roundtrip`

Use when the node must support:

- a representative write
- truthful readback

Best for:

- high-frequency settings nodes
- parameter nodes
- nodes where agent trust depends on value truth

### `dynamic_pin_probe`

Use when the main risk is:

- pin topology changes not being surfaced truthfully

Best for:

- dynamic-input nodes
- threshold/filter nodes
- nodes whose structure changes when settings change

### `context_recipe_required`

Use when the node is not meaningful or legal in isolation.

Best for:

- Blueprint variable access
- Blueprint events
- Blueprint function calls
- Material function calls
- PCG source nodes that depend on world context

### `semantic_family_represented`

Use when the node is most meaningfully covered through a real workflow regression rather than an isolated node test.

Best for:

- branch/route style nodes
- root/sink behavior
- nodes whose main value appears in rewrites or pipelines

## Family Defaults

These are default tendencies, not hard rules.

### PCG

- `source` -> `construct_and_query`
- `create` -> `read_write_roundtrip`
- `sample` -> `read_write_roundtrip`
- `transform` -> `read_write_roundtrip`
- `meta` -> `read_write_roundtrip`
- `route` -> `read_write_roundtrip`
- `filter` -> `dynamic_pin_probe`
- `spawn` -> `construct_and_query`
- `struct` -> `context_recipe_required`

### Material

- `parameter` -> `read_write_roundtrip`
- `math` -> `construct_and_query`
- `sample` -> `construct_and_query`
- `utility` -> `construct_only`
- `function` -> `context_recipe_required`
- `root` -> `semantic_family_represented`

### Blueprint

- `flow` -> `context_recipe_required`
- `variable` -> `context_recipe_required`
- `function_call` -> `context_recipe_required`
- `event` -> `context_recipe_required`
- `utility` -> `construct_and_query`
- `select` and `branch` -> `semantic_family_represented`

## Exception Rules

A node should override its family default when any of these are true:

- it requires external context to be legal
- dynamic pins are the primary risk
- the most important correctness signal is field truth, not structure
- the most valuable coverage comes from workflow regressions
- the family default would over-test or under-test the node

## Concrete Examples

### PCG `Transform Points`

Recommended profile:

- `read_write_roundtrip`

Reason:

- high-frequency settings node
- correctness depends on a few representative fields

### PCG `Filter Attribute Elements`

Recommended profile:

- `dynamic_pin_probe`

Reason:

- pin shape changes are the primary risk

### Material `MaterialFunctionCall`

Recommended profile:

- `context_recipe_required`

Reason:

- requires a child material function asset

### Blueprint `Branch`

Recommended profile:

- `semantic_family_represented`

Reason:

- real value appears in local control-flow workflows

## How Profiles Should Be Stored

Profiles should live in the node database under a dedicated `testing` object.

See:

- [GRAPH_TEST_CATALOG_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_CATALOG_SCHEMA.md)
