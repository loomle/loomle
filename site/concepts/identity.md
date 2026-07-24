---
layout: default
title: Targets and Stable References
parent: Core Concepts
nav_order: 2
description: How Asset Paths, owner chains, typed ids, member paths, and aliases identify UE objects.
---

# Targets and Stable References

Loomle separates complete target identity from selectors inside that target.
This mirrors how Unreal owns editor objects.

## Asset-backed Targets

Assets without a native asset Guid use their exact Object Path:

```text
omle = asset(
  path: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
)
```

A Blueprint has both an Asset Path and a native Blueprint Guid. Its first
discovery query may use the path alone:

```text
door = blueprint(asset: "/Game/BP_Door.BP_Door")
```

Later exact queries and every Patch use the returned id:

```text
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
```

The path loads the asset; the Guid verifies that the resolved object is still
the intended Blueprint.

## Owner Chains

Contained UE objects are scoped by their owner. A Graph target therefore keeps
its Blueprint owner:

```text
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(asset: door, id: "graph-guid")
```

Neither scoped selector in the example is a complete global locator.

## Typed Stable References

Existing contained objects use typed selectors:

```text
blueprint@id
graph@id
node@id
pin@id
variable@id
component@id
widget@id
state@id
transition@id
```

Some interfaces define composite selectors:

```text
parameter@container-id/property-id
```

The active interface card is authoritative.

A selector without a type is invalid:

```text
@id
```

The type states what kind of object should be resolved and prevents unrelated
ids from being treated as interchangeable.

## Member Paths

Member paths select native fields or nested value surfaces:

```text
set widget@id.Slot.Padding = "<native FMargin text>"
references to variable@id.SomeNativeMember in project
```

Only paths advertised by the exact schema are writable. Do not infer a member
path from a display label.

## Local Aliases

Aliases are readable handles inside one SAL Text:

```text
print = node(palette: "palette-entry-id")
add print
```

They are not persisted session state. A later request binds its own target and
uses returned stable identities.

## Names Are for Discovery

Names and labels help search and navigation, but stable ids or exact paths
carry identity. Query a current name to discover the current id, then use that
typed id for exact follow-up work.
