---
layout: default
title: Graph
parent: Interfaces
nav_order: 4
has_children: true
---

# Graph

Graph reads and edits are always scoped by an exact asset-backed owner:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")
```

## Query

Graph primary operations include:

```text
summary
nodes ["text"]
graph@id
node@id
pin@id
context node@id|pin@id [depth N]
exec flow from|to node@id|pin@id [depth N]
data flow from|to node@id|pin@id [depth N]
references to <typed-ref>[.<native-member-path>] [in project]
palette entries ["text"] [from|to pin@id]
palette @id
```

Traversal defaults to depth 1 and stays inside the target Graph. Add layout
detail where stored position matters:

```text
with layout
```

Exact Node reads return all current Pins; traversal returns only the Pins
necessary to express its Edges. The following modifier does not exist:

```text
with pins
```

A summary returns semantic entry Nodes, disconnected-region representatives,
counts, and an index of Nodes carrying native UE health state. Exact Node, Pin,
Graph, and Palette Entry reads may request dynamic schema.

## Patch

Palette creates the Node's native base Pins:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
delay = node(palette: "palette-entry-id")
add delay
```

Graph adds these operations to the Core Patch surface:

```text
connect
disconnect
break
insert
```

It also supports explicit Node movement and current UE operations:

```text
connect pin@source-id -> pin@target-id
break pin@id
move node@id to (640, 320)
invoke node@id <Operation>(<name>: <value>) [as <alias>]
```

Do not declare raw Pins. A normal addition may connect at most one side of a
new Node; use the insert operation for two-sided replacement. Exact dynamic
schema is authoritative for operation names and parameters.

Graph Patch does not compile or save its owning Blueprint. Finalize through a
separate exact Blueprint Patch.
