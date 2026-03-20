# Graph Test Plan Schema

This document defines the first-version JSON plan schema for generated graph test plans in `LOOMLE 0.4`.

The plan is a generated artifact, not a hand-authored source file.

Its job is to answer:

- how each node is covered
- which nodes are ready for automatic case generation
- which nodes still depend on recipes or workflow coverage
- which nodes are currently blocked by missing metadata

## Plan File Strategy

Test plans should be:

- generated as JSON
- split by `graphType`
- treated as build or CI artifacts

They should not be committed as source files in the first version.

The recommended shape is:

- one plan per graph type
- optional aggregate summary outside the plan schema

Examples:

- `blueprint_test_plan.json`
- `material_test_plan.json`
- `pcg_test_plan.json`

Coverage-depth reporting is a separate generated view built from the plan.

The plan stays focused on per-node coverage decisions. Coverage reports answer:

- how many nodes currently reach `construct`
- how many reach `query_structure`
- how many reach `engine_truth`
- how many are still only `workflow` or `inventory`

## Top-Level Schema

Each plan file should have this minimal top-level shape:

- `version`
- `graphType`
- `generatedAt`
- `sourceCatalog`
- `summary`
- `entries`

### `version`

Schema version for the plan format.

The first version should be:

- `"1"`

### `graphType`

One of:

- `"blueprint"`
- `"material"`
- `"pcg"`

### `generatedAt`

Generation timestamp for debugging and artifact tracing.

### `sourceCatalog`

Minimal information about the database used to generate the plan.

The first version only needs:

- `path`

Example:

```json
{
  "path": "/abs/path/to/node-database.json"
}
```

### `summary`

Lightweight top-level coverage summary.

The first version should include:

- `totalNodes`
- `readyAutoCases`
- `readyRecipeCases`
- `workflowOnly`
- `inventoryOnly`
- `blocked`

This summary should stay lightweight. Family breakdown can be derived later from entries instead of being embedded in the first version.

### `entries`

Per-node coverage decisions.

Each entry represents one node from the source database.

## Entry Schema

Each entry should contain:

- `nodeKey`
- `className`
- `displayName`
- `family`
- `profile`
- `mode`
- `fixture`
- `recipe`
- `focus`
- `status`

Optional:

- `reason`

### `nodeKey`

Stable node identifier used inside the plan.

Prefer:

- `className`
- or a stable class path

Do not use display names as the primary key.

### `className`

Canonical Unreal class name.

### `displayName`

Human-readable node name for review and debugging.

### `family`

Semantic node family from the node database.

### `profile`

The node's testing profile from `database.testing.profile`.

Examples:

- `construct_only`
- `read_write_roundtrip`
- `context_recipe_required`

### `mode`

Generator decision for how the node is covered.

The first version should use:

- `inventory`
- `auto_case`
- `recipe_case`
- `workflow_map`
- `blocked`

This is distinct from `profile`:

- `profile` is design intent
- `mode` is generator output

### `fixture`

Resolved fixture id for the generated case.

Rules:

- if a node has a `recipe`, use that recipe's fixture
- otherwise derive fixture from `profile + graphType`

If no fixture applies, this field can be `null`.

### `recipe`

Recipe id when the node depends on a recipe-bound context.

Otherwise `null`.

### `focus`

Small object containing the specific test focus for the node.

The first version should only allow:

- `fields`
- `dynamicTriggers`
- `workflowFamilies`

Examples:

```json
{
  "fields": ["radius"]
}
```

```json
{
  "dynamicTriggers": ["bUseConstantThreshold"]
}
```

```json
{
  "workflowFamilies": ["blueprint_local_control_flow"]
}
```

### `status`

Readiness of the generated plan entry.

The first version should use:

- `ready`
- `inventory_only`
- `workflow_only`
- `blocked`

### `reason`

Optional short explanation when the mode or status needs clarification.

Examples:

- `missing recipe`
- `missing focus.fields`
- `workflow family not assigned`

## Example Entries

### Ready Auto Case

```json
{
  "nodeKey": "UPCGTransformPointsSettings",
  "className": "UPCGTransformPointsSettings",
  "displayName": "Transform Points",
  "family": "transform",
  "profile": "read_write_roundtrip",
  "mode": "auto_case",
  "fixture": "pcg_graph",
  "recipe": null,
  "focus": {
    "fields": ["rotationMin", "bAbsoluteRotation"]
  },
  "status": "ready"
}
```

### Ready Recipe Case

```json
{
  "nodeKey": "UMaterialExpressionMaterialFunctionCall",
  "className": "UMaterialExpressionMaterialFunctionCall",
  "displayName": "Material Function Call",
  "family": "function",
  "profile": "context_recipe_required",
  "mode": "recipe_case",
  "fixture": "material_graph",
  "recipe": "material_function_call",
  "focus": {},
  "status": "ready"
}
```

### Workflow-Mapped Node

```json
{
  "nodeKey": "UK2Node_IfThenElse",
  "className": "UK2Node_IfThenElse",
  "displayName": "Branch",
  "family": "branch",
  "profile": "semantic_family_represented",
  "mode": "workflow_map",
  "fixture": null,
  "recipe": null,
  "focus": {
    "workflowFamilies": ["blueprint_local_control_flow"]
  },
  "status": "workflow_only"
}
```

## What the First Version Should Not Include

Do not add these yet:

- generated Python code
- execution history
- owner fields
- priority fields
- estimated cost
- family-level summary breakdowns
- workflow coverage reports embedded into the same file

Those can be added later as separate derived views if needed.

## Relationship to Other Design Docs

- [GRAPH_TEST_FRAMEWORK.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FRAMEWORK.md)
- [GRAPH_TEST_PROFILES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_PROFILES.md)
- [GRAPH_TEST_RECIPES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_RECIPES.md)
- [GRAPH_TEST_FIXTURES.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_FIXTURES.md)
- [GRAPH_TEST_CATALOG_SCHEMA.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_CATALOG_SCHEMA.md)
- [GRAPH_TEST_ROADMAP.md](/Users/xartest/dev/loomle/tests/GRAPH_TEST_ROADMAP.md)
