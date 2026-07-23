---
layout: default
title: Blueprint
parent: Interfaces
nav_order: 2
has_children: true
---

# Blueprint

The Blueprint interface owns Class Settings, Variables, Dispatchers, top-level
Graph lifecycle, SCS Components, compile, and save. Graph bodies and Widget
trees use their own modules while retaining the exact Blueprint owner.

## Target

The first discovery query may use only the Asset Path:

```sal
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

The result returns `BlueprintGuid`. Later exact queries and every Patch use the
path and id together:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)
```

The path loads the asset; the Guid verifies its identity.

## Query Directory

```text
summary
variables ["text"]
dispatchers ["text"]
graphs ["text"]
components ["text"]
variable <name> | variable@id
dispatcher <name> | dispatcher@id
graph <name> | graph@id
component <name> | component@id
references to <typed-ref>[.<native-member-path>] [in project]
palette entries ["text"] | palette @id
```

Collections are compact, cursor-paginated, and preserve UE authored order by
default. Exact reads may add `with schema` for current writable fields,
constraints, reset behavior, lifecycle, and UE operations.

## Patch Boundary

Blueprint declarations, Graph lifecycle, Class Settings, and SCS Components
may share one ordered Blueprint Patch. Graph-body edits and Widget-tree edits
belong to their respective planners and use following requests.

Creation constructors always come from the target's Palette. Existing objects
use typed references:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door dry run
set door.BlueprintDescription = "Interactive door"
set variable@variable-guid.NativeField = value
move component@component-guid to component@parent-guid
```

See [Blueprint Objects and Components](members.html), [Palette](palette.html),
and the installed Blueprint interface card for exact forms.

## Finalize

Compilation and save are a separate terminal Patch:

```sal
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```

Do not mix finalization with authored source mutations. Compile always targets
the whole Blueprint, never one Graph.
