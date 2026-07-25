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
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

query door
variables "Health"
```

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

query door
component Mesh
with schema
```

Exact-name reads discover current ids. Later requests use Target-relative
stable references inside the complete Blueprint Target:

```sal
@identity
```

All one-segment Blueprint categories share this identity environment. Optional
tags such as `variable @identity` are erasable presentation and do not
disambiguate a collision.

Creation begins with the combined Blueprint Palette. Copy the ordinary brace
object it returns:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door dry run
door.Health = {
  palette: "variable-palette-id",
  type: "<native FEdGraphPinType text>"
}
add door.Health
```

Existing objects support schema-authorized operations:

```sal
set
reset
move
remove
invoke
```

Component hierarchy changes retain SCS semantics; use the exact Component
schema rather than guessing which fields or operations apply.

Graph lifecycle belongs here, but Nodes, Pins, and Edges belong to the
[Graph](graph.html) interface.
