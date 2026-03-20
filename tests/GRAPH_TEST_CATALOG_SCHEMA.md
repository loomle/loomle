# Graph Test Catalog Schema

This document defines how testing metadata should be stored in graph node catalogs.

## Catalog Split

Graph catalogs should remain split between:

- `node-index.json`
- `node-database.json`

The index is for:

- fast browsing
- agent-oriented lookup
- lightweight discovery

The database is for:

- complete node facts
- deeper tool consumption
- testing metadata

Testing metadata should not live in the lightweight index because it adds browsing noise without helping normal agent lookup.

## Where Testing Metadata Lives

Testing metadata should live inside `node-database.json` under a dedicated `testing` object.

This keeps the node object clean:

- node fact fields remain node facts
- test strategy fields remain clearly separate

This also keeps a future extraction path open if testing metadata ever grows large enough to split out.

Fixture choice should not be stored directly in the initial node testing schema. It should normally be derived from:

- `recipe`, when present
- otherwise `profile + graphType`

## `testing` Object

The first version of `testing` should stay small.

### Required

- `profile`

### Optional

- `recipe`
- `focus`
- `reason`
- `notes`

## Field Meanings

### `profile`

The primary testing strategy for the node.

Examples:

- `construct_only`
- `read_write_roundtrip`
- `context_recipe_required`

### `recipe`

The recipe id required when the node cannot be meaningfully tested in isolation.

Examples:

- `blueprint_variable_access`
- `material_function_call`
- `pcg_actor_source_context`

### `focus`

A small object that identifies what the test should focus on.

It should remain profile-driven and compact.

Examples:

- `fields`
- `dynamicTriggers`
- `selectorFields`
- `workflowFamilies`

### `reason`

A short human-readable explanation for why this profile or override exists.

This is mainly for maintainers, not for runtime test execution.

### `notes`

An escape hatch for brief clarifications that do not fit elsewhere.

Use sparingly.

## Example Shapes

### Simple Constructible Node

```json
{
  "testing": {
    "profile": "construct_only"
  }
}
```

### Roundtrip Node

```json
{
  "testing": {
    "profile": "read_write_roundtrip",
    "focus": {
      "fields": ["radius"],
      "selectorFields": ["TargetAttribute"]
    }
  }
}
```

### Context-Bound Node

```json
{
  "testing": {
    "profile": "context_recipe_required",
    "recipe": "material_function_call",
    "reason": "Requires a child function asset."
  }
}
```

### Workflow-Covered Node

```json
{
  "testing": {
    "profile": "semantic_family_represented",
    "focus": {
      "workflowFamilies": ["blueprint_local_control_flow"]
    }
  }
}
```

## What Not To Put in `testing` Yet

Do not add these in the first version:

- fixture selection fields
- test results
- run history
- flaky markers
- owner fields
- smoke/regression scheduling flags
- version-tracking fields

The `testing` object is for design metadata, not execution status.

## Related Docs

- [GRAPH_TEST_FRAMEWORK.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_PROFILES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PROFILES.md)
- [GRAPH_TEST_RECIPES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_RECIPES.md)
