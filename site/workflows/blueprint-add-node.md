---
layout: default
title: Add a Blueprint Node
parent: Workflows
nav_order: 1
---

# Add a Blueprint Node

First bind the exact Graph returned by discovery:

```text
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette entries "Print String"
```

Read the selected Palette Entry with exact schema:

```text
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette @palette-entry-id
with schema
```

Copy the returned brace object fields into a dry run:

```text
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
print = { palette: "palette-entry-id" }
add print
```

If the Node belongs on an existing execution edge, use the exact Pins returned
by Graph queries and the Graph operation matching the intended topology. A
normal addition may connect one side; two-sided replacement uses the Graph
insert operation.

Apply the authored Graph Patch with dry-run state removed. Its result supplies
an independent related Blueprint Target and an explicit compile handoff. Copy
that returned Target into a separate finalization request:

```text
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door
compile
save
```
