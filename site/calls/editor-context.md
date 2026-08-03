---
layout: default
title: Editor Context
parent: MCP Calls
nav_order: 6
description: Read the user's current Unreal Editor interaction target as canonical SAL Result Text.
---

# Editor Context

`editor_context({})` translates the user's current Unreal interaction target
into canonical SAL Result Text. Its Target table carries canonical Domain
Targets, and its optional `objects` section carries ordered Object Text.

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
- a canonical Domain Target plus Target-relative StableRefs for follow-up work.

Use the returned Target instead of guessing an Asset Path or Graph identity
from what appears visible.

## Discovery, Not Hidden Binding

Editor Context does not create persistent SAL aliases and does not replace the
session's `project` binding. Copy the returned canonical exact Target—or the
Asset domain-root Target—into each following `sal_query` or `sal_patch`
request. An incomplete discovery Target belongs only in a new request; Result
Text does not present it as an exact Target.

If the current UI state cannot be mapped to a supported Domain Target, the result
uses `result unresolved_target` without inventing a Target. Any available
factual Object Text stays inside that canonical result, and at least one error
diagnostic is returned in a later independent MCP text block.
