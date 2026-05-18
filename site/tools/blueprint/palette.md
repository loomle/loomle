---
layout: default
title: Blueprint Palette
parent: Blueprint
nav_order: 1
---

# Blueprint Palette

`blueprint.palette` searches UE Blueprint Action Menu entries for graph node
creation.

Recommended flow:

1. Call `blueprint.palette` with the asset, graph, query text, and optional
   context.
2. Select an executable entry.
3. Pass the full entry to `blueprint.graph.edit` with `addFromPalette`.

Do not guess K2 node classes for normal creation.

## Why This Matters

UE node creation is contextual. The same search text can return different
actions depending on the Blueprint, graph, selected pins, and context-sensitive
filtering.

Using the palette keeps agents aligned with the same creation model the editor
uses.

## Non-Executable Entries

Some returned entries describe schema actions or context that cannot be spawned
directly. `blueprint.graph.edit` rejects those entries with a structured error.
Choose an executable entry for `addFromPalette`.
