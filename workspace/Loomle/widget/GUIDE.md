# Widget Guide

This is the primary entrypoint for UMG widget tree work inside the LOOMLE
workspace.

Use this file first. Open `SEMANTICS.md` when you need deeper guidance about
widget tree structure, panel vs leaf widgets, or slot properties.

## Core Loop

1. Read the current widget tree with `widget.query`.
2. Use `widget.describe` to discover what properties a widget class exposes
   before setting them.
3. Plan the edit: determine which widgets to add, remove, reparent, or modify.
4. Apply changes with `widget.mutate` using explicit ops: `addWidget`,
   `removeWidget`, `setProperty`, or `reparentWidget`.
5. Run `widget.verify` to compile and confirm no blueprint errors.
6. Re-query when you need exact proof of the updated widget tree.

## First Checks

- confirm the target WidgetBlueprint asset path (e.g. `/Game/UI/MyWidget`)
- confirm the target widget by name in the tree before mutating
- confirm the target parent is a panel widget (Canvas, Overlay, HorizontalBox,
  VerticalBox, etc.) when adding or reparenting
- use `includeSlotProperties=true` in `widget.query` when slot layout matters
- use `widget.describe` when you are unsure what property names a widget type
  accepts, or to read the current values on a live instance

## Primary Tool Contracts

### `widget.query`

- `assetPath` (required): the WidgetBlueprint asset path
- `includeSlotProperties` (optional, default false): include slot layout data
- Returns `rootWidget` (tree), `revision`, `diagnostics`

### `widget.describe`

- `widgetClass` (optional): short name (`TextBlock`) or full path
  (`/Script/UMG.TextBlock`)
- `assetPath` + `widgetName` (optional): resolve from a live instance in the
  WidgetTree; also returns `currentValues` (current property values)
- At least one of the above must be provided
- Returns `widgetClass` (full path), `properties[]` (name/type/category/writable),
  `slotProperties[]`, and optionally `currentValues`

Property names in `properties[]` map directly to the `property` field in
`widget.mutate setProperty` — what you can describe, you can set.

### `widget.mutate`

- `assetPath` (required)
- `ops` (required): ordered list of mutation operations
- `expectedRevision` (optional): optimistic concurrency guard
- `dryRun` (optional): validate op names without touching the asset
- `continueOnError` (optional): continue remaining ops after a failure

Supported ops:

| op | required args | notes |
|---|---|---|
| `addWidget` | `widgetClass`, `name`, `parentName` | `slot` object optional; `parent` accepted as legacy alias |
| `removeWidget` | `name` | removes widget and its children |
| `setProperty` | `name`, `property`, `value` | `value` is always a string; use `widget.describe` to discover valid property names |
| `reparentWidget` | `name`, `newParent` | `slot` object optional |

### `widget.verify`

- `assetPath` (required)
- Returns `status` (`ok` or `error`), `diagnostics`

## Execution Style

Prefer small, targeted ops. Widget names must match exactly as returned by
`widget.query`. Widget classes use short UE class names (e.g. `TextBlock`,
`Button`, `CanvasPanel`, `Image`).

When in doubt about a property name or its accepted value format, call
`widget.describe` first.

## Validation Style

Minimum widget validation should confirm:

- the new widget appears in the tree at the expected parent
- removed widgets are gone from the tree
- `widget.verify` returns `status: ok`
