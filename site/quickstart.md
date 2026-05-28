---
layout: default
title: Quickstart
nav_order: 3
description: A minimal path from install to first successful UE operation.
---

# Quickstart

This path assumes one of these install paths is ready:

- Native install is configured in your MCP host as `loomle mcp`.
- Fab install has configured the Python MCP server from the Loomle toolbar in
  Unreal Editor.

After setup, both paths expose the same LOOMLE tools.

## 1. Open Unreal Editor

Open the Unreal project you want the agent to work on. Make sure the
`LoomleBridge` plugin is enabled and the Loomle toolbar/status panel reports a
ready state.

If you just changed MCP setup, restart Codex, Claude, or your MCP host before
continuing.

## 2. Find an Unreal Project

Call:

```text
project.list
```

Use the returned `projectId` or `projectRoot` for the next step.

## 3. Attach the Session

Call:

```text
project.attach
```

After attach succeeds, LOOMLE exposes the UE-facing tool surface for the active
project.

## 4. Read Editor Context

If the user already has an asset open or something selected in Unreal Editor,
call:

```text
context
```

Use it to discover the active editor asset, active window, current selection,
and resolved graph references. It is the fastest way to start from the user's
visible UE state instead of guessing asset paths or graph names.

`context` does not attach the MCP session to a project. Use it after
`project.attach` succeeds.

## 5. Inspect Before Editing

Use the domain-specific inspect tool before editing:

- Blueprint: `blueprint.inspect`, `blueprint.graph.inspect`,
  `blueprint.member.inspect`, or `blueprint.node.inspect`.
- Material: `material.graph.inspect` or `material.node.inspect`.
- PCG: `pcg.graph.inspect`, `pcg.node.inspect`, or `pcg.parameter.inspect`.
- Widget: `widget.tree.inspect` or `widget.inspect`.

## 6. Use Palettes for Creation

When adding graph nodes or widgets, query the relevant palette first:

- `blueprint.palette`
- `material.palette`
- `pcg.palette`
- `widget.palette`

Then pass the selected palette entry to the matching edit tool.

## 7. Edit Explicitly

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

## 8. Compile or Verify

Compile after meaningful asset changes:

- `blueprint.compile`
- `material.compile`
- `pcg.compile`
- `widget.compile`
