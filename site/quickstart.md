---
layout: default
title: Quickstart
nav_order: 3
description: A minimal path from install to first successful UE operation.
---

# Quickstart

This path assumes LOOMLE is installed and the MCP host is configured to run
`loomle mcp`.

## 1. Find an Unreal Project

Call:

```text
project.list
```

Use the returned `projectId` or `projectRoot` for the next step.

## 2. Attach the Session

Call:

```text
project.attach
```

After attach succeeds, LOOMLE exposes the UE-facing tool surface for the active
project.

## 3. Inspect Before Editing

Use the domain-specific inspect tool before editing:

- Blueprint: `blueprint.inspect`, `blueprint.graph.inspect`,
  `blueprint.member.inspect`, or `blueprint.node.inspect`.
- Material: `material.graph.inspect` or `material.node.inspect`.
- PCG: `pcg.graph.inspect`, `pcg.node.inspect`, or `pcg.parameter.inspect`.
- Widget: `widget.tree.inspect` or `widget.inspect`.

## 4. Use Palettes for Creation

When adding graph nodes or widgets, query the relevant palette first:

- `blueprint.palette`
- `material.palette`
- `pcg.palette`
- `widget.palette`

Then pass the selected palette entry to the matching edit tool.

## 5. Edit Explicitly

Use explicit local edits:

- `blueprint.graph.edit`
- `blueprint.member.edit`
- `blueprint.node.edit`
- `material.graph.edit`
- `material.node.edit`
- `pcg.graph.edit`
- `pcg.parameter.edit`
- `widget.tree.edit`

For tools with compact first-level schemas, call `schema.inspect` for the
specific operation before editing.

## 6. Compile or Verify

Compile after meaningful asset changes:

- `blueprint.compile`
- `material.compile`
- `pcg.compile`
- `widget.compile`
