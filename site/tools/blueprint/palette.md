---
layout: default
title: Blueprint Palette
parent: Blueprint
nav_order: 1
---

# Blueprint Palette

`blueprint_graph_palette` searches UE Blueprint Action Menu entries for graph node
creation. It keeps agents aligned with UE's own context-sensitive creation
model.

## Recommended Flow

1. Inspect or list the target graph.
2. Call `blueprint_graph_palette` with `assetPath`, `graph`, and query text.
3. Select an executable entry.
4. Pass the full entry to `blueprint_graph_edit` with `addFromPalette`.

Do not guess K2 node classes for normal creation.

## `blueprint_graph_palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | no | Preferred graph address: `{id}` or `{name}`. |
| `graphName` | no | Legacy graph address. Prefer `graph`. |
| `query` | no | Search text. |
| `contextSensitive` | no | Defaults to true. |
| `fromPins` | no | Pin context for pin-drag menu behavior. |
| `limit` | no | Defaults to 50. |
| `offset` | no | Paging offset. |

## Returned Entries

Palette entries are context-bound. Use them in the same Blueprint, graph, and
pin context used for the search.

Some entries describe schema actions or context that cannot be spawned
directly. `blueprint_graph_edit` rejects non-executable entries with a
structured error. Choose an executable entry for `addFromPalette`.
