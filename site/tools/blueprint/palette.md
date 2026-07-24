---
layout: default
title: Palette
parent: Blueprint
grand_parent: Interfaces
nav_order: 5
---

# Palette

Every object created directly through an add operation begins with a UE Palette
result. Loomle does not define a fixed list of constructors or ask the agent to
guess native classes.

Search a Blueprint or Graph target:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette entries "Print String"
```

Graph Palette search may include Pin context:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette entries "Branch" from pin@source-pin-guid
```

Inspect the selected capability:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
palette @palette-entry-id
with schema
```

Then copy its returned binding into Patch Text:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")

patch eventGraph dry run
print = node(palette: "palette-entry-id")
add print
```

Patch re-resolves the Palette id in the current context before creation. Exact
Palette schema provides determinable future Pins, constructor arguments, and
constraints.
