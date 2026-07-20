---
layout: default
title: Exact Nodes and Pins
parent: Blueprint
nav_order: 4
---

# Exact Nodes and Pins

Use exact reads when a flow traversal intentionally omitted nonessential Pins
or when an operation depends on the current Node state:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
node@node-guid
with schema
```

An exact Node returns all current Pins, native fields, and adjacent health
comments. An exact Pin returns its compact owner and complete Pin without
traversing links.

Nodes may own complex internal UE state—dynamic Pins, switch cases, timeline
tracks and keys, delegate state, and other schema-specific behavior. Loomle
does not split those into artificial top-level objects. Exact `with schema`
returns the operations the resolved Node or Pin can execute now:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
# Example only when exact schema advertises this operation.
invoke node@node-guid AddExecutionPin() as addedPin
```

Copy the returned invocation template. Do not infer an operation name from a
similar Node type.
