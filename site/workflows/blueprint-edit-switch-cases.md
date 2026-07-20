---
layout: default
title: Edit Blueprint Switch Cases
parent: Workflows
nav_order: 2
---

# Edit Blueprint Switch Cases

Dynamic switch cases belong to the owning Node. Loomle does not expose a
second artificial Case object or ask the agent to manipulate raw Pins.

Read the exact switch Node and its current capabilities:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
node@switch-node-guid
with schema
```

The schema returns only UE operations available for that resolved Node,
including their exact parameters and a copyable `invoke` template. Use that
template in a dry run:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
invoke node@switch-node-guid AddExecutionPin() as addedPin
```

Review the returned Pins and planned effects, then apply. Finish by compiling
and saving the owning Blueprint in a separate terminal Patch. Never infer an
operation from the display title of a similar switch Node. The example applies
only when exact schema advertises `AddExecutionPin()`; otherwise copy the
operation template it actually returns.
