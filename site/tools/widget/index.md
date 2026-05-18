---
layout: default
title: Widget
parent: Tools
nav_order: 4
---

# Widget Tools

Widget tools operate on UMG WidgetBlueprint assets.

- `widget.palette`: find UMG widgets that can be added.
- `widget.tree.inspect`: inspect the WidgetTree hierarchy.
- `widget.tree.edit`: apply explicit WidgetTree edits.
- `widget.inspect`: inspect one widget class or WidgetTree instance.
- `widget.compile`: compile a WidgetBlueprint and return diagnostics.

Use `widget.palette` before adding widgets, and `widget.tree.inspect` before
editing the WidgetTree hierarchy.

## Recommended Flow

1. Inspect the tree with `widget.tree.inspect`.
2. Use `widget.palette` when adding a widget.
3. Use `schema.inspect` before `widget.tree.edit` commands.
4. Use `widget.inspect` before editing widget or slot properties.
5. Compile after meaningful hierarchy or property changes.

## Boundary

Tree tools own hierarchy changes, reparenting, removal, and tree-level edits.

`widget.inspect` explains widget classes and instances. It is the starting
point before property edits.
