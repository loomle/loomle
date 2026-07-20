---
layout: default
title: Graph
parent: Blueprint
nav_order: 3
---

# Graph

Graph reads and edits are always scoped by an exact asset-backed owner:

```sal
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

Traversal defaults to depth 1 and stays inside the target Graph. Add
`with layout` where stored position matters. Exact Node reads return all
current Pins; traversal returns only the Pins necessary to express its Edges.
There is no `with pins` modifier.

`summary` returns semantic entry Nodes, disconnected-region representatives,
counts, and an index of Nodes carrying native UE health state. Exact Node, Pin,
Graph, and Palette Entry reads may add `with schema`.

## Patch

Palette creates the Node's native base Pins:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
delay = node(palette: "palette-entry-id")
add delay
```

Graph adds `connect`, `disconnect`, `break`, and `insert` to the Core Patch
operations. It also supports explicit Node movement and current UE operations:

```text
connect pin@source-id -> pin@target-id
break pin@id
move node@id to (640, 320)
invoke node@id <Operation>(<name>: <value>) [as <alias>]
```

Do not declare raw Pins. Use `add` for at most one side of a new Node and
`insert` for two-sided replacement. Exact dynamic schema is authoritative for
operation names and parameters.

Graph Patch does not compile or save its owning Blueprint. Finalize through a
separate exact Blueprint Patch.
