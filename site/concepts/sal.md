---
layout: default
title: SAL Working Model
parent: Core Concepts
nav_order: 1
description: How SAL Query Text, Patch Text, and ordered Object Text work together.
---

# SAL Working Model

SAL is Loomle's agent-facing, line-oriented language for reading and changing
Unreal Engine objects.

It is intentionally small. UE Asset Paths, Class Paths, types, field names,
values, Palette capabilities, and diagnostics remain native rather than being
translated into a generic replacement model.

## Three Text Forms

### Query Text

A Query binds a target and selects one primary read:

```sal
door = blueprint(asset: "/Game/BP_Door.BP_Door")

query door
summary
```

Optional clauses such as `where`, `with`, `order by`, `page`, and `depth`
depend on the selected interface and primary operation.

### Patch Text

A Patch binds its complete target and contains ordered bindings or operations:

```sal
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door dry run
set door.BlueprintDescription = "Interactive door"
```

Core operations include `add`, `remove`, `set`, `reset`, `move`, `invoke`, and
`save`. Interfaces add operations such as Graph `connect`, StateTree `bind`,
Widget `wrap`, and Blueprint `compile`.

### Object Text

Queries and Patches return the same ordered Object Text:

```sal
eventGraph = graph(id: "graph-guid")

beginPlay = node(
  graph: eventGraph,
  id: "node-guid",
  type: "/Script/BlueprintGraph.K2Node_Event"
)

beginPlay.then = pin(
  id: "pin-guid",
  direction: out,
  type: "<native FEdGraphPinType text>"
)
```

Object Text is self-contained. When a returned statement needs an owner, the
result declares the compact owner binding before using it.

## Comments Are Part of the Result

SAL comments carry counts, titles, health, schema, pagination guidance,
mutation plans, compiler messages, and diagnostics:

```sal
# variables: 3

###
complete validation or compiler detail
###
```

Comments keep execution information beside the object or operation it
describes. Loomle does not require a second diagnostics language.

## Every Request Is Self-contained

Aliases do not survive between tool calls. Copy the returned facts needed for
the next request and provide the complete owner locator again.

This boundary makes requests reviewable and prevents hidden conversational
state from retargeting an edit.

## Creation Starts From Palette

Do not guess constructors, classes, Pins, destinations, or operation
parameters. Search a Palette in the real target context:

```sal
query eventGraph
palette entries "Print String"
```

Inspect the exact entry when necessary, then copy its returned constructor into
Patch Text.

Use `sal_schema({ module: "<module>" })` for the exact grammar and clauses
supported by one interface.
