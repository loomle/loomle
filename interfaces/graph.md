# graph

Inspect and edit one resolved UE Graph and its Nodes, Pins, and Edges. This
interface assumes the resident SAL core guide and does not repeat Core syntax.

## Target

A Graph request includes its exact asset-backed owner:

```sal
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

eventGraph = graph(asset: door, id: "graph-guid")

query eventGraph
summary
```

`graph@id`, `node@id`, and `pin@id` are scoped selectors. They cannot replace
the complete request target.

## Queries

Every Query has one primary operation:

```sal
query eventGraph
summary

query eventGraph
nodes ["text"]

query eventGraph
graph@id

query eventGraph
node@id

query eventGraph
pin@id

query eventGraph
context node@id|pin@id [depth N]

query eventGraph
exec flow from|to node@id|pin@id [depth N]

query eventGraph
data flow from|to node@id|pin@id [depth N]

query eventGraph
references to <typed-ref>[.<native-member-path>] [in project]

query eventGraph
palette entries ["text"] [from|to pin@id]

query eventGraph
palette @id
```

Traversal stays inside the target Graph and depth defaults to 1. `with layout`
adds stored Node position and size to `nodes`, exact Node or Pin reads,
`context`, `exec flow`, and `data flow`. `with schema` is valid on an exact
Graph, Node, Pin, or Palette Entry. Graph defines no `with pins` or
`with defaults`: exact Nodes return all current Pins, exact Palette Entries
return all determinable future Pins, and traversal returns only necessary Pins.
`nodes` supports `=` and `!=` on `type`, `id`, and `NodeComment`, plus `~=` on
`NodeComment`; ordering keys are `type` and `id`. `palette entries` supports Pin
context; `widget`, `component`, and `actor` accept only `=` and are mutually
exclusive, while Boolean `contextSensitive` accepts `=` and `!=` and defaults
to true. Palette ordering keys are `name`, `category`, and `id`. Both
collections use cursor pagination with a default limit of 50. Ordered
comparisons are unsupported.

Graph Summary returns semantic entry Nodes, disconnected-region
representatives, compact counts, and a compact Comment index of every Node in
the complete Graph that currently carries UE health state. The index includes
`node@id` for exact follow-up without expanding the whole Graph.
`node@id` returns the complete Node and its Pins; `pin@id` returns its compact
owner and the complete Pin without traversing links.

Context and flow results remain ordinary ordered Object Text: compact Nodes and
only the complete target, Edge-endpoint, boundary, or dependency-leaf Pins
appear before the Edges that reference them.

`references` accepts only cursor `page` clauses, with a default limit of 50: no
`where`, `order by`, `with`, or `depth`. Without `in project`, its complete
local scope is exactly the bound Graph; it never silently ascends to the owning
Blueprint or another Graph. Results exclude the declaration itself and remain
ordinary ordered Object Text containing compact matching use-site objects.
Project pages add the compact owner bindings required to read each page
independently. An unavailable or incomplete native extractor must report a
diagnostic rather than guess or claim a complete zero result.

Outside Summary, every Graph Query automatically places current UE health
comments beside each returned existing Node or Pin. Summary keeps its returned
representatives compact and reports all Graph health only through its index.
Node health includes compiler messages with their `ErrorType` severity,
`NodeUpgradeMessage`, and visual warnings from `ShowVisualWarning()` /
`GetVisualWarningTooltipText()`; Pin health includes `DeprecationMessage`.
These are existing Comments, not fields, objects, operations, or execution
diagnostics, and require no `with` clause.

Graph Query does not compile or refresh its owner. For a Blueprint-owned Graph
whose native Status is `BS_Dirty` or `BS_Unknown`, a result that returns or
indexes stored Node compiler annotations warns once that they may be stale. Use
a separate exact Blueprint Patch containing `compile` to obtain fresh complete
ordered compiler diagnostics.

## Dynamic Schema

Inspect the current Graph's actual Query and Graph-level Operations:

```sal
query eventGraph
graph@graph-guid
with schema
```

Inspect one existing object:

```sal
query eventGraph
node@node-guid
with schema
```

Inspect one creation capability:

```sal
query eventGraph
palette @palette-entry-id
with schema
```

The schema comment gives exact fields, constraints, available Operations,
parameters, outputs, and copyable `invoke` templates for the resolved UE
context. It advertises only Operations that the resolved Node or Pin can
execute now; for example, a non-removable dynamic Pin does not advertise its
remove Operation.

Timeline remains one compound Graph Node. An exact Timeline Node read flattens
its paired `UTimelineTemplate` fields, ordered Tracks, internal Curve Keys, and
external Curve references onto `node(...)`. `with schema` exposes its
constructor fields and target-local Track, Key, Curve-ownership, duplication,
and deletion Operations; there is no separate Timeline selector or object.

## Patch

New Nodes use bindings returned by Palette. UE creates their base Pins:

```sal
patch eventGraph [dry run]

delay = node(palette: "palette-entry-id")
add delay
```

Graph Patch operations are:

```sal
add delay
add delay pin@source-id -> delay.execute
add delay delay.then -> pin@target-id

connect pin@source-id -> pin@target-id
disconnect pin@source-id -> pin@target-id
break pin@id

insert pin@source-id -> delay.execute/then -> pin@target-id

set node@id.NativeField = value
set pin@id.NativeField = value
reset node@id.NativeField
reset pin@id.NativeField
move node@id to (x, y)
move node@id by (dx, dy)
remove node@id

invoke eventGraph Operation(namedArguments) [as alias]
invoke node@id Operation(namedArguments) [as alias]
invoke pin@id Operation(namedArguments) [as alias]
```

`add` may connect one side of a new Node. Two-sided replacement uses `insert`.
`disconnect` removes one exact Edge; `break` performs UE Break All Pin Links.
Do not declare raw Pins or guess constructor names, Classes, Palette ids,
fields, Pins, or Operation parameters.

Dry run executes the same ordered Patch against an isolated transient duplicate
of the entire owning Blueprint, including generated Class state, Timeline
Templates, and detached internal Curves. It returns current live Object Text,
not transient object ids. Its transient top-level transaction is canceled after
planning so no undo record survives. Graph Patch requires one available
top-level UE Editor transaction. Any live apply failure ends that transaction
and immediately undoes it; live transaction cancellation is used only for a
successful all-no-op Patch.

Both dry run and apply return the same structured `planned` data: ordered
operations with source indexes, bindings/references/invoke names, followed by
safe effects. Created transient objects are represented only by their source
aliases; only touched ids that resolve back to the live Graph may appear.

## Palette

```sal
query eventGraph
palette entries "Print String"
```

Copy the returned `node(palette: ...)` binding into Patch. Patch re-resolves
and validates the Palette id in the current Graph context before creation.

## Finalize

Graph Patch does not finalize its owning asset. For a Blueprint Graph, compile
and save through a separate exact Blueprint request:

```sal
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```

Graphs owned by another asset type follow that owner's interface card.
