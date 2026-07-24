---
layout: default
title: Add a Blueprint Node
parent: Workflows
nav_order: 1
---

# Add a Blueprint Node

First bind the exact Blueprint and Graph returned by discovery:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette entries "Print String"
```

Read the selected Palette Entry with exact schema:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette @palette-entry-id
with schema
```

Copy the returned constructor into a dry run:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
print = node(palette: "palette-entry-id")
add print
```

If the Node belongs on an existing execution edge, use the exact Pins returned
by Graph queries and the Graph operation matching the intended topology. A
normal addition may connect one side; two-sided replacement uses the Graph
insert operation.

Apply the authored Graph Patch with dry-run state removed, then finalize
separately:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```
