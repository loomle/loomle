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
