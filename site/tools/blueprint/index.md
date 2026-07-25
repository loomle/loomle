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
trees use independent Domain Targets. Cross-Domain navigation is returned as a
related Target plus an explicit handoff.

## Target

The first discovery query may use only the Asset Path:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}

query door
summary
```

The result returns `BlueprintGuid`. Later exact queries and every Patch use the
path and id together:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The path loads the asset; the Guid verifies its identity. `id` must be a
canonical lowercase, hyphenated, non-zero GUID.

## Query Directory

```sal
target
summary
variables ["text"]
dispatchers ["text"]
graphs ["text"]
components ["text"]
variable <name>
dispatcher <name>
graph <name>
component <name>
@identity
references to <exact-subject> [in project]
palette entries ["text"]
palette @id
```

Collections are compact, cursor-paginated, and preserve UE authored order by
default. Exact reads may request dynamic schema for current writable fields,
constraints, reset behavior, lifecycle, and UE operations.

## Patch Boundary

Blueprint declarations, Graph lifecycle, Class Settings, and SCS Components
may share one ordered Blueprint Patch. Graph-body edits and Widget-tree edits
belong to their respective Domains and use following requests.

Creation values always come from the Target's Palette. Existing objects use
Target-relative stable references; optional tags such as `variable` or
`component` do not participate in lookup:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door dry run
set door.BlueprintDescription = "Interactive door"
set @variable-guid.NativeField = value
move @component-guid to @parent-guid
```

See [Blueprint Objects and Components](members.html), [Palette](palette.html),
and the installed Blueprint interface card for exact forms.

## Finalize

Compilation and save are a separate terminal Patch:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

patch door
compile
save
```

Do not mix finalization with authored source mutations. Compile always targets
the whole Blueprint, never one Graph.
