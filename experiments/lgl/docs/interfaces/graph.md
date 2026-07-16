# graph

Inspect and edit one resolved UE Graph and its Nodes, Pins, and Edges. This
interface assumes the resident LGL core guide and does not repeat Core syntax.

## Target

A Graph request includes its exact asset-backed owner:

```lgl
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

```lgl
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
palette entries ["text"] [from|to pin@id]

query eventGraph
palette @id
```

Traversal stays inside the target Graph and depth defaults to 1. `with layout`
adds stored Node position and size to `nodes`, exact Node or Pin reads,
`context`, `exec flow`, and `data flow`. `with schema` is valid on an exact
Graph, Node, Pin, or Palette Entry. Exact Palette Entries may also support
`with pins` and `with defaults`.
`nodes` supports `=` and `!=` on `type`, `id`, and `NodeComment`, plus `~=` on
`NodeComment`; ordering keys are `type` and `id`. `palette entries` supports Pin
context; `widget`, `component`, and `actor` accept only `=` and are mutually
exclusive, while Boolean `contextSensitive` accepts `=` and `!=` and defaults
to true. Palette ordering keys are `name`, `category`, and `id`. Both
collections use cursor pagination with a default limit of 50. Ordered
comparisons are unsupported.

Graph Summary returns semantic entry Nodes, disconnected-region
representatives, and compact counts. It does not expand the whole Graph.
`node@id` returns the complete Node and its Pins; `pin@id` returns its compact
owner and the complete Pin without traversing links.

An execution-flow result remains ordinary ordered Object Text:

```lgl
beginPlay = node(
  graph: eventGraph,
  id: "begin-node-guid",
  type: "/Script/BlueprintGraph.K2Node_Event"
)
# Event BeginPlay

beginPlay.then = pin(
  id: "then-pin-guid",
  type: "<FEdGraphPinType native text>",
  direction: out
)

branch = node(
  graph: eventGraph,
  id: "branch-node-guid",
  type: "/Script/BlueprintGraph.K2Node_IfThenElse"
)
# Branch

branch.execute = pin(
  id: "execute-pin-guid",
  type: "<FEdGraphPinType native text>",
  direction: in
)

beginPlay.then -> branch.execute
```

## Dynamic Schema

Inspect the current Graph's actual Query and Graph-level Operations:

```lgl
query eventGraph
graph@graph-guid
with schema
```

Inspect one existing object:

```lgl
query eventGraph
node@node-guid
with schema
```

Inspect one creation capability:

```lgl
query eventGraph
palette @palette-entry-id
with schema
```

The schema comment gives exact fields, constraints, available Operations,
parameters, outputs, and copyable `invoke` templates for the resolved UE
context.

## Patch

New Nodes use bindings returned by Palette. UE creates their base Pins:

```lgl
patch eventGraph [dry run]

delay = node(palette: "palette-entry-id")
add delay
```

Graph Patch operations are:

```lgl
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

## Palette

```lgl
query eventGraph
palette entries "Print String"
```

Copy the returned `node(palette: ...)` binding into Patch. Patch re-resolves
and validates the Palette id in the current Graph context before creation.

## Finalize

Graph Patch does not finalize its owning asset. For a Blueprint Graph, compile
and save through a separate exact Blueprint request:

```lgl
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```

Graphs owned by another asset type follow that owner's interface card.
