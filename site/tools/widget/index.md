---
layout: default
title: Widget
parent: Tools
nav_order: 9
---

# Widget Tools

Widget tools operate on UMG WidgetBlueprint assets. Use the palette for widget
creation, tree tools for hierarchy edits, and `widget.inspect` before property
edits.

## Recommended Flow

1. Inspect the tree with `widget.tree.inspect`.
2. Use `widget.palette` when adding a widget.
3. Use `schema.inspect` before `widget.tree.edit` commands.
4. Use `widget.inspect` before editing widget or slot properties.
5. Run `widget.compile` after hierarchy or property changes.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `widget.palette` | Search UE Widget Palette entries for UMG widget creation. |
| `widget.tree.inspect` | Inspect a WidgetBlueprint WidgetTree. |
| `widget.tree.edit` | Apply explicit WidgetTree edit commands. |
| `widget.inspect` | Inspect one widget class or WidgetTree instance. |
| `widget.compile` | Compile a WidgetBlueprint and return diagnostics. |

## `widget.palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | Optional WidgetBlueprint asset path. |
| `query` | no | Fuzzy search over label, category, tooltip, keywords, and payload. |
| `elementTypes` | no | `native` or `user`. |
| `limit` | no | Defaults to 50, maximum 500. |
| `offset` | no | Paging offset. |

Pass the selected palette entry to `widget.tree.edit` rather than guessing
widget classes.

## `widget.tree.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | WidgetBlueprint asset path. |
| `view` | no | `outline`, `layout`, or `details`; defaults to `outline`. |
| `filter.names` | no | Exact widget names for matches/details. |
| `filter.text` | no | Case-insensitive fuzzy search over serialized tree entries. |

Use `outline` first. Use `layout` when slot/layout data matters, and `details`
when targeting specific widgets.

## `widget.tree.edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | WidgetBlueprint asset path. |
| `commands` | yes | Ordered command envelopes with `kind`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Command-specific fields are intentionally omitted from `tools/list`. Call
`schema.inspect` with `domain: widget`, `tool: widget.tree.edit`, and the
selected `operation`.

## `widget.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `widgetClass` | no | Widget class path or short class name, such as `TextBlock`. |
| `assetPath` | no | WidgetBlueprint asset path for instance mode. |
| `widget.name` | no | WidgetTree instance name for instance mode. |

Use class mode before adding or understanding a widget type. Use instance mode
before setting widget or slot properties.

## `widget.compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | WidgetBlueprint asset path. |

Compile after meaningful hierarchy or property changes.
