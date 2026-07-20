---
layout: default
title: Quickstart
nav_order: 3
description: Go from the Fab-installed Client to a first SAL query and dry-run patch.
---

# Quickstart

This guide assumes Loomle is installed from Fab, `LoomleBridge` is active in
the target Unreal project, and the MCP host uses the bundled Client.

Loomle 0.7 is not public yet; this workflow applies to the accepted QA build
and to the matching Fab package after it is published.

## 1. Start From the Editor

Open the asset you want to discuss, then call:

```text
editor_context({})
```

The result is ordinary ordered SAL Object Text. It may identify an active
asset, Graph, selected Node, Widget, Actor, or the focused editor surface. Use
the returned locator instead of guessing from what happens to be visible.

## 2. Discover the Interface

Call the local schema index:

```text
sal_schema({})
```

Then load only the relevant interface card, for example:

```text
sal_schema({ module: "blueprint" })
```

The `sal_schema` tool description already contains the compact resident SAL
guide. Module calls are for exact domain details, not a prerequisite before
every request.

## 3. Read a Blueprint Summary

Pass one self-contained SAL Text to `sal_query`:

```sal
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

A first path-based query returns the Blueprint id for later stable access plus
compact counts for Variables, Dispatchers, Graphs, and Components. It does not
download the full Blueprint.

Use a discovered id in later exact requests:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

query door
graphs "Event"
```

## 4. Follow a Graph Locally

Bind the owner chain returned by the previous query:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
exec flow from node@node-guid depth 2
```

Flow queries return compact Nodes and only the Pins needed to describe the
Edges. For every Pin on one Node, query that exact Node instead.

## 5. Discover Before Creating

Never guess a Node constructor. Query the target Graph Palette:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette entries "Print String"
```

Inspect the selected entry with `palette @id` and `with schema`, then copy its
returned constructor into a Patch.

## 6. Dry Run, Then Apply

Use the complete owner binding and set `dry run` on the Patch header:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
print = node(palette: "palette-entry-id")
add print
```

Dry run parses, resolves, validates, and plans through the real edit path
without changing UE state. If the result is valid, send the same Patch without
`dry run`.

## 7. Finalize Through the Owner

Graph edits do not compile or save automatically. Finalize the exact owning
Blueprint in a separate terminal Patch:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```

Compiler messages and mutation diagnostics return as SAL comments beside the
relevant Object Text. Continue with the [interface overview](tools/).
