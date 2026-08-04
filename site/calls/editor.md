---
layout: default
title: Editor
parent: MCP Calls
nav_order: 6
description: Read current Unreal Editor context or control an exact Blueprint Editor or Graph document.
---

# Editor

`editor` observes or controls Unreal's presentation of Blueprints and Blueprint
Graphs. It is a focused semantic interface over UE's native Blueprint Editor
and document APIs, not a generic window manager.

## Read Current Context

Call with no arguments, or spell the operation explicitly:

```text
editor({})
editor({ operation: "context" })
```

Context translates the user's current meaningful Unreal interaction into
canonical SAL Result Text. Its Target table carries canonical Domain Targets,
and its optional `objects` section carries ordered Object Text.

It is designed for conversational handoff. The user can focus a Graph, select
a Node or Widget, or click a Details panel and ask an agent to continue from
that state. Use the returned Target instead of guessing an Asset Path or Graph
identity from what appears visible.

## Open or Close a Blueprint Presentation

`open` and `close` require one bare canonical SAL Blueprint or Graph Target
expression encoded as the `target` string:

```text
editor({
  operation: "open",
  target: "target { domain: graph, asset: \"/Game/BP_Door.BP_Door\", blueprintId: \"11111111-1111-1111-1111-111111111111\", id: \"22222222-2222-2222-2222-222222222222\" }"
})
```

A Blueprint Target denotes its whole Blueprint Editor. A Graph Target denotes
the exact Graph document inside the owning Blueprint Editor. `open` ensures the
presentation is open and focused; `close` ensures it is closed. Both operations
are idempotent and do not expose `dryRun` because they change transient Editor
presentation rather than authored UObject state.

Target Text must be canonical. It cannot be a Target binding, Query, Patch,
Result Text, discovery-by-name Target, or another Domain.

## Results

The first response block is validated canonical SAL Result Text. Successful
open and close calls add a second `Editor result` block with `operation` and a
terminal `status`; diagnostics, when present, use a third block.

Closing a presentation does not delete its content, so a verified close still
returns `result exact_target`. If the content Target resolves but UE blocks or
vetoes the presentation change, Loomle retains that exact Target, reports
`status: failed`, and returns a diagnostic.

Context does not create persistent SAL aliases and does not replace the
session's `project` binding. Copy a returned canonical Target into each
following `sal_query`, `sal_patch`, or `editor` request.
