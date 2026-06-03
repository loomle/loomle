---
layout: default
title: Add a Blueprint Node
parent: Workflows
nav_order: 1
---

# Add a Blueprint Node

Recommended sequence:

1. `blueprint_graph_inspect` to find the target graph context.
2. `blueprint_graph_palette` to find the UE Action Menu entry.
3. `schema_inspect` for `blueprint_graph_edit` operation `addFromPalette`.
4. `blueprint_graph_edit` with the selected palette entry.
5. `blueprint_graph_layout` if the edited region needs formatting.
6. `blueprint_compile` after meaningful changes.

Do not guess a K2 node class when a palette entry is available.
