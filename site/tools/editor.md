---
layout: default
title: Editor Context
parent: Interfaces
nav_order: 1
---

# Editor Context

`editor_context({})` reads the user's current Unreal interaction target and
returns ordinary ordered SAL Object Text.

It is designed for conversational workflows: the user can select a Node, focus
a Graph, open a Widget Blueprint, or click a Details panel and ask the agent to
continue from that state. Loomle reports the focused surface together with the
host editor, active asset, active Graph, and resolvable selection when UE makes
them available.

Call it before guessing an asset path or Graph name:

```text
editor_context({})
```

The returned object is a discovery result, not hidden session binding. Copy a
complete locator into each following `sal_query` or `sal_patch` request.

Editor Context is the public handoff from the user's current editor state into
the self-contained SAL Query and Patch workflow.
