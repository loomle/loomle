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
```

`actors` includes loaded persisted Actors and unloaded root World Partition
descriptors. `components` includes only uniquely proved serialized Components
of loaded Actors and never loads or pins an unloaded owner. Both collections
accept optional case-insensitive text search and cursor pagination; their
default page limit is 50 and maximum is 200. Exact reads accept no Query
clauses, and dynamic schema is unavailable.

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

`level` is not a `PatchTarget`. It never loads or pins Actors, switches maps or
current Levels, reruns construction, changes selection, registers Components,
dirties a package, or creates an Undo entry. Actor and Component mutation,
lifecycle, Palette, save, live World control, and PIE/SIE observation remain
outside this interface.
