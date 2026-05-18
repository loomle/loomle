---
layout: default
title: Editor
parent: Tools
nav_order: 4
---

# Editor Tools

Editor tools operate on Unreal Editor windows and panels. They help agents get
the editor into the right visible state, but they do not mutate asset data by
themselves.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `editor.open` | Open or focus an Unreal asset editor. |
| `editor.focus` | Focus a semantic panel inside an asset editor. |
| `editor.screenshot` | Capture the active editor window to a PNG file. |

## `editor.open`

Open or focus the editor for a specific asset.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Unreal asset path to open. |

### Next Step

Call `context` after opening if the next operation depends on the active editor
or current selection.

## `editor.focus`

Focuses a semantic panel inside an asset editor.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Asset whose editor should receive focus. |
| `panel` | yes | Semantic panel name, such as graph, viewport, details, palette, or find. |

### Boundary

Use focus tools for editor navigation only. Use domain tools for asset data
changes.

## `editor.screenshot`

Captures the active editor window.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `path` | no | Optional PNG output path. |

### Use When

Use screenshots for visual confirmation, UI debugging, or when a task requires
evidence from the visible editor state.
