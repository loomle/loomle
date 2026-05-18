---
layout: default
title: Add a Blueprint Node
parent: Workflows
nav_order: 1
---

# Add a Blueprint Node

Recommended sequence:

1. `blueprint.graph.inspect` to find the target graph context.
2. `blueprint.palette` to find the UE Action Menu entry.
3. `schema.inspect` for `blueprint.graph.edit` operation `addFromPalette`.
4. `blueprint.graph.edit` with the selected palette entry.
5. `blueprint.graph.layout` if the edited region needs formatting.
6. `blueprint.compile` after meaningful changes.

Do not guess a K2 node class when a palette entry is available.
