---
layout: default
title: Editor Context
parent: MCP Calls
nav_order: 4
description: Read the user's current Unreal Editor interaction target as SAL Object Text.
---

# Editor Context

`editor_context({})` translates the user's current Unreal interaction target
into ordinary ordered SAL Object Text.

```text
editor_context({})
```

It is designed for conversational handoff. The user can open an asset, focus a
Graph, select a Node or Widget, or click a Details panel and ask an agent to
continue from that state.

When UE exposes the information, the result can include:

- focused editor surface;
- host asset editor;
- active asset;
- active Graph;
- selected objects; and
- locator-complete stable references for follow-up work.

Use the returned locator instead of guessing an Asset Path or Graph name from
what appears visible.

## Discovery, Not Hidden Binding

Editor Context does not create persistent SAL aliases and does not replace the
session's `project` binding. Copy the complete target or owner locator needed
by each following `sal_query` or `sal_patch` request.

If the current UI state cannot be resolved to a supported object, the result
still reports the factual editor surface and diagnostics available from UE
rather than inventing a target.
