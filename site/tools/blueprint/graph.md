---
layout: default
title: Graph
parent: Interfaces
nav_order: 4
has_children: true
---

# Graph

Graph reads and edits use one flat, exact Graph Target. The owning Blueprint is
verified by fields on that Target rather than embedded as another Domain:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

Both `blueprintId` and `id` must be canonical lowercase, hyphenated, non-zero
GUIDs.

## Query

Graph primary operations include:

```sal
summary
nodes ["text"]
target
@node-guid
@node-guid/pin-guid
context @identity [depth N]
exec flow from|to @identity [depth N]
data flow from|to @identity [depth N]
references to <exact-subject> [in project]
palette entries ["text"] [from|to @node-guid/pin-guid]
palette @id
```

Traversal defaults to depth 1 and stays inside the Target Graph. Request
stored and, when available, live visual layout detail with:

```sal
with layout
```

This detail is available only on `nodes`, exact Node or Pin reads, `context`,
and exec/data flows. It is unavailable on `target`, `summary`,
owning-Blueprint declarations, `references`, and Palette operations.

Every returned Node then has its exact stored `at` and may have a non-zero
stored `size`. Those fields remain stored facts whether or not the editor is
open, so use them only for rough placement.

When a usable live surface presents the exact Graph, the same returned Node
and Pin objects also carry authoritative graph-space visual fields:
`visualBounds` on Nodes; `visualState`, `visualBounds`, `visualCenter`,
`placementAnchor`, and `placementAnchorKind` on measured Pins; or
`visualState: intentionally_not_presented` with `geometryReasons` on Pins
hidden by current presentation rules.

Bounds are `[left, top, right, bottom]`; centers and anchors are `[x, y]`.
`visualState` is `measured` or `intentionally_not_presented`;
`placementAnchorKind` is `pin_image_center` or `pin_row_edge_midpoint`; and
`geometryReasons` is an ordered non-empty subset of `hidden_native`,
`hidden_advanced`, `hidden_unconnected`, and
`hidden_unconnected_no_default`.

If the exact surface is closed, ambiguous, interacting, unsynchronized, or any
applicable object cannot be measured safely, the entire response drops all
visual fields and warns `capability.layout_geometry_unavailable`. Open or
focus and synchronize the exact Graph Editor surface, finish any interaction,
then retry. Gate precise layout on both the absence of that warning and
complete visual fields on every applicable returned object. `with layout`
creates no status or snapshot object and does not widen the Query projection.
The warning's closed `actual.reason` values are listed by the Graph interface
card returned from `sal_schema`.

Exact Node reads return all current Pins; traversal returns only the Pins
necessary to express its Edges. The following modifier does not exist:

```sal
with pins
```

A summary returns semantic entry Nodes, disconnected-region representatives,
counts, and an index of Nodes carrying native UE health state. Exact Node, Pin,
Graph, and Palette Entry reads may request dynamic schema.

## Patch

Palette creates the Node's native base Pins:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
delay = { palette: "palette-entry-id" }
add delay
```

A formatter may add an erasable presentation tag, for example
`delay = node { palette: "palette-entry-id" }`; removing `node` cannot change
creation or validation.

Graph adds these operations to the Core Patch surface:

```sal
connect
disconnect
break
insert
```

It also supports explicit Node movement and current UE operations:

```sal
connect @source-node-guid/source-pin-guid ->
  @target-node-guid/target-pin-guid
break @node-guid/pin-guid
move @node-guid to (640, 320)
invoke @node-guid <Operation>(<name>: <value>) [as <alias>]
```

Graph Node movement accepts only absolute `to (x, y)`. For relative intent,
query the Node with `with layout`, read its stored `at`, compute the absolute
destination, and emit `to`; `by` is not a Graph capability.

Coordinates must be signed 32-bit mathematical integers that round-trip
exactly through UE's `FVector2f` Schema path; every integer from `-16777216`
through `16777216` is safe. Each valid move remains in
`planned.operations` with `to`, `before.at`, `after.at`, and `changed`. For a
move-only Patch, `diff` is a complete ordered Graph-scoped change set; no-ops
stay in the plan but are omitted from `diff.changes`. Query again with
`with layout` after apply to refresh visual geometry.

Do not declare raw Pins. A normal addition may connect at most one side of a
new Node; use the insert operation for two-sided replacement. Its middle
separator names the new Node's input and output references with spaces around
`/`:

```sal
insert @source-node-guid/source-pin-guid ->
  delay.input / delay.output ->
  @target-node-guid/target-pin-guid
```

Exact dynamic schema is authoritative for operation names and parameters.

Graph Patch does not compile or save its owning Blueprint. Finalize through a
separate exact Blueprint Patch. A Graph result exposes that transition through
an independent related Target and handoff:

```sal
result exact_target
target eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
related door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to door
no_objects
```
