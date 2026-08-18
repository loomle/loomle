---
layout: default
title: Level
parent: Interfaces
nav_order: 7
description: Inspect persistent source-map Actors and serialized Components through stable native identity.
---

# Level

The Level interface reads the authored contents of one saved source map. It is
Query-only in this release and does not represent the current Editor World,
PIE/SIE World, a streamed composition, or a temporary Level Instance World.

## Target

A discovery Query may omit `type`:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena"
}
```

The returned canonical exact Target retains the verified native World Class:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena",
  type: "/Script/Engine.World"
}
```

Target resolution uses Asset Registry evidence without loading or switching
the map. `target` is the only operation available while that exact source Level
is not already loaded in the authored Editor World.

## Identity and Query

An Actor uses its persistent native `ActorGuid`. A serialized Component uses
its owner ActorGuid, proved source kind, and source-specific slot id:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/RootComponent
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/instance/AudioComponent_0
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/scs/"/Game/Actors/BP_Enemy.BP_Enemy_C#bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"
```

The closed Component source kinds are `native`, `scs`, and `instance`.
Labels, object names, paths, array indices, and generated or local-partition
PCG objects are not stable identity.

```sal
target
summary
actors ["text"]
components ["text"]
@identity
palette entries ["text"] to <destination>
palette @id to <same-destination>
```

`actors` includes loaded persisted Actors and unloaded root World Partition
descriptors. `components` includes only uniquely proved serialized Components
of loaded Actors and never loads or pins an unloaded owner. Both collections
accept optional case-insensitive text search and cursor pagination; their
default page limit is 50 and maximum is 200. Exact reads accept no Query
clauses, and dynamic schema is unavailable.

## Palette Discovery

Palette discovery is destination-bound and read-only:

```sal
query arena
palette entries "Static Mesh" to arena.Actors

query arena
palette entries "Audio" to @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa.Components
```

`arena.Actors` names the currently bound Level Target and discovers Actor
creation capabilities for the exact loaded source map. An exact persisted
Actor `@actorGuid.Components` discovers instance Component creation
capabilities for that Actor. An exact replay re-resolves the same destination
and revalidates the opaque `palette` id before returning one entry:

```sal
query arena
palette @level.actor.<digest> to arena.Actors
```

Entries report the exact native Class, the editor category, and any required
source Asset type. Actor entries use the editor's placeable-capability
catalog; Component entries use the editor's instance-Component list and are
restricted to direct instance creation. Session state such as Favorites or
Recently Placed is never advertised. Creation Patch is not active in this
interface, so every entry reports `creation: unavailable` with the exact
capability reason; discovery never claims a creation capability the adapter
cannot execute. Ordering is canonical and fixed, and discovery is bounded by
the same 50/200 page limits as the collections.

## Related Targets

A recognized Level Instance placement may return its canonical source Level
Target with `handoff inspect_source_level`. A supported original authored
`UPCGComponent` may return an exact PCG Component Target with
`handoff inspect_pcg_component`.

Every successful exact Level Query also returns the source map's canonical
Asset Target with `handoff inspect_asset`. Loaded exact Actor or Component
reads return the actual native Class Target with `handoff inspect_class` and,
when
already-loaded evidence uniquely proves one generated Blueprint, its Blueprint
Target with `handoff inspect_blueprint`. These handoffs neither load their
targets nor grant save or compile authority.

## Read-only Boundary

Level Query never loads or pins Actors, switches maps or current Levels,
reruns construction, changes selection, registers Components, dirties a
package, or creates an Undo entry.

## Patch

`level` is a Patch Target for authored Actor and Component field edits:

```sal
patch arena [dry run]
set @actor-guid.bHidden = true
reset @actor-guid.bCanBeDamaged
```

Exact schema (`with schema`) advertises the scalar `set`/`reset` fields on a
loaded persisted Actor or Component; any other field, unloaded descriptor,
or unsupported statement fails closed. Edits are planned and applied inside
one top-level transaction with `Modify` and native post-edit notifications,
archetype/template reset sources, readback, and rollback on failure. A dry
run shares the plan without touching the live object.

Actor or Component lifecycle creation and removal, the transform invoke,
attachment, Palette creation Patch, save, live World control, and PIE/SIE
observation remain unavailable in this capability.
