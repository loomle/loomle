---
layout: default
title: Widget
parent: Tools
nav_order: 9
---

# Widget Tools

Widget tools operate on UMG WidgetBlueprint assets. Use the palette for widget
creation, tree tools for hierarchy edits, and `widget_inspect` before property
edits.

## Recommended Flow

1. Inspect the tree with `widget_tree_inspect`.
2. Use `widget_palette` when adding a widget.
3. Use `schema_inspect` before `widget_tree_edit` commands.
4. Use `widget_inspect` before editing widget or slot properties.
5. Run `widget_compile` after hierarchy or property changes.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `widget_palette` | Search UE Widget Palette entries for UMG widget creation. |
| `widget_tree_inspect` | Inspect a WidgetBlueprint WidgetTree. |
| `widget_tree_edit` | Apply explicit WidgetTree edit commands. |
| `widget_inspect` | Inspect one widget class or WidgetTree instance. |
| `widget_compile` | Compile a WidgetBlueprint and return diagnostics. |

## `widget_palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | Optional WidgetBlueprint asset path. |
| `query` | no | Fuzzy search over label, category, tooltip, keywords, and payload. |
| `elementTypes` | no | `native` or `user`. |
| `limit` | no | Defaults to 50, maximum 500. |
| `offset` | no | Paging offset. |

Pass the selected palette entry to `widget_tree_edit` rather than guessing
widget classes.

## `widget_tree_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | WidgetBlueprint asset path. |
| `view` | no | `outline`, `layout`, or `details`; defaults to `outline`. |
| `filter.names` | no | Exact widget names for matches/details. |
| `filter.text` | no | Case-insensitive fuzzy search over serialized tree entries. |

Use `outline` first. Use `layout` when slot/layout data matters, and `details`
when targeting specific widgets.

## `widget_tree_edit`

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
`schema_inspect` with `domain: widget`, `tool: widget_tree_edit`, and the
selected `operation`.

## `widget_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `widgetClass` | no | Widget class path or short class name, such as `TextBlock`. |
| `assetPath` | no | WidgetBlueprint asset path for instance mode. |
| `widget.name` | no | WidgetTree instance name for instance mode. |

Use class mode before adding or understanding a widget type. Use instance mode
before setting widget or slot properties.

## `widget_compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | WidgetBlueprint asset path. |

Compile after meaningful hierarchy or property changes.
