---
layout: default
title: Add Widget Text
parent: Workflows
nav_order: 5
---

# Add Widget Text

Recommended sequence:

1. `widget.tree.inspect` to find the current WidgetTree.
2. `widget.palette` to find `TextBlock` or another widget class.
3. `schema.inspect` for `widget.tree.edit` operation `addFromPalette`.
4. `widget.tree.edit` to add the widget under the chosen parent.
5. `widget.inspect` for the widget instance before setting properties.
6. `widget.tree.edit` to set widget or slot properties.
7. `widget.compile`.

Use WidgetTree tools for hierarchy changes. Use inspect results to choose
property names and slot property paths.
