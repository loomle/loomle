---
layout: default
title: Editor
parent: Tools
nav_order: 4
---

# Editor Tools

Editor tools operate on Unreal Editor windows and panels. They do not mutate UE
asset data by themselves.

## Tool List

- `editor.open`: open or focus an Unreal asset editor.
- `editor.focus`: focus a semantic panel in an asset editor.
- `editor.screenshot`: capture the active editor window to a PNG file.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `editor.open` | `assetPath` | Unreal asset path to open. |
| `editor.focus` | `assetPath`, `panel` | `panel` is a semantic editor panel such as graph, viewport, details, palette, or find. |
| `editor.screenshot` | none | Optional `path` for the PNG output location. |

Use `editor.open` before editor-context-dependent work when the relevant asset
is not already visible. Use `context` after focusing to confirm the active
editor state.
