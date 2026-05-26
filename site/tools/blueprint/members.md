---
layout: default
title: Blueprint Members
parent: Blueprint
nav_order: 4
---

# Blueprint Members

Blueprint members are asset-owned definitions, not graph wiring. Use member
tools for variables, functions, macros, dispatchers, custom events, and
components.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `blueprint.member.inspect` | Inspect Blueprint-owned members. |
| `blueprint.member.edit` | Edit Blueprint-owned members. |

## `blueprint.member.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `memberKind` | yes | `variable`, `function`, `macro`, `dispatcher`, `event`, `customEvent`, or `component`. |
| `name` | no | Exact member name filter. |

Use this before editing members, and when deciding whether an operation belongs
to member tools or graph tools.

## `blueprint.member.edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `memberKind` | yes | Same member kind set as inspect. |
| `operation` | yes | Operation within the member kind. |
| `args` | yes | Operation-specific arguments from `schema.inspect`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Call `schema.inspect` with `domain: blueprint`, `tool: blueprint.member.edit`,
and `operation: <memberKind>.<operation>` before editing.

For inherited native Blueprint events/functions, use `memberKind: "function"`
with `operation: "override"` instead of `function.create`. Override graphs are
created from UE's inherited function signature so native dispatch reaches the
Blueprint implementation.

## Boundary

Use member tools for Blueprint-owned definitions. Use graph tools only for graph
placement, pins, links, node defaults, comments, and layout.
