---
layout: default
title: Add Widget Text
parent: Workflows
nav_order: 5
---

# Add Widget Text

Recommended sequence:

1. `widget_tree_inspect` to find the current WidgetTree.
2. `widget_palette` to find `TextBlock` or another widget class.
3. `schema_inspect` for `widget_tree_edit` operation `addFromPalette`.
4. `widget_tree_edit` to add the widget under the chosen parent.
5. `widget_inspect` for the widget instance before setting properties.
6. `widget_tree_edit` to set widget or slot properties.
7. `widget_compile`.

Use WidgetTree tools for hierarchy changes. Use inspect results to choose
property names and slot property paths.
