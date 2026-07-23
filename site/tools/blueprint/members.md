---
layout: default
title: Blueprint Objects and Components
parent: Blueprint
grand_parent: Interfaces
nav_order: 2
---

# Blueprint Objects and Components

Blueprint Variables, Dispatchers, Graphs, and SCS Components are contained UE
objects, not one generic member abstraction. Query each native collection by
its own name:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

query door
variables "Health"
```

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

query door
component Mesh
with schema
```

Exact-name reads discover current ids. Later requests use typed references such
as `variable@id`, `dispatcher@id`, `graph@id`, and `component@id` inside the
complete Blueprint target.

Creation begins with the combined Blueprint Palette. Copy the constructor it
returns:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door dry run
door.Health = variable(
  palette: "variable-palette-id",
  type: "<native FEdGraphPinType text>"
)
add door.Health
```

Existing objects support schema-authorized `set`, `reset`, `move`, `remove`,
and `invoke`. Component hierarchy changes retain SCS semantics; use the exact
Component schema rather than guessing which fields or operations apply.

Graph lifecycle belongs here, but Nodes, Pins, and Edges belong to the
[Graph](graph.html) interface.
