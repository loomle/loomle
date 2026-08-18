# Level Domain

## Status

Level is not part of the latest externally released Loomle interface catalog;
that product catalog still contains six Domains. This document is the design
and internal implementation contract for the persistent authored Domain named
`level`.

The unpublished scene/PCG family branch now contains an internal nine-Domain
Query-only release candidate. Its Level contribution includes:

- protocol v6 Target, Result, handoff, and Domain-specific StableRef
  groundwork for `level`;
- Slice 1A read-only `target`, `summary`, `actors`, and exact Actor Query with
  source-map canonicalization, ActorGuid identity, and root World Partition
  descriptor projection; and
- Slice 1B read-only Level Instance source ownership, including exact related
  source-Level Targets for loaded placement Actors and supported unloaded root
  Actor descriptors; and
- Slice 1C-A read-only `components` collection, summary counts, exact
  Component Query, and structured ActorGuid/source/slot StableRef lowering
  for bounded, proved native, SCS, and instance identities; and
- Slice 1C-B-A's internal Query-only `pcg_component` `target`/`summary`
  adapter, bounded Graph-binding reader, and matching Level-to-Component
  handoff; and
- Slice 1C-B-B's bounded Graph Parameter collection, exact descriptor-Guid
  StableRef Query, effective-source readback, and read-only value projection;
- Slice 1C-C's read-only Level Editor Context and zero-load Asset, Class, and
  proved-Blueprint related Target/handoff implementation; and
- the static `level` card, offline schema registration, Query-only admission,
  and explicit exclusion from `PatchTarget` used by the coordinated RC.

The card and adapter are branch-internal release-candidate artifacts, not an
external publication. Palette, mutation, save, and editable schema remain
planned later capabilities. The latest branch-local acceptance snapshot has
passed:

- official UE 5.7 and UE 5.8 arm64 builds;
- the eight-test `Loomle.Sal.Level.Query` group, seven-test coordinated PCG
  Query group, and two-test family Phase 0 group on both engines;
- the `ComponentIdentity` lifecycle fixture on both engines, including real
  construction rerun and Blueprint recompile, persisted Blueprint/map
  save-unload-reload, and same-slot isolation across distinct ActorGuids;
- real saved unloaded-root World Partition Actor, unloaded Level Instance
  descriptor, and native Level Instance edit-mode Editor Context fixtures on
  both engines;
- SAL, interface-card, Client, Site, version, native Client, and package unit
  validation; and
- local packaged end-to-end acceptance on UE 5.7 and UE 5.8.

This evidence validates the internal Query-only RC; it does not publish it.
The final release-artifact audit, Windows acceptance, and production promotion
have not run, and no external catalog exposes `level` yet.

The next public milestone is publication of the coordinated Scene/PCG
**Query-only** catalog RC after those gates pass. The branch RC already contains
the static card, read-only Editor Context, frozen related Target/handoff
coverage, and `PatchTarget` exclusion; every Level Patch is rejected before
Bridge dispatch. Exact
editable schema, Palette, mutation, save, and their effect/result contracts are
later capabilities and do not gate publication of the read-only card.

The core boundary is confirmed:

- `level` identifies one saved source map, whose top-level asset is a
  `UWorld`;
- the Domain owns authored Actor membership and serialized Actor-instance
  state for that source map;
- Actor identity is the native persistent `ActorGuid`, scoped to the Level
  Target;
- Component identity is the owning ActorGuid plus a source kind and
  source-specific persistent slot id, subject to strict lifecycle and
  collision checks;
- World Partition Actor descriptors are a read-only projection of the same
  Actor identity, not another object namespace;
- Level Instance contents belong to their saved source Level, never to a
  temporary composed instance;
- authored Patch, dry run, Undo, dirty-state recovery, and Level-package save
  follow the shared SAL mutation contract;
- live World control and observation belong to explicit Python + Skill
  workflows, while generation/cancellation/inspection belong to the typed
  `pcg_execution` frontend on the shared async kernel and its Bridge-private
  World epoch/ticket;
- PCG Component configuration belongs to a level-owned specialized
  `pcg_component` Target.
- every SAL Query or Patch still has one active Target and one active Domain;
  related Targets and result-only handoffs only guide later independent
  requests.

This file is not a published interface card. Exact editable property sets,
Actor and Component Palette coverage, preview-World support, and package-save
result fields remain gates for their later capabilities. The branch RC has
landed its Query diagnostics, hostile-fixture sources, static schema module,
and catalog entry. Current-snapshot dual-engine execution and local packaged
Query acceptance now pass; final release-artifact, Windows, and production
promotion gates still separate this branch-local RC from an external release.

## Decision

The persistent Domain is named `level`, not `world`.

A `.umap` package exports a `UWorld`, and the canonical Target therefore
verifies `/Script/Engine.World`. Loomle nevertheless uses `level` for the
authored boundary because the Target represents one saved source map and its
owned authored Level contents. It does not represent whichever live
EditorWorld, PIE World, streaming composition, preview World, or Level
Instance happens to contain those objects at the time of a request.

The distinction is:

| Surface | Identity and lifetime | Responsibility |
| --- | --- | --- |
| `level` | persistent source map Asset Path | authored Actors, serialized instance state, transaction, and save |
| `pcg_component` | persistent Level + ActorGuid + source-aware Component slot | PCG-specific component configuration and instance Parameter overrides |
| Python live workflow | no SAL Target; each call reacquires native objects | Editor/PIE/SIE control and live observation; optional `sal.object()` projection back to published persistent views |
| `pcg_execution` | opaque execution id bound to a private World epoch and source | generation, cancellation, cleanup, messages, and inspection |

`level` is a product ownership term, not a claim that the top-level asset is
a `ULevel`. No persistent `world` Domain is introduced as an alias.

## Intent

The full planned Domain should eventually let an agent:

1. obtain the exact saved source Level from Editor Context or Asset discovery;
2. inspect authored Actors without relying on labels, paths, selection, or
   the current viewport;
3. query both loaded Actors and unloaded World Partition descriptors through
   one ActorGuid identity environment;
4. inspect exact serialized Components of a loaded Actor;
5. discover instance-specific schema and destination-bound creation
   capabilities;
6. dry-run one ordered Actor/Component edit against native editor behavior;
7. apply it in one top-level transaction with complete effect readback;
8. save the Level's package closure in a separate terminal request; and
9. unload and reopen the source map with the same surviving Actor identities.

The first public milestone stops at read-only inspection and navigation: items
1 through 4 plus exact read-only schema, related Targets/handoffs, and Level
Editor Context. It advertises no Palette, Patch, dry run, transaction, Undo, or
save capability.

The Domain is not useful if it merely wraps `GEditor` helpers while retaining
implicit current-World scope, Actor labels, hidden Actor loading, nested
transactions, or best-effort package save.

## Capability Boundary

### Full planned scope

The authored `level` Domain owns:

- one saved source-map `UWorld` Target;
- the source World's `PersistentLevel`;
- persisted Actors logically owned by that Level, including World Partition
  Actors stored in external packages;
- Actor instance properties and editor-authored transform;
- serialized Actor-owned Components that pass exact identity checks;
- conservative instance-editable Component properties;
- Palette-backed Actor creation;
- Palette-backed instance Component creation;
- Actor removal and instance Component removal;
- native property-change, Actor, Component, and Level notifications;
- World Partition loaded/unloaded read projection;
- Level Instance source-Level handoffs;
- dry-run planning, one top-level transaction, rollback, readback, and a
  multi-package dirty ledger;
- explicit terminal save of the Level's native package closure.

### Outside the first public slice

The first public Level surface does not own:

- any authored Patch, dry run, transaction, Undo, lifecycle edit, or save;
- an unsaved or transient Editor World;
- PIE, SIE, game, preview, inactive client, or server World execution;
- streaming state, visibility, viewport focus, selection, simulation, or
  World Partition pinning;
- generated PCG Actors, local partition Components, PCG execution, or Data
  View;
- Blueprint class defaults, SCS Component definitions, native Component
  definitions, or Blueprint compile;
- mutation through an unloaded World Partition Actor descriptor;
- mutation of Level Instance contents through a containing instance;
- referenced Assets, PCG Graphs, Data Assets, materials, meshes, or classes;
- Actor attachment, folder, Data Layer, HLOD, runtime grid, or packed-Level
  structural editing until each has an exact native contract;
- arbitrary reflected property writes that exact schema does not advertise;
- map creation, rename, move, duplicate, deletion, or map switching;
- any Query or Patch that tries to activate another Level or Domain, including
  cross-Level/cross-Domain atomic mutation;
- baking or adopting runtime-generated output as authored Level state.

These exclusions are authored `level` capability boundaries. They must not be
bypassed through generic UObject reflection, an implicit Python fallback, the
experimental MCP Toolsets, or an implicit current-World fallback. An
explicitly authorized Python request may still perform transient Editor/PIE/SIE
control outside the Level Domain; its effects never acquire Level Patch,
transaction, or save semantics.

## UE Ownership

The persistent ownership model is:

```text
map UPackage
`- UWorld                         top-level asset and Target verifier
   `- PersistentLevel: ULevel     authored source-Level owner
      |- AWorldSettings
      |- ALevelScriptActor
      |- ordinary persisted AActor instances
      |  `- serialized UActorComponent instances
      `- World Partition Actors   may serialize in external Actor packages
```

The live EditorWorld may compose many `ULevel` objects:

```text
EditorWorld
|- current persistent source Level
|- streamed source Levels
|- Level Instance edit Levels
`- temporary or generated runtime/editor projections
```

Composition does not transfer authored ownership. Each saved source map has
its own Level Target. A streamed sublevel is addressed through the Target for
its own source map. A Level Instance placement Actor belongs to the containing
Level; the Actors displayed inside the instance belong to the referenced
source Level.

An external Actor package changes storage, not logical ownership. The Actor
remains part of the source Level Target and its package participates in that
Target's dirty and save closure.

Actor class structure remains owned elsewhere:

- a locally editable Actor instance belongs to `level`;
- its class default and native declaration belong to `class`;
- a Blueprint-authored class, SCS tree, and compile lifecycle belong to
  `blueprint`;
- a PCG Graph asset belongs to `pcg`;
- a serialized PCG Component's PCG-specific configuration belongs to
  `pcg_component`.

The same UObject may therefore be opened through different exact Domain
Targets, but one field or lifecycle operation must have one mutation owner.

## Target

Discovery may initially omit `type`:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena"
}
```

Canonical exact Query and every Patch return and require:

```sal
arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena",
  type: "/Script/Engine.World"
}
```

`asset` is the exact top-level source-map object path. `type` verifies the
native Asset Class and never selects the Domain.

The Target is flat. It does not contain a nested Asset Target, current World
pointer, streaming-level index, Level Instance id, editor tab, selection, or
runtime context.

The v1 resolver:

1. verifies one saved non-transient map asset at `asset`;
2. verifies its native `UWorld` Class;
3. canonicalizes the source map path and native Class without loading or
   switching maps;
4. resolves one exact loaded authored source `ULevel` when an operation needs
   Level contents;
5. verifies that it belongs to the Editor World context, not PIE or preview;
   and
6. rechecks the same ownership immediately before mutation.

It never changes the active map, loads a map into the Editor, chooses the
current Level, or falls through to another loaded World. A saved map whose
Asset Path and Class canonicalize but whose authored source Level is not
loaded retains its exact Target and returns
`capability.level_not_loaded` for content Query or Patch. A missing,
ambiguous, temporary, or wrong-Class map remains `unresolved_target`.

`target` is deliberately the one non-content Level Query. It succeeds from
Asset Registry evidence alone and returns the canonical exact Target even
when the source `UWorld` is not loaded. In that state the normalized resolved
Target has no live World or Level object. The Level adapter, rather than the
generic Target resolver, rejects `summary`, `actors`, exact-object, and other
content operations with `capability.level_not_loaded` while preserving
`targetContext: exact_target`. This absence is an authored loading-state fact,
not unresolved identity and never authorizes the resolver to load the map.

An unsaved Editor World has no persistent Level Target. Editor Context remains
`unresolved_target`, reports the temporary package as evidence, and suggests
saving the map. It never turns a temporary package name into a Target or
invents a live-World Target. Python may still observe that temporary World as
ordinary live data.

## Identity Environment

### Actors

An authored Actor StableRef is:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
```

The segment is the Actor's native persistent `AActor::ActorGuid`. It is scoped
to the exact source Level Target.

The resolver audits all locally owned persisted Actors and World Partition
Actor descriptors as one injective set. Resolution succeeds only when exactly
one native Actor or descriptor matches. Invalid or duplicate ActorGuids remain
readable as corruption evidence but receive no StableRef.

The Actor set is closed as follows:

- its root is the exact source `UWorld::PersistentLevel` selected by the
  Target;
- a loaded candidate must be a non-template, non-transient, save-eligible
  Actor owned directly by that root Level, including a newly authored unsaved
  Actor that would be serialized by the next Level save;
- `AWorldSettings` and `ALevelScriptActor` are included because they are
  authored root-Level Actors, although later exact schema may expose only a
  restricted surface for them;
- World Partition external Actors remain members of the source Level even
  though their storage package differs from the map package;
- streamed sublevel Actors, ordinary Level Instance composed-Level Actors,
  World Partition child-container Actors, templates, preview Actors,
  construction-only ChildActor/UCS products, PCG-generated Actors, and other
  runtime/transient projections are excluded;
- an excluded live projection never suppresses or conflicts with the source
  Actor or descriptor that owns the persistent identity.

On a World Partition source map, the adapter indexes only the root container's
Actor descriptors. A loaded Actor and its own descriptor with the same Guid
are one logical candidate, not a duplicate, and the live Actor wins because
its in-memory authored fields may be newer. An unloaded root descriptor is the
read-only representation of that same identity. Two independent root
candidates with one Guid are a real identity conflict. If the adapter cannot
prove root-container scope or complete the descriptor scan, it fails closed
with structural diagnostics rather than publishing a partial identity set.

The following are not Actor identity:

- Actor label;
- UObject `FName`;
- object or package path;
- Actor array index;
- attachment path;
- native Class;
- semantic tag;
- World Partition cell;
- `ActorInstanceGuid`.

Changing a label, transform, attachment, loaded state, external-package path,
or World Partition cell must not change a valid Actor StableRef.

### Components

Generic `UActorComponent` has no persistent component Guid shared by all
supported Actor and Component classes. A serialized Component StableRef is
therefore owner- and source-relative:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/RootComponent
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/instance/AudioComponent_0
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/scs/"/Game/Actors/BP_Enemy.BP_Enemy_C#bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"
```

The first segment is the owning ActorGuid. The second is the closed source
kind. The third is a source-specific persistent slot id:

- `native`: the actor-scoped default-subobject `FName`;
- `instance`: the actor-scoped serialized instance-Component `FName`;
- `scs`: the qualified `OwnerGeneratedClassPath + VariableGuid` key used by
  native `FComponentKey`/SCS resolution.

A quoted string segment preserves qualified ids. Construction-script (`ucs`)
products have no persistent Level StableRef in v1 because a UCS node id does
not identify one exact constructed instance across control flow and reruns.

Resolution requires:

- one exact loaded owner Actor;
- one direct Actor-owned Component from `GetComponents(..., false)`; child-Actor,
  nested-object, template, archetype, transient, incomplete-load, PIE, preview,
  generated, and wrong-package objects are excluded;
- one exact Component resolved through a proved native/SCS/instance source
  locator rather than through `CreationMethod` alone;
- no ambiguity across native, SCS, instance, or reconstructed Components; and
- a lifecycle category that the adapter can report exactly without loading an
  Actor, registering a Component, or rerunning construction.

The three supported source proofs are closed:

- `native` requires `CreationMethod::Native` and one unique same-name,
  compatible-Class default-subobject slot on the Actor Class CDO. The live
  Component must carry the inherited default-subobject flag and agree with
  that already-loaded CDO slot. The Query path does not call an archetype
  helper that may preload a Blueprint Class. A `NewObject` Component that
  merely retains the enum's default `Native` value is not a native slot.
- `instance` requires `CreationMethod::Instance`, direct Actor ownership, and
  the exact live pointer appearing exactly once in the Actor's serialized
  `InstanceComponents` array. Its non-empty Actor-scoped `FName` is the slot
  id.
- `scs` requires `CreationMethod::SimpleConstructionScript` and exactly one
  valid `USCS_Node` in the generated-Class inheritance chain whose generated
  object property points to that exact live Component. The node, property,
  `BlueprintCreatedComponents` membership, and live Component form a unique
  one-to-one mapping. The node's declaring `UBlueprintGeneratedClass` path and
  non-zero `VariableGuid` form the qualified id. The node variable name and the
  current UObject `FName` are evidence only.

`CreationMethod::UserConstructionScript` is always excluded. If a candidate
has no unique source proof, has a duplicate source id, or crosses a durability
boundary, the adapter omits it from the persistent Component identity set and
returns bounded structural evidence. It never guesses a source from names,
`IsCreatedByConstructionScript()`, editor presentation, or current registration
state.

The SCS proof uses an iterative, visited, bounded walk over each already-loaded
`USimpleConstructionScript::GetRootNodes()` tree. It does not call
`GetAllNodes()`, `FindSCSNodeByGuid()`, `GetActualComponentTemplate()`,
`GetArchetype()`, or any editor-tree approximation: those paths may fill
caches, preload Classes, recurse without the Level adapter's bounds, or resolve
by presentation name. A null, cyclic, duplicate, incomplete, reinstancing, or
otherwise ambiguous SCS graph makes Component identity fail closed.

Native and instance ids denote durable serialized slots, not one UObject
incarnation. A rename changes their StableRef, and delete/recreate with the
same name across reload cannot be distinguished; the contract does not claim
incarnation identity for those sources. SCS identity survives variable/FName
rename through its qualified Guid key. Live UObject incarnation remains local
to one Python call or typed execution record. After every operation that can rerun
construction or reconstruct Components, the adapter rebuilds the source-aware
Component identity environment before resolving a later statement.

Runtime-created, UCS, preview, generated, and local World Partition PCG
Components do not become persistent Level StableRefs. Python may return them as
ordinary evidence or mark them with `sal.object()`. A transient-only object
normally produces `status: unsupported` with a transient-only diagnostic;
only a uniquely proven persistent source can instead produce a `projected`
view with `relation: authored_source`.

### New objects

Dry run uses request-local aliases for planned Actor and Component creations.
It never publishes a preview ActorGuid, UObject name, or external package.
Live apply returns the actual native ActorGuid, source kind, and persistent
Component slot id after native creation and final readback.

## Object Text

`actor` and `component` are optional erasable presentation tags. Identity,
schema, mutation, and effects remain identical without them.

A loaded Actor may be returned as:

```sal
enemy = actor {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/Engine.StaticMeshActor",
  Name: "StaticMeshActor_2",
  ActorLabel: "Enemy_2",
  loaded: true,
  external: true,
  package: "/Game/Maps/__ExternalActors__/Arena/...",
  Transform: {
    Translation: { X: 100.0, Y: 0.0, Z: 0.0 },
    Rotation: { X: 0.0, Y: 0.0, Z: 0.0, W: 1.0 },
    Scale3D: { X: 1.0, Y: 1.0, Z: 1.0 }
  }
}
```

An unloaded World Partition Actor uses the same `id`:

```sal
enemy = actor {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/Engine.StaticMeshActor",
  ActorLabel: "Enemy_2",
  loaded: false,
  descriptor: true,
  package: "/Game/Maps/__ExternalActors__/Arena/...",
  bounds: {
    Min: { X: -50.0, Y: -50.0, Z: 0.0 },
    Max: { X: 50.0, Y: 50.0, Z: 100.0 }
  }
}
```

Only fields proven by the native Actor descriptor are returned when the Actor
is unloaded. Missing live-only fields are omitted rather than filled with
defaults.

A Level Instance placement remains an `actor` object in the containing Level.
When its saved source map canonicalizes exactly, the Actor adds an explicit
link to the related source-Level Target:

```sal
result exact_target
target arena = target {
  domain: level,
  asset: "/Game/Maps/Arena.Arena",
  type: "/Script/Engine.World"
}
related encounter_source = target {
  domain: level,
  asset: "/Game/Maps/Encounter.Encounter",
  type: "/Script/Engine.World"
}
handoff inspect_source_level to encounter_source
objects
encounter = actor {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/Engine.LevelInstance",
  level: arena,
  levelInstance: true,
  sourceLevel: encounter_source,
  loaded: true
}
```

`level` still names the containing Level, while `sourceLevel` is a `LocalRef`
to the independently canonical related Target. The semantic tag remains
`actor`; Loomle does not introduce a parallel `level_instance` object kind or
identity namespace. When source canonicalization fails, `sourceLevel` is
omitted and the placement Actor remains readable with a bounded
`resolution.level_instance_source_unavailable` warning.

A loaded Component is returned as an independent collection or exact-object
binding while retaining its owner explicitly:

```sal
mesh = component {
  actor: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,
  id: "StaticMeshComponent0",
  name: "StaticMeshComponent0",
  source: native,
  type: "/Script/Engine.StaticMeshComponent",
  CreationMethod: Native,
  registered: true,
  stableRefAvailable: true,
  ref: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/StaticMeshComponent0
}
```

The exact StableRef is
`@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/StaticMeshComponent0`. The local
binding name and `name` are presentation/evidence; neither replaces the
source-qualified `id`. An SCS Component additionally reports its declaring
generated Class path; its `id` and `ref` retain the qualified Class-path/Guid
key even if the SCS variable and Component UObject are renamed.

Once the specialized query-only `pcg_component` adapter is active in the same
coordinated Slice 1C-B-A, an exact serialized original `UPCGComponent` may
additionally retain one specialized Target:

```sal
forest = component {
  actor: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,
  id: "PCGComponent",
  name: "PCGComponent",
  source: native,
  type: "/Script/PCG.PCGComponent",
  CreationMethod: Native,
  pcgComponent: forest_component,
  stableRefAvailable: true,
  ref: @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/native/PCGComponent
}
related forest_component = target {
  domain: pcg_component,
  asset: "/Game/Maps/Arena.Arena",
  actorId: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  source: "native",
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent"
}
handoff inspect_pcg_component to forest_component
```

`pcgComponent` is the explicit Component-to-Target `LocalRef`. The handoff is
named `inspect_pcg_component` because the first specialized adapter is
Query-only. Multiple emitted references to the same canonical Component
Target share one related Target and one handoff. A local partition Component,
a generated/debug/cleanup projection, or a Component whose
`GetConstOriginalComponent()` is not itself produces no persistent Target.
This handoff does not require, imply, or wait for Graph Parameter readback;
that is the separate Slice 1C-B-B.

Results preserve exact native Class paths, names, property names, enum values,
and value shapes. The adapter does not translate them into a parallel scene
model.

## Query

The planned static Query surface is:

```sal
target
summary
actors ["text"]
components ["text"]
@identity
context @identity [depth N]
palette entries ["text"] to <exact-destination>
palette @id to <same-exact-destination>
```

`target` verifies the exact source map and reports Level-level authored facts.
`summary` returns bounded counts for loaded Actors, unloaded descriptors,
external Actors, supported Components, and structural diagnostics. Its
Component projection is closed as `componentCount`, `nativeComponentCount`,
`scsComponentCount`, `instanceComponentCount`, `pcgComponentCount`, and
`componentIdentityComplete`. `componentCount` is exactly the sum of the three
source counts. `pcgComponentCount` is the subset of those proved slots whose
live object is an authored original `UPCGComponent`; it is not an additional
identity category.

`actors` includes loaded persisted Actors and unloaded World Partition
descriptors. Search may inspect ActorGuid, native name, Actor label, object
path, package path, and native Class without treating any of them except
ActorGuid as identity.

`components` searches supported serialized Components on loaded Actors and
returns their owning ActorGuid. It never loads or pins an unloaded Actor.
Search covers current Component FName, source kind/id, native Class, owner
identity, and supported authored display fields. `context @actor-guid depth 1` is the
owner-scoped way to enumerate one Actor's Components; the Domain does not add
a special unquoted collection operand to Core grammar.

`@identity` returns one exact Actor or Component. `context` may include the
Actor's Components, owner relationship, attachment evidence, Level Instance
source relationship, and bounded referenced-asset evidence. It does not grant
mutation authority over related objects.

Actor and Component collections are cursor-paginated. The final static
interface card closes filter fields, ordering keys, limits, and result budget.
Default ordering must be deterministic and independent of `ULevel::Actors`
array order, World Partition iteration order, load order, label localization,
or pointer address.

Plain Query must not:

- load or pin an Actor;
- rerun a construction script;
- register or unregister a Component;
- repair Actor descriptors;
- switch the current Level or map;
- mutate selection or viewport state;
- dirty any package;
- create an Undo entry.

## Exact Schema

`with schema` is available for:

- the exact Level Target;
- one exact loaded Actor;
- one exact loaded Component;
- one exact unloaded Actor descriptor as a read-only surface;
- one exact contextual Palette entry.

Collections, summary, ambiguous Palette search, and context do not return
instance schema.

Schema is conservative and instance-specific. A reflected `FProperty` is not
writable merely because it exists or appears in a Details panel. A writable
field must pass all of the following:

- it belongs to the active Domain's mutation ownership;
- it is editable on this exact instance;
- it is persistent and representable without loss;
- it is not transient, duplicate-transient, deprecated, or disabled on
  instances;
- its native setter/change-notification path is known;
- preflight can predict all relevant cascades;
- reset can resolve one exact native archetype or template value;
- final readback can verify the applied value.

Registered specialized providers participate in exact capability. For an
`APCGVolume` or any Actor/Component whose native Level edit could schedule PCG
generate or cleanup, the operation is writable only when the provider can
suppress asynchronous work throughout preview, live transaction, and
rollback, then report a bounded invalidation without silently generating. If
that cannot be proven for an exact transform, reconstruction, or lifecycle
path, the Level operation is unavailable.

Schema distinguishes:

- read-only identity and descriptor fields;
- ordinary exact `set` and `reset` fields;
- compound native operations such as Actor transform;
- Actor and Component lifecycle capability;
- operations unavailable because the Actor is unloaded, the Component is
  inherited, the Editor is in PIE, preflight is unavailable, or the package
  is read-only.

Unloaded Actor descriptor schema never advertises live property mutation or
Component discovery. Schema must not silently load the Actor to expand its
surface.

The first implementation should support a small verified value set before
general containers or instanced subobjects. Object references must use the
exact schema representation and return related Targets when those can be
canonicalized.

`reset` means the native instance default from the exact archetype/template
chain. It never means a zero value, a guessed Class Default Object, or an
empty container. If one exact reset source cannot be proven, schema omits
`reset`.

## Palette

Palette discovery is destination-bound:

```sal
query arena
palette entries "Static Mesh Actor" to arena.Actors
```

```sal
query arena
palette entries "Audio Component" to @actor-guid.Components
```

An exact replay uses the same destination:

```sal
query arena
palette @opaque-entry to arena.Actors
```

Actor entries represent exact native placeable capabilities for the target
Level and engine. Component entries represent exact instance-Component
capabilities for the destination Actor. Opaque Palette identity may encode
the native Class, ActorFactory, required source Asset type, destination
constraints, and compatibility version, but no public prefix selects a
Domain or bypasses replay validation.

Exact Palette schema supplies every required creation field. A capability
that depends on current Content Browser selection, viewport position, active
Data Layer, current Level, modal UI, or another implicit editor state is not
advertised. Required source Assets, transform, attachment, or other inputs
must be explicit schema fields.

Raw Class paths are not creation authorization. The adapter re-resolves the
opaque entry against the current engine, destination, package state, and
preview capability immediately before dry run and live apply.

ActorFactory and Component creation entries remain unpublished until the
preview-World path can execute their native lifecycle without touching the
live World. Palette search may be implemented before creation Patch, but it
must report unavailable creation capability honestly.

## Patch

Level reuses the existing core Patch statements:

```sal
patch arena [dry run]

createdActor = { palette: "<opaque-actor-entry>" }
add createdActor to arena.Actors

set <actor-or-component>.<exact-field> = <value>
reset <actor-or-component>.<exact-field>
invoke <actor-or-component> <ExactSchemaOperation>(namedArguments) [as outputs]

createdComponent = { palette: "<opaque-component-entry>" }
add createdComponent to <actor>.Components

remove <exact-actor-or-component>
```

Creation aliases may be used by later statements in the same ordered Patch.
Exact Palette or instance schema must be queried and copied; callers cannot
guess a Class, field, operation, default, or lifecycle capability.

The adapter rejects:

- `compile`;
- authored edits mixed with terminal `save`;
- raw Actor or Component creation by caller-supplied Class;
- mutation of an unloaded Actor descriptor;
- mutation of a transient, preview, runtime-generated, or local PCG
  Component;
- removal of `AWorldSettings`, `ALevelScriptActor`, or another required Level
  Actor;
- removal of native or SCS Component definitions;
- Component rename;
- implicit Actor loading or map switching;
- a cross-Level StableRef or local alias;
- a property or operation absent from exact unchanged instance schema;
- placement inferred from selection, current viewport, current Data Layer, or
  current Level;
- a runtime execution command;
- a cross-Domain write.

### Actor mutation

Actor instance fields use `set` and `reset` only when exact schema advertises
them. Label mutation preserves ActorGuid. Transform is a compound
schema-discovered operation because native editor transform behavior includes
root-Component updates, movement notifications, construction behavior, and
readback; it is not implemented as three unrelated reflected property writes.

Actor creation:

1. re-resolves one exact Palette capability;
2. creates the Actor in the target source Level, never merely in the current
   World;
3. runs the native editor-equivalent construction path;
4. assigns and audits the final ActorGuid;
5. captures all created Components and external-package effects; and
6. returns the final Actor StableRef.

Actor removal follows the native editor destruction path, reports removed
Components and relationship changes, and records a pending external Actor
package deletion when applicable. It does not recursively delete referenced
Assets or source Levels.

Attachment, Actor duplication, Actor object rename, folder moves, Data Layer
membership, and World Partition placement metadata require separate
schema-discovered operations and remain unavailable until their cascades are
specified.

### Component mutation

Existing serialized Component properties may use `set` and `reset` when exact
schema proves instance editability and the correct archetype/template reset
source.

Component lifecycle is narrower:

- only an exact Palette entry may create a Component;
- creation uses the Actor's instance-Component path, native ownership,
  `CreationMethod`, registration, and editor notifications;
- removal is limited to Components whose native lifecycle is owned by the
  Actor instance, normally `CreationMethod::Instance`;
- a native or SCS Component definition returns a Class or Blueprint handoff
  instead of being removed;
- root replacement, attachment repair, and reconstruction cascades must be
  explicit effects or make the operation unavailable.

PCG-specific fields and `UPCGGraphInstance` overrides are not duplicated in
Level schema. An exact serialized original `UPCGComponent` returns a
`pcg_component` handoff. Runtime-generated local PCG Components remain
execution evidence.

### Native editor notifications

Mutation must follow the target engine's public editor-equivalent lifecycle:

- `Modify()` coverage for every authored object that may change;
- property pre-change and post-change notification;
- Actor movement and Level Actor notifications where applicable;
- Component creation, registration, unregistration, destruction, and owner
  notifications;
- construction-script and descriptor refresh behavior;
- package dirtying through the native path;
- final authoritative native readback.

The adapter uses a small 5.7/5.8 parity shim. It does not drive a Details
panel, require a focused Level Editor, or call a convenience API that owns an
uncontrolled nested transaction inside Loomle's top-level transaction.

## Level Instances

A Level Instance placement Actor is an ordinary Actor in the containing Level
and may be addressed there by its ActorGuid. Its instance transform and
supported placement properties belong to the containing Level.

The displayed contents of that instance do not belong to the containing
Level. For a loaded `ALevelInstance` family placement, the authored source
truth is `ILevelInstanceInterface::GetWorldAsset()`, including UE's effective
Property Override Asset source when one is active. The read path consumes only
the returned `FSoftObjectPath`; it does not call `Get()`, `LoadSynchronous()`,
`GetLoadedLevel()`, or a Level Instance load/edit API. `ULevelInstanceSubsystem`
is reserved for the later Editor Context case that must walk from an Actor in
an edit/composed Level back to its owning placement. A loaded temporary Level
or its package is never treated as the authored source.

For an unloaded root World Partition placement descriptor, the cross-version
public read path is:

1. verify that the descriptor's native Actor Class is an `ALevelInstance`
   family Class;
2. read the serialized source package through
   `FWorldPartitionActorDesc::GetChildContainerPackage()` without calling
   `IsLoaded()`, acquiring a reference, or registering a child container;
3. do not require `IsChildContainerInstance()` to be true, because a valid
   Level-Streaming Level Instance has a source package while that predicate is
   false; and
4. use disk-only Asset Registry evidence to select exactly one top-level
   `/Script/Engine.World` in that package and format its canonical object path.

Loaded and unloaded locators then share the same zero-load canonicalization.
The source must be one saved, non-temporary, top-level `UWorld`; an object
subpath, missing or non-World asset, ambiguous package result, PIE/temporary
path, or self-reference to the containing Target is unavailable. Only after
that proof does the result add:

- `levelInstance: true` on the placement Actor;
- `sourceLevel: <LocalRef>`;
- one canonical related `level` Target with the closed
  `domain, asset, type` shape; and
- one `inspect_source_level` handoff to that same alias.

An unavailable source keeps `levelInstance: true`, omits `sourceLevel`, and
emits a bounded warning.
It does not turn the containing query into `unresolved_target`. This is also
not a Python `sal.object()` `projected` record: the main result remains
`targetContext: exact_target` for the containing Level and the source is an
independent exact related Target.

Within one result, related source Targets are structurally deduplicated. Two
placements of the same source share one Target alias, one
`inspect_source_level` handoff, and the same `sourceLevel` reference. Different
sources receive independent aliases and handoffs. A paginated `actors` Query
retains sources only for placement Actors emitted on that page; an exact Actor
Query retains only that Actor's source. `target` and `summary` do not enumerate
or retain Level Instance sources. The collection cursor binds both each raw
source locator and its current disk-only Asset Registry resolution state, so a
deleted, restored, renamed, or retyped source invalidates an older cursor even
when the placement's soft path did not change. Cursor preparation is bounded
to 4,096 distinct source locators and fails closed rather than issuing a
continuation whose source projection cannot be proven stable.

The common UE 5.7/5.8 public descriptor surface does not expose a reliable
source package for every unloaded Packed Level Actor representation. In
particular, when an unloaded Packed descriptor returns no source through
`GetChildContainerPackage()`, Loomle preserves the placement Actor evidence but
omits `sourceLevel` and returns the same warning. It does not cast to a
version-private descriptor, infer from names, or load the Actor. A loaded Packed
Level Actor can still use its `ILevelInstanceInterface` source normally.

When a Level Instance is in edit mode, the later Editor Context slice likewise
returns the source Level that owns the selected Actor, never the temporary
instance package.

The rules are:

- no concatenated persistent ref such as
  `@level-instance-guid/contained-actor-guid`;
- no persistent use of `ActorInstanceGuid`;
- no write-through from containing Level to instance contents;
- no temporary Level Instance package as a Target;
- no heuristic source resolution from labels or package-name conventions;
- a missing, unsaved, unsupported, or ambiguous source remains ordinary
  evidence with no invented related Target.

The same authored ActorGuid may appear in several live instances. Python must
reacquire the intended instance on each call; no live-instance token becomes a
persistent Level StableRef. A proven `sal.object()` authored-source projection
still points to the shared source Level object, not to one placed live instance.

## World Partition

World Partition does not create a second Actor identity model.

When an Actor is loaded, Query reads the live Actor because unsaved in-memory
state may be newer than its descriptor. When it is unloaded, Query reads the
native `FWorldPartitionActorDesc` associated with the same ActorGuid.

Descriptor-backed Query may return only facts that the selected engine can
prove without loading the Actor, such as:

- ActorGuid;
- native Class;
- Actor label or name when present;
- bounds;
- Actor package;
- external-package state;
- supported reference or placement metadata whose 5.7/5.8 semantics match.

Actor descriptors do not enumerate arbitrary Actor Components. Component
Query, Component StableRef resolution, exact live schema, and all authored
Patch therefore require the Actor to be loaded.

Ordinary Query never creates an `FWorldPartitionReference`, pins a cell, loads
an Actor, or changes streaming state. Explicit loading and pinning use the
separately authorized Python fallback in short calls, followed by a later
Python native-state readback and, when useful, `sal.object()` projection of the
now-loaded persistent Actor. They remain outside the authored Level Patch. V1
also rejects deletion
or property mutation of an unloaded Actor even when a lower-level World
Partition API could perform a partial operation.

External Actor packages remain in the owning Level's dirty ledger and save
closure. Actor package paths are storage evidence, not StableRef identity.
Moving an Actor between external packages or cells does not grant another
Level authority.

## Dry Run, Transaction, And Readback

Every authored Patch follows:

```text
parse
  -> canonical Level resolve
  -> identity and Palette resolve
  -> exact capability validation
  -> ordered native plan and effect manifest
  -> volatile-state recheck
  -> one top-level transaction
  -> authoritative readback
```

The volatile-state recheck participates in the shared Scene/PCG operation
coordinator. If the affected Actor or Component is an active PCG execution
source, Level Patch fails before mutation and returns exact execution facts
plus a typed PCG settle/cancel next-action suggestion outside the SAL handoff
table. It never cancels execution implicitly. Holding the Level mutation lease
prevents a new execution from being admitted against that source.

The same guard forbids native callbacks from starting a new asynchronous PCG
generate or cleanup inside the Level transaction or rollback. A successful
Level edit may report derived invalidation, but it never hides generation as a
callback side effect. If the engine path cannot be contained, exact schema
marks that operation unavailable for the instance.

Even after execution settles, Actor/Component removal or a reconstructive
operation fails closed while the source Component retains managed generated
resources or their inventory is incomplete. The result returns exact
World/source facts plus a typed Component-scoped `pcg.cleanup` next-action
suggestion, not a SAL handoff. Native destruction must not smuggle cleanup of
derived resources into the authored Level transaction.

Dry run shares parse, resolve, validation, and planning. It never implements
preflight by mutating the live Level and then calling Undo.

Pure edits may be planned statically only when native behavior and all effects
are exact without execution. ActorFactory creation, construction scripts,
Component registration, transform callbacks, Actor removal, and any property
change with reconstructive behavior require an isolated preview World that
runs the same native operation path used by live apply.

Duplicating one Actor is not a sufficient sandbox. The preview facility must
provide an isolated transient `UWorld` and `ULevel`, required subsystem
context, duplicated authored inputs, deterministic mapping from source
ActorGuid/source-aware Component slot to preview objects, and capture of created,
destroyed, reconstructed, and dirtied state. It must not register the preview
as the current EditorWorld.

If an Actor Class, ActorFactory, Component Class, subsystem, or callback
cannot run faithfully in that preview, exact schema and Palette report the
operation unavailable. Loomle does not replace exact dry run with an
optimistic validation-only promise.

Dry run must leave unchanged:

- source Actors, Components, descriptors, and packages;
- current map, current Level, selection, viewport, and focus;
- World Partition loaded and pinned state;
- editor subsystem and PCG generation state;
- package dirty flags;
- Undo and Redo history;
- Source Control and disk state.

### Top-level transaction

Live apply uses one Loomle-owned top-level transaction. It modifies the source
World, Level, affected Actors, Components, and other authored objects before
their native mutation paths require it. Convenience APIs that open their own
top-level transaction are not composed blindly.

Statements execute in written order. After construction, destruction, or
reconstruction, later StableRefs are resolved again against current live
identity. A later failure explicitly undoes the complete transaction and
verifies native state.

Transaction cancellation is not rollback. On failure the adapter reverses the
exact transaction object created for this request; it never calls a generic
Editor Undo that could consume an earlier user transaction. The adapter:

1. explicitly applies the inverse of its still-active captured transaction;
2. cancels/removes and then closes that transaction so the failed request does
   not enter user history;
3. verifies Actor membership, ActorGuid, Component identity, fields, and
   relationships;
4. restores prior package dirty flags through the dirty ledger;
5. preserves the user's pre-existing Redo history; and
6. reports rollback failure independently from the original error.

An all-no-op Patch succeeds without dirtying a package or creating an Undo
entry.

### Dirty ledger

Before live apply, the adapter records every known affected package and its
prior state. Preflight may declare an operation-local package role when native
creation chooses the final external package path only during live apply; the
live ledger resolves that role to the actual path and preserves dry-run/live
correlation.

If a native callback touches an owned package that was neither in the plan nor
represented by a declared role, the adapter records it for recovery, fails the
Patch, and rolls back. It must not silently extend a successful ledger and
still claim plan parity.

The ledger distinguishes:

- the main map package;
- existing external Actor packages;
- newly created external Actor packages;
- external Actor packages pending deletion or rename;
- other engine-reported external packages logically owned by the source
  Level;
- related packages observed but outside mutation authority.

For each owned package it records prior dirty state, existence, ownership, and
the operations that affected it. Rollback restores in-memory state and prior
dirty flags. It does not claim to roll back disk I/O because save cannot appear
in the same authored Patch.

Effect readback reports direct and cascade changes, including Actor and
Component creation/removal, construction-script reconstruction, changed
properties, descriptor refresh, relationship changes, and dirty-package
membership. Facts outside the target Domain are ordinary evidence, not hidden
writes or mutation effects. When an exact canonical Target exists, Result Text
may separately retain it through a result-only handoff; the handoff is context
for a later request, not an effect. Non-Target runtime recovery is returned as
ordinary typed next-action data instead.

## Terminal Save

Level persistence is an independent terminal Patch:

```sal
patch arena
save
```

The request contains exactly one `save` statement. It cannot contain authored
edits, `compile`, runtime execution, or another statement.

Save uses the engine's World-aware editor save path. The candidate closure is
enumerated again immediately before I/O and contains only packages logically
owned by the exact source Level:

- the source map package when required;
- dirty external Actor packages;
- pending external Actor package deletions or renames;
- other native external packages that the engine reports as part of saving
  that World.

It does not save:

- streaming sublevel source maps;
- Level Instance source maps other than the active Target;
- referenced Assets;
- Blueprint classes;
- PCG Graph assets;
- external PCG Settings assets;
- runtime-generated or transient packages.

Package filtering cannot omit a derived PCG Actor or Component that physically
lives in a normal map or external Actor package. Before checkout, `PreSave`,
or I/O, Level save asks registered derived-state providers to inspect the
candidate closure without mutation. If PCG-managed generated projections are
present, or their inventory is incomplete, save fails closed and returns the
managing Component/World facts plus a typed PCG cleanup next-action
suggestion when exact inputs are available. It never runs cleanup automatically
and does not encode that suggestion as a SAL handoff. A future explicit
bake/adopt contract may authorize persistence of generated output; ordinary
Level save does not.
An active execution whose source or effects intersect the closure blocks this
check and save until it settles; the save lease in turn blocks new intersecting
execution admission.

There is no `save @actor` or `save @component` in v1. An internal Actor change
requires saving the map package. An external Actor change may require its
external package plus World bookkeeping. `pcg_component` returns a handoff to
this Level Target rather than pretending that Component persistence is an
independent PCG package save.

Save is Source Control-aware. A read-only preflight checks all known candidate
packages before checkout or write. The live path then acquires required
Source Control state before package I/O where the provider permits it.
Preflight reduces partial failure but cannot make several checkouts and
package writes atomic.

### Multi-package failure semantics

World save is external I/O and is not part of the preceding UObject
transaction. Files may be written, deleted, or checked out before a later
package fails. The result therefore reports every candidate package with an
actual status such as:

- saved;
- deleted;
- unchanged or skipped;
- blocked before I/O;
- failed after earlier success.

A save dry run never executes native `PreSave`. The live path captures an
authored pre/post snapshot around `PreSave` for every affected package because
engine normalization may change in-memory serialized state before any file is
written. If no Source Control change, observable `PreSave` change, package
write, or deletion begins, the request fails with no persistence effect. A
successful checkout or observable `PreSave` change is itself an effect and
must be reported even if no package is later written. If one checkout,
`PreSave` change, write, or deletion succeeds and a later step fails, the
result is a partial failure: successful effects remain, failed packages retain
their real dirty state, `applied` reflects that an observable effect occurred,
and an error diagnostic is returned. Loomle does not claim Source Control,
disk, or `PreSave` rollback and does not revert prior in-memory authored edits.

A successful package save clears only the dirty state that UE actually
clears. Final readback re-enumerates the Level package closure and reports
remaining dirty or failed packages.

`save` means “persist the current owned dirty closure of this Level Target,”
not “persist only the immediately preceding Loomle Patch.” It may therefore
persist unrelated user edits already present in the same map or external
Actor package. A dry-run save may enumerate and validate the current candidate
set without writing, but cannot guarantee that a later live save sees the same
set or succeeds.

## Results And Diagnostics

Level uses the shared SAL result envelope:

- canonical main Level Target after successful open;
- independent related Targets and handoffs;
- ordered Object Text;
- mutation plan and actual effect metadata;
- registered diagnostics in the later diagnostics block;
- honest applied, rollback, dirty, and save state.

Likely shared diagnostics include:

- `resolution.target_not_found`;
- `resolution.identity_conflict`;
- `resolution.level_instance_source_unavailable` as a warning that preserves
  the readable placement Actor;
- `validation.atomic_apply_failed`;
- `validation.atomic_rollback_failed`;
- `validation.save_failed`;
- `capability.operation_unavailable`;
- `capability.transaction_unavailable`;
- `capability.preflight_unavailable`.

Level-specific conditions need closed registered codes before publication,
including:

- map not saved or not loaded as one exact authored source Level;
- target resolves to PIE, preview, or another transient World;
- ActorGuid invalid, duplicate, or missing;
- Actor not loaded;
- Component missing, ambiguous, transient, or lifecycle-owned elsewhere;
- Level Instance source unresolved;
- World Partition operation requires explicit loading;
- package ownership ambiguous;
- multi-package save partially failed.

Suggestions point to an exact Query, `with schema`, contextual Palette, a
source `level` Target, or `pcg_component`. A transient
loading/pinning/control requirement may instead suggest the explicit Python
workflow and later `sal.object()` projection, but the Level adapter never
invokes it. Suggestions never use
an Actor label as identity, silently switch Target/Domain, or perform the
purpose named by a result-only handoff.

## Cross-Domain Handoffs

Handoffs use SAL's existing result-only Result Text envelope. An adapter emits
them as factual, flat, independent context for a later request; they are not
Query/Patch statements and do not perform the named purpose. Every related
Target is retained by Object Text through a scoped reference or by an explicit
handoff; a native object or package path string is never treated as Target
retention or mutation authority.

### Asset

Every successful exact Level Query retains the source map's canonical Asset
Target and the fixed `inspect_asset` handoff. This applies to the exact Level
Target read and exact Actor/Component reads; collection and summary rows do not
repeat per-object Class or Blueprint navigation. The source-map navigation is:

```sal
related arenaAsset = target {
  domain: asset,
  path: "/Game/Maps/Arena.Arena",
  type: "/Script/Engine.World"
}
handoff inspect_asset to arenaAsset
```

### Blueprint and Class

Only a loaded exact Actor or exact Component Query may return declaration
navigation. It retains the live object's actual native Class Target with the
fixed `inspect_class` handoff. A descriptor-only unloaded Actor has no live
UObject Class authority and omits this handoff. A loaded exact read additionally
retains one Blueprint Target with
`inspect_blueprint` only when already-loaded evidence uniquely proves a
Blueprint Generated Class and its exact `ClassGeneratedBy` Blueprint. For an
SCS Component, the source-aware locator's declaring Blueprint Generated Class
is the authority; the adapter does not infer a Blueprint from the current
runtime Class or a display name.

`summary`, `actors`, and `components` do not emit per-row Class or Blueprint
Targets/handoffs. Navigation adds no fields to Actor or Component Object Text,
performs no load, and emits no `save` or `compile` handoff. Ambiguous, unloaded,
transient, or non-Blueprint declaration evidence simply omits the applicable
Class/Blueprint navigation while preserving the exact Level Query result.

The Class Target is for exact defaults or native declaration facts. The
Blueprint Target is for Blueprint-authored defaults, SCS lifecycle, compile,
or structural Component edits.

Level Patch never crosses into those Targets.

### PCG Component

One loaded serialized original `UPCGComponent` may return:

```sal
related forestComponent = target {
  domain: pcg_component,
  asset: "/Game/Maps/Arena.Arena",
  actorId: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  source: "native",
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent"
}
handoff inspect_pcg_component to forestComponent
```

The Level Domain owns the Component's containment, Actor transform, lifecycle,
transaction package closure, and save. The specialized Target owns
PCG-specific settings, Graph binding, observer semantics, and GraphInstance
Parameter overrides. The same field is never writable in both Domains.
For an SCS source, only exact placed-instance overrides may enter the
specialized writable schema; template/default-owned fields hand off to
Blueprint or Class and remain read-only through the Level-owned Target.

Local partition PCG Components and generated Actors do not produce persistent
Targets. A local Component may return evidence relating it to its original
authored Component only through the execution surface.

### Python live-object projection

Level results never hand off to a generic live-World Target because no such SAL
Domain is introduced. Explicit Actor loading, streaming, PIE/SIE control,
selection, and camera behavior remain Python workflows.

Inside a later Python readback, `sal.object(actor)` may produce an exact
`level` view for the original Editor Actor or an `authored_source` view for a
uniquely proven PIE/SIE duplicate. The Bridge-owned projection is a standalone
SAL exact Query result. It does not preserve the live UObject, create a handoff
from a hidden Target, or allow a later SAL request to address the duplicate.

Editor Context continues to return only the persistent source `level` Target.
Typed PCG execution obtains its separate private World selector/ticket from the
typed PCG frontend, not from Level or Python projection.

### Level Instance source

A Level Instance returns the independently canonical source `level` Target,
retained both by the Actor's `sourceLevel` LocalRef and the fixed
`inspect_source_level` handoff. The Actor's `level` LocalRef and the result's
main Target remain the containing Level, which retains authority only over the
placement Actor. Failure to prove the source omits `sourceLevel` and produces
neither a related Target nor a handoff.

## UE 5.7 And UE 5.8 Compatibility

The public Level contract is the semantic intersection of UE 5.7 and UE 5.8.
Engine API drift belongs behind a small native compatibility layer.

The following required semantics exist in both supported versions:

- saved map packages with a top-level `UWorld`;
- `UWorld::PersistentLevel`;
- persistent editor ActorGuid;
- Actor-owned Component native names;
- instance Component lifecycle and `CreationMethod`;
- loaded Level Instance source-World resolution through
  `ILevelInstanceInterface::GetWorldAsset()`;
- supported unloaded Level Instance source-package evidence through
  `FWorldPartitionActorDesc::GetChildContainerPackage()`;
- World Partition Actor descriptors keyed by ActorGuid;
- external Actor packages;
- editor transactions, property notifications, Undo, and World-aware save.

Known compatibility areas that must remain private include:

- Actor descriptor iteration, handles, and container APIs;
- Level Instance subsystem ownership and edit-Level APIs;
- unloaded Packed Level Actor descriptor source availability;
- World Partition external-package enumeration and refresh;
- ActorFactory and Component editor helper signatures;
- property-change and Actor movement notification signatures;
- World-aware save and external Actor pre/post-save integration;
- fields available only on one version's Actor descriptor;
- PCG partition and original-Component helper differences.

The public Query returns only descriptor fields with equivalent meaning on
both versions. Version-specific fields may be absent or exposed through exact
schema only when their availability is explicit; they never silently change
the identity or ownership contract.

The adapter uses UE public runtime/editor APIs and a version-selected parity
shim. The plugin's always-on PCG integration links only the public `PCG`
runtime module; neither product nor test code links `PCGEditor` or other Editor
Private implementations, drives Slate, or depends on UE 5.8's experimental
ModelContextProtocol or PCGToolset plugins.

Every implementation slice builds and runs first on its selected engine, then
on the other supported official engine. Passing compilation alone is not
compatibility acceptance.

## Protocol And Catalog Impact

The internal family protocol has already added `level`, its closed Target
shape, `actors`, Result Target/handoff support, and Domain-specific StableRef
lowering under the unpublished v6 allocation. Slice 1B reuses the existing
Object Text `LocalRef`, canonical related-Target table, and result-only handoff
shape; `levelInstance` and `sourceLevel` are ordinary Actor result fields and
require no new statement grammar or protocol version.

The branch RC has landed the coordinated Core, Client, and Bridge contract for:

- the branch-local nine-Domain static catalog and offline `level` card;
- Level semantics for the existing `components` and `context` operations;
- generalize the existing `palette ... to <request-ref>` branch beyond
  StateTree so Level destinations reuse current text grammar and normalized
  request refs;
- Editor Context returning Level plus one selected Actor StableRef;
- retain unresolved behavior for unsaved maps;
- `sal_schema({module: "level"})` as an offline static module; and
- keeping `level` out of `PatchTarget` before Bridge dispatch.

The current snapshot now has UE 5.7/5.8 arm64 build and targeted Automation
evidence, real unloaded-root World Partition and native Level Instance
edit/composition evidence, static-card/Client/Bridge parity, and local packaged
end-to-end acceptance on both engines. External publication still requires the
final release-artifact ZIP audit, Windows acceptance, and production promotion.

Publishing any later Level Patch or save capability requires a coordinated
protocol/capability bump even if the SAL statement spelling is unchanged. That
later release also owns its editable schema, Palette, effect, transaction, and
persistence result contracts.

No public resolver or Domain-composition layer is added. `Target.domain`
selects exactly one adapter, and one Patch still belongs to one Domain planner.

## Implementation Slices

Each slice remains unpublished until its own gates pass. “Implemented in the
RC” below means present on the feature branch, including its branch-local card
where stated; it does not mean externally released. The current Query-only RC
has separate dual-engine local packaged acceptance recorded below.

The family Phase 0 Target/admission and Domain-specific StableRef work is a
prerequisite and is not expanded here with effects, save, projection, or
mutation behavior.

### Slice 1A: Target and Actor identity Query — implemented internally

- consume the internal family Target/protocol branch without publishing a
  static interface card or changing the already allocated, unpublished v6
  family protocol number;
- add the normalized `actors` collection operation to that v6 contract;
- canonicalize an exact saved source-map Target through Asset Registry without
  loading or switching maps;
- preserve an unloaded map as `exact_target`, allowing `target` while content
  Query reports `capability.level_not_loaded`;
- build one deterministic root Actor/ActorDesc identity index;
- return truthful `target`, `summary`, paginated `actors`, and exact Actor or
  unloaded-descriptor reads;
- merge a loaded Actor with its own root World Partition descriptor, preferring
  the live Actor;
- diagnose duplicate, invalid, incomplete, and out-of-scope identity evidence;
- prove that every Query preserves current World/Level, selection, transaction,
  construction, package dirty, and World Partition loaded state.

### Slice 1B: Level Instance source ownership — implemented internally

- keep the placement Actor in the containing Level identity environment;
- mark recognized placements with `levelInstance: true`;
- resolve a loaded placement's effective
  `ILevelInstanceInterface::GetWorldAsset()` and a supported unloaded root
  descriptor's `GetChildContainerPackage()` through zero-load Asset Registry
  canonicalization;
- return `sourceLevel: <LocalRef>` plus one related `level` Target retained by
  an `inspect_source_level` handoff;
- structurally deduplicate same-source Targets and handoffs while preserving
  the explicit Actor-to-source LocalRef;
- scope collection enrichment to the emitted page;
- reject temporary instance packages, `ActorInstanceGuid`, child-container
  composition, self-reference, and ambiguous, unsupported, or unsaved sources;
  and
- degrade an unsupported unloaded Packed Level Actor to readable placement
  evidence without `sourceLevel`, plus a bounded warning.

The current UE 5.7/5.8 acceptance snapshot now includes both a real unloaded
root World Partition Level Instance descriptor and a native Level Instance
edit/composed-Level Editor Context fixture. They prove that source readback
retains the canonical saved source without Actor load, child-container
registration, map switch, edit-state change, save, or temporary-package Target
substitution. This is branch-local RC evidence, not external publication.

### Slice 1C-A: Component identity Query — implemented internally

- implement the native/SCS/instance Component locator and exact read-only
  Component Query;
- lower Actor and Component StableRefs without fusing their identity segments;
- return deterministic paginated `components` results over only proved loaded
  persistent slots;
- keep UCS, transient, nested, generated, local-partition, and ambiguous
  Components outside the identity set;
- preserve World, Level, Actor, Component registration, construction,
  selection, transaction, package dirty, and load state across every Query.

At Slice 1C-A's original landing, it did not load Components for unloaded
World Partition descriptors, publish a static Level or `pcg_component` card,
or emit the later specialized handoff. Those card and handoff additions now
live in the coordinated RC without changing the 1C-A identity boundary.

At that slice's original landing, the UE 5.7 and UE 5.8 official arm64
persistent hosts compiled it and passed the targeted
`Loomle.Sal.Level.Query.ComponentIdentity` Automation
test and the full five-test `Loomle.Sal.Level.Query` regression. The internal
fixture proves native/SCS/instance identity, UCS and PCG generated/debug/
cleanup exclusion, structured StableRef round-trip, deterministic Component
pagination and cursor invalidation, summary count invariants, and read-only
Editor/Component lifecycle behavior. The current eight-test Query acceptance
also closes the real World Partition unloaded-owner boundary through the
descriptor-only fail-closed fixture. Its expanded `ComponentIdentity` fixture
now also proves, on both supported engines:

- real `RerunConstructionScripts` and Blueprint recompile replace live Actor or
  SCS Component incarnations while the durable source-qualified locator
  re-resolves to the current UObject;
- a saved Blueprint and Level can be fully unloaded and explicitly reloaded
  without an unloaded Query loading either package, while native, SCS,
  instance, and original PCG Component locators survive and resolve to fresh
  incarnations; and
- identical native/SCS/instance slot identifiers on two Actors remain isolated
  by ActorGuid, including direct resolver ownership checks rather than only
  Result-text comparison.

Hostile corruption cases that cannot arise after a completed synchronous
compile or construction rerun remain defensive-regression work; they are not
being represented as lifecycle evidence from these positive fixtures.

### Slice 1C-B-A: PCG Component binding Query and handoff — implemented internally

- activate only query-only `pcg_component` `target`, `summary`, and
  `target with schema`;
- resolve the exact Component by reusing the Level source-map, ActorGuid, and
  native/SCS/instance slot proofs from Slice 1C-A, without another identity
  model;
- read the Component-owned GraphInstance's direct `Graph` binding and its
  bounded top-Graph chain without loading an asset, switching a map, pinning
  an Actor, refreshing a GraphInstance, or retaining a native pointer as
  identity;
- keep Graph Parameter enumeration, exact Parameter StableRefs, values, and
  override-source inference out of this slice;
- add the candidate Target to Level Component results through the explicit
  `pcgComponent` LocalRef and fixed `inspect_pcg_component` handoff only after
  that Target resolves successfully; and
- keep local partition, generated, preview, UCS, incomplete, non-original,
  ambiguous, and unloaded-owner Components outside the specialized Target
  surface.

The specialized adapter's exact fields and reverse `inspect_level` /
`inspect_graph` handoffs are frozen in
[`PCG_RUNTIME_DOMAIN_DESIGN.md`](PCG_RUNTIME_DOMAIN_DESIGN.md). No `save`
handoff is emitted from this Query-only slice because no authored state was
changed.

At that slice's original landing, the official UE 5.7 and UE 5.8 arm64
persistent hosts compiled it and passed
`Loomle.Sal.PCGComponent.Query.AuthoredTargetSummaryAndBoundaries` plus
the full five-test `Loomle.Sal.Level.Query` regression. The fixture covers
saved versus unsaved top Graph navigation, direct unbound state, a parent
GraphInstance whose chain ends unbound, bounded-depth failure, frozen nested
field closure, the complete empty-Parameter case, and read-only
Level/Component/Graph invariants. The current real World Partition fixture now
proves that exact generic and PCG Component reads reject a descriptor-only
owner with `capability.component_owner_not_loaded` without loading it; an
entirely unloaded source map is separately covered by
`capability.level_not_loaded`.

### Slice 1C-B-B: PCG Component Parameter readback — implemented internally

- address declarations only by the top Graph property-bag descriptor Guid;
- prove descriptor alignment through the bounded Graph-interface chain before
  returning local, inherited, or default values;
- derive the Component-owned local override bit from
  `UPCGGraphInstance::IsPropertyOverridden` on the aligned native property,
  not `IsGraphParameterOverridden(FName)`; and
- use the frozen lossless type/value encoding, effective-source semantics,
  result bounds, exact Parameter StableRef lowering, and incomplete-chain
  behavior documented in `PCG_RUNTIME_DOMAIN_DESIGN.md`.

At that slice's original landing, the official UE 5.7 and UE 5.8 arm64
persistent hosts compiled it and passed the then-current full
`Loomle.Sal.PCGComponent.Query` group. The Parameter fixture
proves local, inherited, and Graph-default sources; canonical Guid identity
through rename and removal; exact and paged Query; representative certified
scalar encodings and descriptor-only unsupported value paths; bounded
GraphInstance-chain failure; and unchanged bags, override bits, delegates,
task/resource state, packages, and transactions. UE 5.8 additionally covers
the descriptor-only `int8`, `int16`,
and `uint16` additions. Native `map` remains intentionally fail-closed because
its key shape has no lossless representation in this public type shape; that
behavior and its hostile fixture are now implemented in RC source.

RC source now includes the native UE 5.8 `map` fail-closed case, exact 8 Ki
per-value and 64 Ki aggregate/evidence boundaries, and a zero-declaration chain
with a stale override bit. The latest official UE 5.7/5.8 arm64 snapshot now
passes the complete seven-test coordinated PCG Query group containing these
hostile boundaries. That is internal RC acceptance; external publication still
depends on the final distribution audit and promotion gates.

### Slice 1C-C: other referenced ownership and Editor Context — implemented in RC

- attach the canonical source-map Asset Target plus `inspect_asset` to every
  successful exact Level Query;
- attach the actual Class Target plus `inspect_class` only to exact Actor and
  Component Query;
- attach `inspect_blueprint` only when already-loaded unique
  Blueprint-Generated-Class/`ClassGeneratedBy` evidence proves the Blueprint;
  SCS Components use the declaring Generated Class already proved by their
  source-aware locator;
- emit no per-row Class/Blueprint navigation from collections or summary, add
  no Actor/Component Object Text fields, load nothing, and emit no `save` or
  `compile` handoff;
- upgrade Level Editor Context from Asset evidence to the shared Level
  resolver and identity index;
- project one selected source Actor as `@ActorGuid` only after the full root
  Actor/ActorDesc audit succeeds;
- keep loading, pinning, selection changes, and transient runtime objects in
  explicit Python workflows outside SAL.

The implementation is present in the branch RC, including shared Level
resolution for Editor Context and exact-query handoff injection. The latest
snapshot passes the eight-test Level Query group on official UE 5.7 and UE 5.8,
including the native edit-mode Context fixture, and passes local packaged
end-to-end acceptance on both engines. It remains unpublished.

### Slice 1D: coordinated Query-only publication — implemented and accepted in the branch RC

Landed in RC source:

- the read-only `level` card and offline
  `sal_schema({module: "level"})` in the branch-local nine-Domain catalog;
- the implemented Query, exact read-only schema, Editor Context, and related
  Target/handoff paths;
- rejection of canonical `level` from `PatchTarget` before Bridge dispatch;
- coordinated Client/Bridge/generated protocol source and hostile fixture
  coverage; and
- no Palette, mutation effects, transaction, save result, `sal.object()`, or
  execution capability.

Current branch-local acceptance:

- UE 5.7 and UE 5.8 arm64 builds pass;
- Level Query passes 8/8, coordinated PCG Query passes 7/7, and family Phase 0
  passes 2/2 on both engines;
- real saved unloaded-root World Partition, unloaded Level Instance descriptor,
  and native Level Instance edit-mode Context fixtures pass on both engines;
- SAL, interfaces, Client, Site, version, native Client, and package unit suites
  pass; and
- local packaged end-to-end acceptance passes on both engines.

Still required before external publication are the final release ZIP audit,
Windows acceptance, and production promotion. These remaining distribution
gates do not add mutation, Palette, save, or editable-schema capability to this
Query-only slice.

### Slice 2: Editable schema and Palette discovery

- conservative loaded Actor and Component schema;
- read-only unloaded descriptor schema;
- exact native value serialization and reset-source proof;
- destination-bound Actor and instance Component Palette;
- opaque replay and stale capability rejection;
- explicit reporting when preview or lifecycle support is unavailable.

Palette discovery progress (branch Slice 2-A, not published):

- `palette entries ["text"] to arena.Actors` and `to @actorGuid.Components`
  are accepted by the parser and normalized schema as destination-bound
  operations (shared `Destination*` wire variants with `state_tree`), with
  roundtrip and invalid fixtures;
- the Level adapter enumerates Actor creation capabilities from the editor
  placement catalog and instance Component capabilities from the editor
  component registry, excluding session state (Favorites/Recently Placed)
  and non-direct-creation actions;
- every entry reports its opaque digest id, exact native Class, category,
  required source Asset type when factory-driven, and
  `creation: unavailable` with the capability reason: creation Patch remains
  inactive until the preview-World execution path lands;
- destination resolution is fail-closed: Actor destinations must name the
  bound Level Target alias with path `Actors`; Component destinations must be
  one exact persisted loaded Actor with path `Components`; other shapes,
  malformed Guids, unknown Actors, and unloaded source maps are rejected;
- ordering is canonical (category, then display name) and results are
  cursor-paginated (`level_palette1:<fingerprint>:<offset>`);
- the exact `palette @id to <destination>` replay re-enumerates and
  revalidates the digest against the same destination before returning one
  entry, so stale or cross-destination ids fail closed.

Exact schema progress (branch Slice 2-B, not published):

- exact loaded Actor and Component reads accept `with schema` and emit a
  bounded schema comment classifying the surface: read-only identity fields,
  scalar `set`/`reset` candidates (persistent, instance-editable,
  non-editor-only scalar reflection properties), the compound Actor
  transform, lifecycle availability, and a PCG async-suppression constraint
  when a managed `UPCGComponent` is present;
- unloaded Actor descriptors report that live property schema is not
  advertised; `where`/`order by`/`page` and non-`schema` details remain
  rejected;
- the schema header states that authored mutation Patch is inactive and every
  advertised field is revalidated against the full native setter, cascade,
  reset-source, and readback gates at Patch planning time.

Remaining in Slice 2: unloaded descriptor read-only schema, exact native
value serialization and reset-source proof, and the full gate revalidation
that lands with authored mutation.

### Slice 3: Existing-object authored mutation

- preview-World kernel for supported exact operations;
- shared execution-source operation lease and fail-closed admission;
- loaded Actor and Component set/reset;
- Actor transform operation;
- native observer and construction effects;
- one top-level transaction;
- complete readback, rollback, and dirty ledger;
- no-op and late-failure behavior.

### Slice 4: Actor and Component lifecycle

- Palette-backed Actor creation;
- required ActorFactory support only where isolated preflight is exact;
- Actor removal and external-package deletion effects;
- Palette-backed instance Component creation;
- instance Component removal;
- reconstruction, registration, attachment, and root effects;
- final ActorGuid/source/component-slot alias readback.

### Slice 5: Save, handoffs, and packaged acceptance

- World-aware Source Control preflight;
- map and external-package closure;
- extensible derived-state save guards, including PCG managed projections;
- partial multi-package failure semantics;
- save/unload/reload identity and value verification;
- mutation/save-specific persistence-owner handoffs;
- save-capability packaged acceptance on UE 5.7 and UE 5.8.

## Test Requirements

### Protocol and static schema

- Level Target parse, normalize, format, and canonical ordering;
- omitted discovery `type` and required canonical `type`;
- Actor Guid and ActorGuid/source/component-slot StableRef round-trip;
- quoted qualified SCS Component id segments;
- generated JSON Schema and TypeScript parity;
- offline `sal_schema({module: "level"})`;
- Client-Bridge protocol mismatch fixtures;
- packaged static catalog parity.

### Target and Editor Context

- correct saved source map and wrong native Class assertion;
- no implicit map switch or map load;
- current persistent Level, streamed source Level, and Level Instance edit
  source resolve to the correct independent Target;
- unsaved map remains unresolved;
- Editor Context reads EditorWorld, never PIE;
- zero, one, and multiple Actor selection follow the shared context contract;
- selected Actor label and UObject-name changes do not change its StableRef;
- closed, replaced, or ambiguous Level owners fail closed.

### Identity and Query

- ActorGuid survives save, unload, reload, transform, label change, and
  external-package movement;
- invalid and duplicate ActorGuid corruption;
- same native/instance Component FName slot on different Actors;
- qualified SCS Guid survives component-variable rename;
- UCS product remains live-only and has no persistent SAL Target;
- duplicate or unsupported source-aware Component identity within one Actor;
- native, SCS, instance, transient, generated, and reconstructed Component
  classification;
- deterministic Actor and Component ordering and pagination;
- eligible original PCG Components retain one canonical `pcgComponent` LocalRef
  and deduplicated `inspect_pcg_component` handoff only after the specialized
  Target resolves;
- local, generated, debug, cleanup, preview, UCS, incomplete, ambiguous,
  wrong-Class, and unloaded-owner PCG Components produce no specialized
  Target or handoff;
- target, summary, collection, exact object, context, and schema reads;
- Query does not dirty, transact, load Actors, pin World Partition, change
  selection, or rerun construction.

### Level Instance and World Partition

- containing placement Actor remains in the containing Level;
- a loaded placement resolves its effective interface source without loading
  or switching the source map;
- a real unloaded root World Partition descriptor resolves its serialized
  source package without loading/pinning the Actor or registering its child
  container on both UE 5.7 and UE 5.8;
- contained Actor in a true edit/composed-Level fixture resolves to its saved
  source Level rather than the temporary package;
- two placements of one source share one related Target and handoff, retain
  explicit `sourceLevel` links, and do not create a persistent
  instance-qualified Actor ref;
- placements of different sources retain distinct canonical Targets and
  Actor-to-source links;
- paginated `actors` results retain sources only for placements emitted on the
  current page;
- unresolved source evidence keeps the placement readable with no
  `sourceLevel` and no related Target;
- an unloaded Packed Level Actor without a cross-version public source path
  degrades without a private cast or Actor load;
- temporary instance package is never a Target;
- loaded Actor takes precedence over stale descriptor state;
- unload/reload preserves the Actor StableRef;
- ActorDesc Query exposes only proven descriptor fields;
- Component Query and Patch on an unloaded Actor fail without loading;
- external Actor package remains in the source Level closure.

### Schema and Palette

- writable fields match exact unchanged instance capability;
- transient, deprecated, template-owned, and unsafe properties are absent;
- reset uses the correct Actor or Component archetype/template;
- Actor and Component Palette search, exact replay, and stale-entry rejection;
- Palette never reads implicit Content Browser, viewport, current Level, or
  Data Layer state;
- unsupported ActorFactory or Component preview fails before live mutation.

### Patch and native effects

- dry run and live plan parity for every published operation;
- dry run leaves World, Level, Actor, Component, descriptor, package,
  selection, World Partition pin, Undo/Redo, Source Control, and disk state
  unchanged;
- loaded Actor and Component set/reset;
- Actor transform callbacks and final readback;
- `APCGVolume`/PCG-source transform is either contained as authored change plus
  invalidation or unavailable; it never starts generation in the Patch;
- construction-script Component reconstruction;
- Actor and instance Component creation/removal;
- native/SCS Component lifecycle rejection and handoff;
- special required Actor removal rejection;
- PCG source Actor/Component removal is rejected while execution is active or
  managed resources remain/inventory is incomplete;
- local aliases resolve after lifecycle boundaries;
- one top-level Undo restores the complete Patch;
- later failure rolls back all earlier statements;
- dirty ledger restores pre-existing clean and dirty states;
- no-op Patch does not dirty or create Undo;
- rollback failure is separate from the original apply failure.

### Save and persistence

- terminal-only save grammar;
- no save mixed with authored edits or compile;
- clean Level no-op save;
- internal Actor map-package save;
- external Actor create, edit, delete, and package closure;
- all Source Control blockers discovered before I/O where possible;
- failure before the first write;
- simulated failure after one external package succeeds;
- partial result lists every saved, deleted, skipped, blocked, and failed
  package accurately;
- partial failure makes no disk rollback claim;
- final dirty readback matches UE;
- unrelated dirty state in an owned package is documented and persists;
- streaming sublevel, Level Instance source, Blueprint, PCG Graph, and
  referenced Asset packages are not saved;
- PCG-managed projection in an owned package blocks save before I/O until
  explicit cleanup settles; incomplete inventory also fails closed;
- active intersecting PCG execution blocks Level Patch/save, and a held Level
  mutation/save lease blocks new execution admission;
- save/unload/reload preserves surviving ActorGuid, source-qualified Component
  slot, fields, and relationships.

### Compatibility and integration

- targeted native automation passes on official UE 5.7 and UE 5.8;
- adapter builds without experimental ModelContextProtocol or PCGToolset;
- public query/result schema is identical across versions for common facts;
- Query-only live MCP acceptance covers Editor Context, Query, handoffs,
  `PatchTarget` rejection, and reload identity on both versions; and
- later mutation/save acceptance separately proves that API parity shims
  produce the same notifications, effects, Undo, and save semantics.

Recommended native automation names are:

```text
Loomle.Sal.Level.Query.*
Loomle.Sal.Level.Mutation.*
Loomle.Sal.Level.WorldPartition.*
Loomle.Sal.Level.Robust.*
```

## Release Gates

The Level RC card is ready for external publication when:

- every canonical Target round-trips to the same saved source Level;
- Actor identity depends only on one unique native ActorGuid in that Target;
- source-aware Component slot identity and lifecycle limits survive
  save/unload/reload or fail closed;
- Editor Context never substitutes a transient or composed World;
- Level Instance placements retain the exact saved source Level through
  matching `sourceLevel` and `inspect_source_level` references, with structural
  deduplication and containing-Level `exact_target` context;
- real unloaded root World Partition and temporary edit/composed-Level
  fixtures prove the same Level Instance ownership on UE 5.7 and UE 5.8
  without loading, pinning, child-container registration, or temporary-package
  Target substitution;
- unloaded ActorDesc Query never loads or mutates an Actor;
- exact read-only schema advertises no Patch, Palette, transaction, or save
  capability;
- exact Query emits only the frozen zero-load Asset/Class/Blueprint related
  Targets and handoffs, while collections and summary emit no per-row
  declaration navigation;
- `level` is accepted by `QueryTarget` and rejected by `PatchTarget` before
  Bridge dispatch;
- every Query preserves World/Level selection, Actor loading and construction,
  package dirty state, transactions, Undo, and World Partition state;
- UE 5.7 and UE 5.8 targeted Query suites both pass; and
- Client, Bridge, protocol, Editor Context, static schema, and packaged
  artifacts advertise the same Query-only contract.

The current branch snapshot supplies the dual-engine build, targeted native
Automation, real World Partition/Level Instance, contract-parity, and local
packaged end-to-end evidence for this Query-only gate. It is still not an
external release: the final ZIP artifact audit, Windows acceptance, and
production promotion have not run.

The distilled `interfaces/level.md` card and nine-Domain `sal_schema` catalog
remain branch-local release-candidate artifacts. They may be exposed by an
external release only after the remaining release-artifact, platform, and
promotion gates pass.

The later authored mutation/save capability has a separate release gate:

- exact writable schema advertises only operations that unchanged Patch
  accepts;
- every published Palette entry replays without implicit editor state;
- every reconstructive operation has exact isolated preflight or is
  unavailable;
- dry run and live apply use equivalent native paths and effect accounting;
- one Undo restores a successful authored Patch;
- rollback restores authored state and prior dirty flags after later failure;
- terminal save enumerates the real Level package closure;
- terminal save fails before I/O when a registered provider reports live or
  incompletely inventoried derived projections in that closure;
- partial multi-package save is reported without an atomicity or rollback
  claim;
- no Level request mutates Blueprint structure, PCG Graphs, live Editor/PIE state,
  or referenced Assets;
- UE 5.7 and UE 5.8 mutation/save suites both pass; and
- Client, Bridge, protocol, static schema, and packaged artifacts advertise the
  same bumped Patch capability.

## Decisions Required Before Authored-Mutation Capability Freeze

The ownership and identity boundary is settled. These narrower capability
decisions remain:

1. Which exact Actor and Component property value families enter the first
   writable schema, and which require dedicated compound operations?
2. Does v1 permit per-instance mutation of native/SCS Components, or only
   query plus mutation of `CreationMethod::Instance` Components?
3. Which ActorFactory and Component creation paths can be proven inside the
   preview World, including construction scripts, attachment, and root
   replacement?
4. What exact package-status and Source Control fields become the canonical
   multi-package save result?

Internal Phase 0 and the implemented read-only Level slices have landed on the
family feature branch. Their presence does not publish a Domain; Query-only
`level`, `pcg`, and `pcg_component` still enter the public catalog only through
the coordinated family release.

None of these questions permits changing the persistent `level` Target,
ActorGuid identity, Level Instance ownership, ActorDesc read-only boundary, or
terminal multi-package save semantics.
