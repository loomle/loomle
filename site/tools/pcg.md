---
layout: default
title: PCG
parent: Interfaces
nav_order: 8
description: Inspect an asset-backed PCG Graph, its native identities, persisted layout, Settings evidence, and Edges.
---

# PCG

The PCG interface reads one independently saved, top-level `UPCGGraph` asset.
It is Query-only. Graph Instances, Component-owned or embedded Graphs, and
non-PCG assets are not PCG Targets.

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

## Read-only Boundary

`pcg` is not a `PatchTarget`. Parameters, context and data flow, Palette, Node
creation or removal, Settings mutation, movement, connections, save, execution,
generation, cancellation, and inspection remain unavailable. Persistent
Component configuration belongs to the separate PCG Component interface.
