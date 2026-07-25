---
layout: default
title: Exact Nodes and Pins
parent: Graph
grand_parent: Interfaces
nav_order: 1
---

# Exact Nodes and Pins

Use exact reads when a flow traversal intentionally omitted nonessential Pins
or when an operation depends on the current Node state:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
@node-guid
with schema
```

An exact Node returns all current Pins, native fields, and adjacent health
comments. An exact Pin returns its compact owner and complete Pin without
traversing links.

Nodes may own complex internal UE state—dynamic Pins, switch cases, timeline
tracks and keys, delegate state, and other schema-specific behavior. Loomle
does not split those into artificial top-level objects. Exact dynamic schema
returns the operations the resolved Node or Pin can execute now:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
# Example only when exact schema advertises this operation.
invoke @node-guid AddExecutionPin() as addedPin
```

Copy the returned invocation template. Do not infer an operation name from a
similar Node type.
