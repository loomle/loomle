# LGL Bridge Query Implementation Notes

These notes summarize the existing UE bridge code that should inform the first
`lgl.query` implementation.

The notes are reference material for implementation. They do not change the
spike scope in [`BRIDGE_QUERY_SPIKE.md`](BRIDGE_QUERY_SPIKE.md).

## Existing Code To Read

RPC dispatch:

```txt
engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRpc.inl
```

Existing Blueprint graph and node tool handlers:

```txt
engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeBlueprint.inl
```

Existing Blueprint adapter and UE graph helpers:

```txt
engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBlueprintAdapter.cpp
engine/LoomleBridge/Source/LoomleBridge/Public/LoomleBlueprintAdapter.h
```

General editor graph context and selection helpers:

```txt
engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeModule.cpp
```

The first LGL query spike should read these files as UE behavior references. It
should not call their public tool handlers from production LGL code.

## Useful Existing UE Logic

### RPC Registration And Dispatch

`LoomleBridgeRpc.inl` dispatches current tool names such as:

```txt
blueprint.graph.inspect
blueprint.node.inspect
blueprint.graph.edit
blueprint.palette
```

The LGL path needs a new object RPC entry, starting with:

```txt
lgl.query
```

The dispatch branch should call the new LGL module boundary, not
`BuildBlueprintGraphInspectToolResult` or `BuildBlueprintNodeInspectToolResult`.

### Blueprint Asset And Graph Resolution

Existing graph resolution logic appears in both `LoomleBridgeModule.cpp` and
`LoomleBlueprintAdapter.cpp`.

Important behavior to preserve:

- asset path resolves to a `UBlueprint`
- graph name lookup covers:
  - `UbergraphPages`
  - `FunctionGraphs`
  - `MacroGraphs`
  - implemented interface graphs
- legacy text/tool paths may default to `EventGraph`, but normalized LGL object
  requests must carry an explicit non-empty `GraphRef`
- graph ids use `UEdGraph::GraphGuid`

The LGL implementation should move the focused resolution behavior into
`Private/Lgl/Blueprint/LglBlueprintResolve.*` rather than calling old public
tool handlers.

### Node Identity

Existing node serialization exposes useful identity fields:

```txt
NodeGuid
GetName()
GetNodeTitle(ENodeTitleType::ListView)
GetClass()->GetPathName()
GetPathName()
```

For LGL, the stable internal node id should come from `NodeGuid` when present.
The readable LGL alias should be derived separately and must be unique within a
returned snippet.

The first spike supports the constrained form `find nodes where name = <name>`.
It should not silently choose among multiple matches. If a readable name matches
more than one UE node, return `ambiguous_node` with candidates.

### Pin Readback

Existing pin serialization reads:

```txt
PinName
PinId
Direction
PinType.PinCategory
PinType.PinSubCategory
PinType.PinSubCategoryObject
PinType.ContainerType
DefaultValue
DefaultObject
DefaultTextValue
LinkedTo
```

This is enough for the first `with pins, defaults` response.

For LGL, pins should be mapped to the schema `Pin` object rather than copied as
the old inspect JSON. Keep only fields needed for follow-up query and patch
work.

### Link Readback

Existing code reads links from `UEdGraphPin::LinkedTo` and normalizes direction
when one pin is output and the other is input.

The LGL graph object should emit edges once, using normalized output-to-input
direction when possible. The implementation should guard against duplicate
links because old readback code checks both direct `LinkedTo` and reverse
references in some cases.

### Layout Readback

Existing node layout reads:

```txt
NodePosX
NodePosY
```

Comment node size can use:

```txt
UEdGraphNode_Comment::NodeWidth
UEdGraphNode_Comment::NodeHeight
```

Existing code estimates other node sizes and exec pin anchors. The LGL bridge
should avoid estimated layout in normal asset readback. Stable node layout
belongs behind `with layout`; pin anchors require measured live editor geometry.

Measured Slate geometry is out of scope for the first spike.

## Old Boundaries Not To Reuse

Do not implement `lgl.query` by calling:

```txt
BuildBlueprintGraphInspectToolResult
BuildBlueprintNodeInspectToolResult
blueprint.graph.inspect
blueprint.node.inspect
```

Do not preserve these old response shapes as the LGL object contract:

```txt
semanticSnapshot
nodeEditCapabilities
inspectWith
candidatePins
graphRef
revision
meta
```

Some of these ideas may reappear later as LGL diagnostics, cache metadata, or
patch planning hints, but they should not define the first object boundary.

The old implementation can still be used as:

- source code reference for UE API usage
- comparison-test oracle
- manual debugging tool while the LGL path is being built

## LGL Mapping For The Query Spike

### Empty Query

Old readback source:

```txt
FLoomleBlueprintAdapter::ListGraphNodes
LoomleBlueprintAdapterInternal::SerializeNode
LoomleBlueprintAdapterInternal::SerializePin
```

LGL output:

```txt
ObjectResult.object.kind = "graph"
Graph.target.domain = "blueprint"
Graph.nodes = compact nodes
Graph.edges = normalized links
```

The first response should be compact. It does not need every old inspect field.

### Find Node

Old readback source:

```txt
UEdGraph::Nodes
UEdGraphNode::NodeGuid
UEdGraphNode::GetName()
UEdGraphNode::GetNodeTitle(...)
```

LGL output:

```txt
Graph.nodes = [matched node]
Graph.edges = links adjacent to that node when requested or cheap
```

Resolution order should be conservative:

1. exact node guid/id if the query supplies one later
2. exact LGL alias if aliases are available
3. exact UE `GetName()`
4. exact UE list-view title
5. relaxed matching only if it still yields one match

Ambiguous matches should return diagnostics, not a guessed node.

### Pins And Defaults

Old readback source:

```txt
UEdGraphNode::Pins
UEdGraphPin::DefaultValue
UEdGraphPin::DefaultObject
UEdGraphPin::DefaultTextValue
```

LGL output:

```txt
Pin.name
Pin.direction
Pin.type
Pin.value
```

Defaults should be omitted when UE has no meaningful value rather than filled
with misleading empty strings.

### Layout

Old readback source:

```txt
UEdGraphNode::NodePosX
UEdGraphNode::NodePosY
UEdGraphNode_Comment::NodeWidth
UEdGraphNode_Comment::NodeHeight
```

LGL output:

```txt
Node.at
Node.size
```

Pin layout should not be derived as best-effort `anchor` metadata when measured
editor geometry is unavailable.

## Suggested Implementation Order

1. Add new LGL module registration and a stub `lgl.query` RPC.
2. Decode the request envelope and reject malformed or non-query objects before
   adapter dispatch.
3. Add an adapter registry with only the `blueprint` adapter registered.
4. Implement Blueprint asset and graph resolution in
   `LglBlueprintResolve`.
5. Implement empty query graph readback in `LglBlueprintRead`.
6. Implement `find nodes where name = <name>` matching and ambiguity diagnostics.
7. Add pins, defaults, and layout readback behind requested `with` flags;
   include node `at` and non-zero node `size` when available.
8. Validate or structurally check response objects before encoding.
9. Add accepted and rejected object JSON fixtures.
10. Add a dependency check or review checklist ensuring LGL files do not call
    old public graph inspect handlers.

## UE Behavior Risks To Verify

- Some node titles are localized or context-dependent. Stable LGL ids should
  not depend on title text.
- `GetName()` may be stable enough for debug output but not necessarily for
  long-lived patch references.
- Pin display names can differ from `PinName`; patch references should prefer
  stable pin names.
- Hidden pins should probably be omitted from compact query output unless a
  later query explicitly asks for hidden/internal pins.
- Exec pin layout is estimated without measured Slate geometry.
- Composite subgraphs and interface graphs may need follow-up handling after
  the first EventGraph-focused spike.
- Widget Blueprints are `UBlueprint` subclasses and may need an explicit domain
  or target-kind decision later.

## Acceptance Checklist

Before implementing patch support, the query spike should demonstrate:

- object request decode rejects bad envelopes
- unknown domain fails before Blueprint code runs
- Blueprint asset and graph resolution produce clear diagnostics
- empty query returns schema-shaped graph snippets
- `find nodes where name = <name>` returns one node or an ambiguity diagnostic
- pins/defaults are included only when requested; layout metadata is emitted
  through `at`, `size`, and `anchor` when available
- edge output is normalized and duplicate-free
- implementation files live under `Private/Lgl`
- production LGL code does not call old public inspect handlers
