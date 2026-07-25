---
layout: default
title: Palette
parent: Blueprint
grand_parent: Interfaces
nav_order: 5
---

# Palette

Every object created directly through an add operation begins with a UE Palette
result. Loomle does not define a fixed list of object kinds or ask the agent to
guess native classes.

Search a Blueprint or Graph target:

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

Graph Palette search may include Pin context:

```text
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query eventGraph
palette entries "Branch" from @source-node-guid/source-pin-guid
```

Inspect the selected capability:

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

Then copy its returned binding into Patch Text:

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

Patch re-resolves the Palette id in the current context before creation. Exact
Palette schema provides determinable future Pins, creation arguments, and
constraints. A formatter may emit an optional tag such as `node { ... }`, but
the tag is erasable and cannot select the object kind.
