# graph

Inspect and edit one resolved UE Graph and its Nodes, Pins, and Edges.

## Target

A discovery Query may use exact Graph name or GraphGuid and may omit
`blueprintId`. Canonical exact Queries and every Patch use:

```sal
eventGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

The same flat Target covers top-level and child/collapsed Graphs.

## Identity

```sal
@node-guid
@node-guid/pin-guid
```

A Pin always includes its owning NodeGuid. Optional tags are presentation only:

```sal
node @node-guid
pin @node-guid/pin-guid
```

Graph Domain may also expose declared owning-Blueprint identities for reads and
references, but resolving them does not grant Blueprint mutation authority.

## Query

```sal
target
summary
nodes ["text"]
@identity
context @identity [depth N]
exec flow from|to @identity [depth N]
data flow from|to @identity [depth N]
references to <exact-subject> [in project]
palette entries ["text"] [from|to @node-guid/pin-guid]
palette @id
```

Use the structural operation `target` to read Graph Target state and schema:

```sal
query eventGraph
target
with schema
```

Traversal stays inside the exact Graph and defaults to depth 1. `with layout`
adds stored Node position and size. Exact Node reads include current Pins;
exact Pin reads include its compact owner. Exact objects and Palette entries
may use `with schema`.

`nodes` filters on `type`, `id`, and `NodeComment`; Palette filters and ordering
are closed by the static and exact schema. Collections use cursor pagination.

Every Graph Query outside Summary places current UE health comments beside
returned existing Nodes and Pins. Summary instead returns one complete compact
health index.

## Object Text

```sal
beginPlay = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}

beginPlay.then = pin {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "<FEdGraphPinType native text>",
  direction: out
}
```

The `node` and `pin` tags are erasable. The exact Graph Target, native fields,
and references supply the meaning.

Edges connect Pins:

```sal
beginPlay.then -> sequence.execute
```

There is no Edge UObject or Edge identity.

## Patch

```sal
patch eventGraph [dry run]

delay = { palette: "palette-entry-id" }
add delay

connect @source-node-guid/source-pin-guid ->
  @target-node-guid/target-pin-guid
disconnect @source-node-guid/source-pin-guid ->
  @target-node-guid/target-pin-guid
break @node-guid/pin-guid

insert @source-node-guid/source-pin-guid ->
  delay.execute / delay.then ->
  @target-node-guid/target-pin-guid

set @node-guid.NodeComment = "Wait briefly"
set @node-guid/pin-guid.DefaultValue = "1.0"
reset @node-guid/pin-guid.DefaultValue
move @node-guid to (640, 0)
remove @node-guid

invoke @node-guid Operation(namedArguments) [as alias]
invoke @node-guid/pin-guid Operation(namedArguments) [as alias]
```

Palette creates each base Node and its Pins through UE. Raw Pin creation is
invalid. `insert` atomically replaces one existing Edge. Exact schema is
authoritative for fields, operations, arguments, outputs, and availability.

Dry run executes the same ordered native edit path against an isolated
transient owner and returns only stable live identities or creation aliases.

## Finalization Handoff

Graph Domain does not compile or save its owning Blueprint. A Graph result
supplies a related Blueprint Target:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

Compile and save in a following Blueprint request.
