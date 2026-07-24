---
layout: default
title: Quickstart
nav_order: 3
description: Check status, bind a project, inspect a Blueprint, dry-run a SAL patch, apply it, and finalize the asset.
---

# Quickstart

This guide completes one read-and-edit workflow. It assumes that the matching
Loomle 0.7 plugin is installed, `LoomleBridge` is enabled, the MCP host launches
the bundled Client, and the target Unreal project is open.

{: .note }
> Paths and ids below are examples. Always copy Asset Paths, typed ids, Palette
> entries, and invocation templates returned by the current project.

## 1. Check Status

Call once before the first Loomle operation:

```text
status({})
```

This identifies the running Client, checks for an update, and reports the
current session and Bridge health. An unavailable update check does not block
local Loomle work.

## 2. Bind One Project

If Status reports an unbound session, inspect the current session and known
projects:

```text
project({})
```

If Loomle reports no bound project or more than one candidate, bind the desired
one:

```text
project({ projectId: "<returned-project-id>" })
```

The binding is session-local and sticky. If the Editor restarts or the project
goes offline, Loomle preserves the same project intent.

## 3. Start From the Editor

Open or select the asset you want to discuss, then call:

```text
editor_context({})
```

The result is ordered SAL Object Text. It may identify an active asset, Graph,
selected Node, Widget, Actor, or the focused editor surface. Use the returned
locator instead of guessing from the visible UI.

## 4. Discover the Interface

List the active interface modules:

```text
sal_schema({})
```

Load the Blueprint card when exact domain syntax is unfamiliar:

```text
sal_schema({ module: "blueprint" })
```

The `sal_schema` tool description already carries the compact resident SAL
guide. Static module cards are for exact domain boundaries; they are not a
prerequisite before every request.

## 5. Read a Blueprint Summary

Send one self-contained Query Text to `sal_query`:

```text
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

The first path-based query returns the Blueprint id and compact counts for
Variables, Dispatchers, Graphs, and Components. It does not download the
complete Blueprint.

Use the returned id in later exact requests:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)

query door
graphs "Event"
```

Every Query and Patch supplies its own complete target binding. A typed id is a
stable selector inside that owner scope, not a global target.

## 6. Follow the Graph Locally

Bind the exact Graph returned by the Blueprint query:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)
eventGraph = graph(asset: door, id: "returned-graph-guid")

query eventGraph
exec flow from node@returned-node-guid depth 2
```

Flow queries return compact Nodes and only the Pins needed to express the
Edges. Query an exact Node reference when an operation needs every current Pin
or dynamic schema.

## 7. Discover Before Creating

Never guess a Node constructor. Search the target Graph Palette:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)
eventGraph = graph(asset: door, id: "returned-graph-guid")

query eventGraph
palette entries "Print String"
```

Inspect the selected entry with exact schema:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)
eventGraph = graph(asset: door, id: "returned-graph-guid")

query eventGraph
palette @returned-palette-entry-id
with schema
```

Then copy its returned constructor into a Patch.

## 8. Dry Run

Send the complete Patch Text to `sal_patch` with dry-run state on the Patch
header:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)
eventGraph = graph(asset: door, id: "returned-graph-guid")

patch eventGraph dry run
print = node(palette: "returned-palette-entry-id")
add print
```

Dry run parses, resolves, validates, and plans through the real edit path
without changing live authored state. Review the returned diagnostics,
resolved references, operations, effects, and diff.

## 9. Apply and Read Back

If the dry run is valid, send the same authored Patch again with dry-run state
removed from the header. Afterward, query the affected Graph or exact Node
again rather than assuming that the intended result was applied.

## 10. Finalize Through the Owner

Graph edits do not compile or save their owning Blueprint automatically.
Finalize in a separate terminal Patch:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "returned-blueprint-guid"
)

patch door
compile
save
```

Compiler messages and mutation diagnostics return as SAL comments beside the
relevant Object Text.

You have now completed the standard Loomle loop:

```text
status → bind → locate → inspect → discover → dry run → apply → read back → finalize
```

Continue with [Core Concepts](concepts/) or browse the
[Interfaces](tools/).
