---
layout: default
title: PCG
parent: Interfaces
nav_order: 8
description: Inspect and edit an asset-backed PCG Graph, its native identities, persisted layout, Settings evidence, Edges, and authored persistence.
---

# PCG

The PCG interface reads and edits one independently saved, top-level
`UPCGGraph` asset. Query reads are read-only; Patch mutations apply inside one
top-level transaction. Graph Instances, Component-owned or embedded Graphs,
and non-PCG assets are not PCG Targets.

## Target and Identity

A discovery Query may omit `type`; the canonical Target retains the Graph's
actual native Class:

```sal
forest = target {
  domain: pcg,
  asset: "/Game/PCG/PCG_Forest.PCG_Forest",
  type: "/Script/PCG.PCGGraph"
}
```

PCG uses native string identity rather than fabricated Guids. A Node uses its
serialized UObject `FName`; a Pin adds native direction and exact Label:

```sal
@SurfaceSampler_0
@SurfaceSampler_0/in/Surface
@SurfaceSampler_0/out/Points
@SurfaceSampler_0/in/"Bounding Shape"
```

Node titles, Settings Classes, Pin types, local result keys, and semantic tags
are not identity. Edges are exact Pin relationships and have no independent
StableRef.

## Query

```sal
target [with schema]
summary
nodes ["text"] [with layout]
@identity [with schema, layout]
```

`nodes` accepts optional case-insensitive text search, `with layout`, and cursor
pagination. Native Graph order is preserved; the default page limit is 50 and
maximum is 200. `with layout` returns only the persisted integer Node position,
not live Slate geometry or Editor state.

An exact Node read includes its current Pins and incident Edges. An exact Pin
read includes its compact owner and incident Edges with the opposite endpoints.
Exact Target, Node, and Pin reads may request an explicitly read-only dynamic
schema. `summary` accepts no clauses.

## Settings Evidence

Node results keep the Node Class separate from the Settings interface and
effective Settings Classes. Their ownership is derived from the real Outer
chains. External Settings and Settings-instance wrappers remain read-only even
when reachable from the Graph. Pin results preserve native `allowedTypes` and
`currentTypes` separately; `typeDisplay` is presentation only.

## Patch

`pcg` is a Patch Target for authored PCG Graph edits:

```sal
patch forest [dry run]
sample = { palette: "P_PrintString" }
add sample
set @SurfaceSampler_0.Enabled = true
reset @SurfaceSampler_0.PointRadius
move @SurfaceSampler_0 to (320, 0)
connect @SurfaceSampler_0/out/Points -> @Noise_0/in/Points
remove @Obsolete_0
```

Node creation is Palette-backed: `add` references the exact opaque Palette id
discovered through the read-only `palette entries` Query for the same Target,
and the created Node returns its live serialized identity. Every authored
change is a planned effect with dry-run/live parity. A dry run shares the
ordered plan without touching the live Graph; live apply runs inside one
top-level transaction with `Modify` and native post-edit notification, and a
later failure rolls back the complete authored Graph snapshot.

`set`/`reset` target exact Node member fields on the graph-owned Settings and
notify through the Node's public change path so the owning Node rebuilds Pins.
External Settings assets and Settings-instance wrappers stay read-only.
`connect`/`disconnect` resolve exact output/input Pin refs; incompatible,
occupied, cyclic, or missing-edge edits fail closed before mutation. The Graph
default input and output Nodes are protected from removal.

Persistence is an independent terminal statement:

```sal
patch forest
save
```

`save` must be the only statement in its Patch. It persists only the
outermost package that owns the exact Graph Target through a
source-control-aware package save and never saves a related subgraph or
external Settings asset. A save dry run is advisory: it reports the dirty or
clean closure plan without executing native `PreSave` or writing disk, and a
clean closure is a valid no-op.

## Unavailable

`break`, Parameter migration, external Settings mutation, execution,
generation, cancellation, and live component configuration remain
unavailable. Persistent Component configuration belongs to the separate PCG
Component interface.
