---
layout: default
title: PCG Component
parent: Interfaces
nav_order: 9
description: Inspect persistent PCG configuration and Graph Parameters on one exact authored Level Component.
---

# PCG Component

The PCG Component interface reads one persistent authored `UPCGComponent`
owned by a saved source Level. It reads and edits certified scalar configuration under the async edit guard; it does not expose live
execution or generated resources.

## Exact Target and Identity

Every Query uses the complete exact Target:

```sal
forestComponent = target {
  domain: pcg_component,
  asset: "/Game/Maps/Forest.Forest",
  actorId: "11111111-1111-1111-1111-111111111111",
  source: "native",
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent"
}
```

The map, persistent ActorGuid, and Level Component `source + id` prove the
Component identity. `source` is `native`, `scs`, or `instance`. Resolution
requires one loaded, original authored Component; UCS, transient, generated,
debug, cleanup, local-partition, preview, ambiguous, and unloaded-owner
Components have no Target.

A Graph Parameter uses the non-zero canonical lowercase descriptor Guid from
the terminal top Graph's native Property Bag:

```sal
@22222222-2222-2222-2222-222222222222
```

Parameter name, order, type, value, and override source do not participate in
identity.

## Query

```sal
target [with schema]
summary
parameters ["text"]
@parameter-guid [with schema]
```

`summary` returns the direct Graph interface, terminal top Graph, binding kind,
and completeness. An unbound complete Component has null Graph fields and no
Parameters. An incomplete, cyclic, unsupported, or over-budget chain fails the
Parameter Query atomically rather than publishing partial identity or values.

`parameters` accepts optional case-insensitive text search and cursor
pagination only. Canonical descriptor-Guid order is fixed; the default page
limit is 50 and maximum is 200. Exact Parameter Query accepts only optional
`with schema`; the schema advertises no Patch operations.

## Parameter Values and Handoffs

Each Parameter reports its descriptor identity and type, local override state,
effective source, and lossless value status. Unsupported kinds remain
descriptor-only with `valueStatus: unsupported` only when their complete
descriptor shape is representable. A native shape the closed type record
cannot represent, including a UE 5.8 Map key shape, fails the complete snapshot
closed. String-like values are limited to 8 Ki UTF-16 code units each and 64
Ki in aggregate; snapshots accept at most 4,096 Parameters and 32 loaded
Graph-interface links.

Every successful result returns the owning Level Target with
`handoff inspect_level`.
`summary` also returns `handoff inspect_graph` when the terminal Graph is an
independently canonical asset-backed PCG Target. These handoffs are navigation
for later independent Queries, not shared authority.

## Patch

`pcg_component` is a Patch Target under the async edit guard:

```text
patch forestComponent [dry run]
set @PCGComponent.Seed = 100
reset @PCGComponent.Seed
```

The edit guard is fail-closed: the exact Component must be idle (no
generation, cleanup, or refresh task) before and after the transaction, and a
native notification that starts an asynchronous PCG task inside the
transaction rolls the change back and reports `capability.pcg_async_unproven`.
Exact schema advertises the certified scalar `Seed`; any other field or
statement fails closed. A dry run shares the plan without touching the
Component; live apply runs inside one top-level transaction with `Modify`,
readback, and rollback on failure.

`pcg_component` never saves. A lone terminal `save` is a valid no-op that
reports the owning Level persistence owner. Graph assignment, Parameter
override or schema mutation, generation, cleanup, cancellation, task state,
managed resources, messages, and inspection remain unavailable. Level owns
Component containment and persistence; PCG owns authored Graph state.
