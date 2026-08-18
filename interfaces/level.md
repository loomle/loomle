# level

Inspect the authored contents of one persistent source map through stable Actor
and Component identity. This interface is read-only.

## Target

A discovery Query may omit `type`:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena"
}
```

The canonical exact Target includes the verified native World Class:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena",
  type: "/Script/Engine.World"
}
```

`asset` identifies the saved source map. It never means the current Editor
World, PIE/SIE World, a streamed composition, or a temporary Level Instance
World. `target` canonicalizes the saved map through Asset Registry evidence
without loading or switching it. Content operations require that exact source
Level to already be loaded in the authored Editor World.

## Identity

An Actor uses its persistent native `ActorGuid`:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
```

A serialized Component uses its owner ActorGuid, proved source kind, and
source-specific slot id:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/RootComponent
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/instance/AudioComponent_0
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/scs/"/Game/Actors/BP_Enemy.BP_Enemy_C#bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"
```

The closed source kinds are `native`, `scs`, and `instance`. Actor labels,
object names, paths, array indices, Component `CreationMethod` alone, UCS
products, and generated or local-partition PCG objects are not identity.

## Query

```sal
target
summary
actors ["text"]
components ["text"]
@identity
palette entries ["text"] to <destination>
palette @id to <same-destination>
```

`target` is the only operation available when the saved map is not loaded.
`summary` returns bounded Actor and Component counts plus identity-completeness
facts. `actors` includes loaded persisted Actors and unloaded root World
Partition descriptors. `components` includes only uniquely proved serialized
Components of loaded Actors and never loads or pins an unloaded owner.

The two collections accept optional case-insensitive text search and cursor
pagination only:

```sal
query arena
actors "enemy"
page limit 25

query arena
components "pcg"
page limit 25
page after "<cursor>"
```

The default limit is 50 and the maximum is 200. Actor search covers ActorGuid,
native name, label, object path, package path, and native Class. Component
search covers owner ActorGuid, source, slot id, current name, native Class,
CreationMethod, and declaring Class. Ordering is canonical and fixed; custom
`where`, `with`, and `order by` clauses are unavailable. Cursors are bound to
the exact Target, complete identity snapshot, search, page limit, and retained
Level Instance source evidence.

Exact reads accept no Query clauses. An unloaded Actor descriptor remains an
exact read-only Actor, but its live-only fields and Components are absent.
Dynamic `with schema`, `context`, references, and every Patch form are not part
of this Query-only interface.

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

## Objects And Handoffs

A loaded Actor reports its exact Class, persistent identity, authored
transform, ownership package, and load state. An unloaded World Partition
descriptor reports only facts proved by that descriptor and keeps the same
ActorGuid identity.

A loaded Component retains its owner and exact source-qualified reference:

```sal
mesh = component {
  actor: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,
  id: "StaticMeshComponent0",
  name: "StaticMeshComponent0",
  source: native,
  type: "/Script/Engine.StaticMeshComponent",
  CreationMethod: Native,
  stableRefAvailable: true,
  ref: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/StaticMeshComponent0
}
```

A recognized Level Instance placement may retain its independently canonical
source map as `sourceLevel` and emit `handoff inspect_source_level`. A supported
original authored `UPCGComponent` may retain an exact `pcg_component` Target
as `pcgComponent` and emit `handoff inspect_pcg_component`. Related Targets are
navigation for later independent Queries; they never widen this request.

Every successful exact Level Query also retains the canonical Asset Target for
the source map and emits `handoff inspect_asset`. A loaded exact Actor or
Component read retains its actual native Class Target through
`handoff inspect_class`; a descriptor-only unloaded Actor has no live UObject
Class authority, so that handoff is omitted.
When the already-loaded Class is uniquely proved to be generated by one valid
Blueprint, that same exact read additionally emits `handoff inspect_blueprint`.
SCS Component provenance uses the declaring generated Class proved by its slot
identity rather than guessing from the placed Actor's most-derived Class. None
of these handoffs loads a Class or Blueprint, adds save/compile authority, or
changes the active Level Target.

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
