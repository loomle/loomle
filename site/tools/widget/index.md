---
layout: default
title: Widget
parent: Tools
nav_order: 9
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

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `widget.palette` | none | `assetPath`, `query`, `elementTypes: native/user`, `limit`, `offset` |
| `widget.tree.inspect` | `assetPath` | `view: outline/layout/details`, `filter.names`, `filter.text` |
| `widget.tree.edit` | `assetPath`, `commands` | Command envelopes with `kind`; command-specific args through `schema.inspect` |
| `widget.inspect` | none | Class mode: `widgetClass`; instance mode: `assetPath`, `widget.name` |
| `widget.compile` | `assetPath` | WidgetBlueprint asset path. |

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
