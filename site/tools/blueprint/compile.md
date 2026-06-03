---
layout: default
title: Blueprint Compile
parent: Blueprint
nav_order: 6
---

# Blueprint Compile

Use `blueprint_compile` after meaningful Blueprint changes. Compile diagnostics
should guide the next inspect or edit step.

## `blueprint_compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | no | Optional graph address. Prefer `{id}` or `{name}`. |
| `graphName` | no | Legacy graph address. Prefer `graph`. |
| `dryRun` | no | Validate without applying when supported. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

## Compile After

- Adding, removing, or reconnecting graph nodes.
- Changing pin defaults.
- Editing Blueprint members.
- Editing node-local structure such as switch cases or Format Text arguments.
- Changing parent class or interfaces.

## Next Step

If compile returns diagnostics, inspect the relevant asset, graph, member, or
node before applying another edit.
