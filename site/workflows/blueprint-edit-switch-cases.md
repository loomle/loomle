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
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
@switch-node-guid
with schema
```

The schema returns only UE operations available for that resolved Node,
including their exact parameters and a copyable invocation template. Use that
template in a dry run:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
invoke @switch-node-guid AddExecutionPin() as addedPin
```

Review the returned Pins and planned effects, then apply. The Graph result
supplies an independent related Blueprint Target and explicit compile handoff;
copy that returned Target into a separate terminal Patch to compile and save.
Never infer an operation from the display title of a similar switch Node. The
example applies only when exact schema advertises the operation shown above;
otherwise copy the operation template it actually returns.
