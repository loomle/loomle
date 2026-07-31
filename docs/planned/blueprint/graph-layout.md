# Blueprint Graph Layout

## Status

The read-side design below is confirmed and awaits implementation. It extends
the existing `with layout` detail; it does not add a Query operation, result
object, Target kind, or MCP tool.

Current SAL returns stored Node layout through `with layout` and can move exact
Nodes to explicit coordinates. It does not yet return authoritative Slate
geometry, plan a layout, or automatically format a graph.

Automatic layout mutation remains a later design. The Query contract in this
document supplies facts only. The agent chooses what to change, and Graph Patch
expresses the exact mutation set.

## Intent

Near-human Blueprint layout needs trustworthy knowledge of what the UE Graph
Editor presents. Stored coordinates are enough for rough placement, but stored
Node dimensions are not a substitute for rendered Node and Pin geometry.

The public workflow stays within existing SAL boundaries:

1. an ordinary Graph Query selects objects;
2. `with layout` adds the best available layout fields to the Node objects
   (including Comments) and Pin objects that Query already returns;
3. the agent interprets those facts and chooses a layout plan;
4. `sal_patch` moves the exact Nodes chosen by the agent;
5. a later Query verifies the result.

Query selection, agent planning, and Patch mutation are separate concerns. A
Query page is not an atomic layout region, and `with layout` does not decide
which objects a later Patch may change.

## UE 5.7 Source Facts

Relevant UE source:

- `Editor/GraphEditor/Private/GraphEditorActions.cpp`
- `Editor/GraphEditor/Private/SGraphEditorImpl.h`
- `Editor/GraphEditor/Private/SGraphEditorImpl.cpp`
- `Editor/GraphEditor/Private/SGraphPanel.cpp`
- `Editor/GraphEditor/Private/SGraphNode.cpp`
- `Editor/GraphEditor/Private/SGraphNodeComment.cpp`
- `Editor/GraphEditor/Private/SGraphNodeKnot.cpp`
- `Editor/GraphEditor/Private/SGraphPin.cpp`
- `Editor/GraphEditor/Private/SNodePanel.cpp`
- `Editor/GraphEditor/Private/ConnectionDrawingPolicy.cpp`
- `Editor/BlueprintGraph/Private/EdGraphSchema_K2.cpp`
- `Editor/Kismet/Private/BlueprintEditor.cpp`
- `Runtime/Engine/Classes/EdGraph/EdGraphNode.h`
- `Runtime/Engine/Private/EdGraph/EdGraphSchema.cpp`

UE exposes native commands for aligning, distributing, stacking, and
straightening selected Nodes. Those commands are live Graph Editor operations:

- alignment, distribution, and stacking use measured Node bounds;
- straightening uses live Node and Pin widgets;
- selection, hover, focus, and the active Graph surface influence which native
  command acts;
- movement eventually writes `NodePosX` and `NodePosY` through schema-aware
  Node positioning inside an editor transaction.

These are useful native primitives, but they are not a headless formatter for
an arbitrary set chosen by an agent.

`SNodePanel::GetBoundsForNode` derives a Node rectangle from its live widget
position and desired size. `NodeWidth` and `NodeHeight` are normally zero for
non-resizable Nodes, so they are not a general rendered-size fallback.
`UEdGraphSchema_K2::EstimateNodeHeight` is explicitly a best-effort estimate
used before a later Slate tick and is not authoritative visual geometry.

Pin placement is Slate state. A Pin row, its center, and its connection image
are properties of its individual `SGraphPin`; two Pins on opposite sides of one
row may have the same Y coordinate while remaining distinct Pin objects.

An off-viewport Node can still be measured when its real `SGraphNode` exists on
the live panel. The provider must prepass that widget with the panel's effective
child layout scale, construct its arranged geometry, and walk the Node's local
widget tree through public Slate arrangement APIs. It must not treat a
visible-child-only panel search or an unticked `SGraphPin::CachedNodeOffset` as
authoritative.

Comment membership is a full-rectangle containment relationship over visual
Node bounds. A Comment's body bounds include its authored Comment box and title
area. Shadows, external bubbles, tooltips, wires, selection outlines, and
resize hit borders are presentation adornments rather than body bounds.

A Knot remains one real `UK2Node_Knot` with two distinct Pin identities even
when their visual centers or placement anchors coincide. It is transparent to
compilation, not to layout.

## Public Query Contract

### Existing Syntax Only

No new Query syntax is introduced. Existing Graph operations continue to opt
into layout with the existing detail:

```sal
query g
@node-guid
with layout
```

```sal
query g
context @node-guid depth 3
with layout
```

```sal
query g
exec flow from @node-guid depth 5
with layout
```

```sal
query g
nodes
where type = "/Script/BlueprintGraph.K2Node_CallFunction"
with layout
page limit 50
```

The underlying operation keeps deciding which objects are returned:

- `nodes` remains a compact, paginated Node collection;
- an exact Node keeps returning that Node and its current Pins;
- an exact Pin keeps returning its compact owner and that Pin;
- `context`, execution-flow, and data-flow keep their existing traversal and
  boundary projections;
- Edges and Comments are not implicitly added merely because layout was
  requested.

`with layout` enriches existing returned objects. It does not widen the
operation's semantic projection and does not return a synthetic layout,
capture, status, snapshot, or region object.

Pagination also keeps its ordinary Query meaning. `page.next` says only that
more matching objects can be queried. An agent may collect any pages or other
queries it needs and may later Patch any exact Nodes it chooses.

### Stored Node Fields

Every returned Node selected with `with layout` keeps the existing stored
fields:

```sal
at: [640, 0]
size: [240, 120]
```

- `at` is the exact stored integer `NodePosX/NodePosY`.
- `size` is present only when UE stores a non-zero
  `NodeWidth/NodeHeight`.
- `size` is not estimated and does not claim to be the current Slate size.
- The meaning of `at` and `size` never changes when a Graph Editor opens or
  closes.

These fields support semantic inspection and conservative rough placement.
They must not be used as rendered collision bounds unless the particular Node
class gives that meaning to its stored size.

### Authoritative Visual Fields

When the exact Graph has a usable live Graph Editor surface and measurement
succeeds, `with layout` adds visual facts directly to the returned objects.
There is no separate status object.

Node example:

```sal
print = node {
  id: "dddddddd-dddd-dddd-dddd-dddddddddddd",
  type: "/Script/BlueprintGraph.K2Node_CallFunction",
  at: [400, 0],
  visualBounds: [400.0, 0.0, 640.0, 120.0]
}
```

Measured Pin example:

```sal
print.execute = pin {
  id: "33333333-dddd-dddd-dddd-dddddddddddd",
  type: "<FEdGraphPinType native text>",
  direction: in,
  visualState: measured,
  visualBounds: [400.0, 36.0, 432.0, 60.0],
  visualCenter: [416.0, 48.0],
  placementAnchor: [412.0, 48.0],
  placementAnchorKind: pin_image_center
}
```

Intentionally unpresented Pin example:

```sal
print.advancedInput = pin {
  id: "55555555-dddd-dddd-dddd-dddddddddddd",
  type: "<FEdGraphPinType native text>",
  direction: in,
  visualState: intentionally_not_presented,
  geometryReasons: [hidden_advanced]
}
```

Comment example:

```sal
regionComment = node {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "/Script/UnrealEd.EdGraphNode_Comment",
  at: [-64, -96],
  size: [1024, 480],
  visualBounds: [-64.0, -96.0, 960.0, 384.0]
}
```

The field contract is:

| Object | Field | Meaning |
| --- | --- | --- |
| Node | `visualBounds` | `[left, top, right, bottom]` for the Node body in graph-space Slate logical units. |
| Pin | `visualState` | `measured` or `intentionally_not_presented`. |
| Pin | `visualBounds` | Complete arranged `SGraphPin` row bounds in graph space. |
| Pin | `visualCenter` | Center of the Pin row; its Y value is the row-alignment coordinate used by native straightening. |
| Pin | `placementAnchor` | Pin image center when available, otherwise the direction-appropriate row-edge midpoint. |
| Pin | `placementAnchorKind` | `pin_image_center` or `pin_row_edge_midpoint`. |
| Pin | `geometryReasons` | Ordered reasons why an intentionally unpresented Pin has no visual geometry. |

All `visual*` coordinates are graph-space values. No per-response coordinate
space object is required. Node centers, sizes, unions, and Comment containment
can be derived from the returned bounds.

`visualBounds` is a bounds tuple, not a position-and-size tuple:

```text
[left, top, right, bottom]
```

Node body bounds include the core Node widget and its internal title and
content. They exclude external adornments. Comment Node bounds represent the
full Comment box. Knot bounds and the geometry of each of its two Pins remain
separate.

Every returned Node with authoritative geometry must have finite,
positive-area `visualBounds`. Every returned Pin must use exactly one of these
forms:

1. `visualState: measured`, with finite positive-area `visualBounds`, a finite
   `visualCenter`, a finite `placementAnchor`, and
   `placementAnchorKind`; or
2. `visualState: intentionally_not_presented`, with no visual coordinates and
   a non-empty `geometryReasons`.

The closed ordered `geometryReasons` values are:

1. `hidden_native`;
2. `hidden_advanced`;
3. `hidden_unconnected`;
4. `hidden_unconnected_no_default`.

Standard hidden, advanced, and panel filters do not remove a connected Pin.
If a custom Node presentation or low LOD makes a required connected Pin
unmeasurable, authoritative enrichment is unavailable for the response rather
than fabricating a Pin position.

`placementAnchor` is useful for placement and wire reasoning, but it is not a
promise of the final rendered Bezier endpoint. A schema-specific connection
drawing policy may add spline and arrow offsets.

### Availability And Fallback

Authoritative visual geometry is available only from the actual live Graph
Editor surface presenting the exact Target Graph. Opening the Blueprint asset
alone is insufficient when that exact Graph has no live surface.

The Node does not need to be inside the current viewport. The exact Graph
surface must, however, exist and be Slate-synchronized. Surface selection is:

1. use the focused live surface when it presents the exact Target Graph;
2. otherwise use the one unique matching live surface;
3. treat no match or an ambiguous match as unavailable.

Measurement must not run while a conflicting drag, resize, pan, relink, or
other captured interaction is in progress. It must observe a stable
post-update Slate state and a consistent Node/Pin widget inventory.

The provider may prepass and arrange the requested live Node widgets without
moving the viewport or changing user state. It must not arrange the whole
panel, invoke a second-pass layout path that can write Node positions, or
measure-then-restore a mutation.

If authoritative geometry cannot be obtained, the ordinary Query still
returns its stored Node fields and native Node/Pin facts. It omits every
visual-layout field from every applicable returned object and emits the
structured warning:

```text
capability.layout_geometry_unavailable
```

The warning retains the exact Graph Target, identifies the underlying Query
operation, and uses:

```json
{
  "actual": {
    "detail": "layout",
    "reason": "graph_not_open"
  },
  "suggestion": "Open the exact Graph Editor surface, then retry the same Query."
}
```

The closed reason set is:

- `graph_not_open`;
- `surface_ambiguous`;
- `slate_unavailable`;
- `interaction_in_progress`;
- `visual_sync_pending`;
- `layout_scale_unavailable`;
- `node_widget_unavailable`;
- `pin_widget_unavailable`;
- `second_pass_layout_unavailable`;
- `unsupported_widget_geometry`;
- `prepass_incomplete`;
- `non_finite_geometry`;
- `non_positive_bounds`.

Reason-specific suggestions tell the agent to open the exact Graph, focus a
matching surface, finish the interaction, wait for Slate synchronization, retry
the same Query, or report that the current widget cannot be measured safely.

This is a progressive read, not an error merely because the editor is closed.
Syntax, Target resolution, StableRef resolution, native inventory corruption,
and result-size failures keep their existing error behavior and are not hidden
behind stored-layout fallback.

### Response-Wide Consistency

One successful response must not mix authoritative visual fields with stored
fallback:

- authoritative success adds complete visual fields to every applicable
  returned Node and Pin;
- visual unavailability adds no `visualBounds`, `visualState`,
  `visualCenter`, `placementAnchor`, `placementAnchorKind`, or
  `geometryReasons` anywhere in the response and emits the mandatory warning.

An intentionally unpresented Pin is part of authoritative success because its
absence is explained by current presentation rules. A merely missing required
widget is not.

An agent therefore gates precise layout on both:

1. absence of `capability.layout_geometry_unavailable`; and
2. complete visual field shapes on all applicable returned objects.

Stored `at` and optional `size` remain useful when that gate fails, but they
authorize only rough placement and must never be relabeled or extrapolated as
Slate measurements.

The consistency guarantee belongs to one Query response. It does not turn the
response, a page, or several collected pages into a mutation region. Multiple
queries are ordinary observations made at their respective execution times.

## Query And Patch Separation

The Query reports current facts. The agent owns layout policy, including:

- which Nodes to consider;
- which queried pages or traversals to inspect;
- alignment and spacing choices;
- anchor Nodes;
- treatment of cycles, comments, reroutes, and shared paths;
- which exact Nodes should ultimately move.

Graph Patch owns the actual mutation set. Existing explicit movement remains:

```sal
patch g dry run
move @first-node-guid to (320, 0)
move @second-node-guid to (720, 0)
```

The Patch changes only the Nodes named by its operations. Query pagination,
query traversal boundaries, editor selection, focus, hover, and viewport do
not add or remove Patch targets.

The agent should re-query relevant objects before planning when earlier reads
may be stale, use dry run where supported, apply the explicit Patch, and query
again to verify resulting stored and visual layout. Query does not expose a
layout revision or geometry fingerprint in this design. A future Patch may
accept a concurrency token only after its real dry-run and apply paths enforce
that token.

Compilation and save remain explicit terminal operations on the owning
Blueprint. Neither `with layout` nor Node movement implicitly compiles or
saves.

## Read Side-Effect Boundary

A Query with `with layout` must not:

- open or switch a Blueprint or Graph tab;
- change focus, selection, hover, Pin visibility, zoom, pan, or viewport;
- create an undo transaction;
- call `Modify()`, move a Node, reconstruct Nodes, or dirty a Package;
- compile or save;
- retain a persistent server-side snapshot.

Updating read-only prepass and descendant-arrangement caches for already live
Node widgets is allowed when it has no user-visible or authored-state effect.

## Acceptance Matrix

Implementation is not complete until the public `sal_query` path verifies:

### Protocol And Projection

- no new Query operation or grammar is introduced;
- `with layout` remains available only on the Graph operations already
  advertising it;
- no synthetic layout, status, capture, snapshot, or region object is returned;
- `nodes`, exact reads, context, and flows preserve their existing object and
  pagination projections;
- `page.next` retains only its ordinary pagination meaning;
- stored `at` and optional `size` retain their exact existing semantics;
- Editor state never changes which Node, Pin, Edge, or Comment objects the
  underlying Query returns.

### Live Geometry

- ordinary K2 Nodes with short and long titles and defaults return accurate
  graph-space `visualBounds`;
- off-viewport Nodes are measured from their real widgets without changing the
  viewport or trusting stale cached offsets;
- pan, window position, and view scale do not leak into graph-space values;
- every measured Pin returns valid row bounds, center, placement anchor, and
  anchor kind;
- input and output Pins that share a row remain separate Pin objects;
- every intentionally unpresented Pin returns the closed ordered reasons and no
  visual coordinates;
- connected Pins never disappear under the standard hidden-Pin reasons;
- Comments return full box bounds;
- Knots retain two distinct Pin identities and geometries even when points
  coincide;
- missing, incomplete, second-pass-dependent, or non-finite geometry is never
  estimated.

### Availability And Consistency

- an open, unique, synchronized exact Graph surface can return visual fields;
- a closed Graph, ambiguous surface, active interaction, pending visual sync,
  unsupported widget, or invalid geometry returns stored facts, no visual
  fields, and the registered warning with the exact reason and actionable
  suggestion;
- one response is wholly authoritative or wholly without visual enrichment;
- visual fallback does not replace syntax, Target, identity, inventory, or
  result-budget errors;
- retrying the same Query after opening or synchronizing the Graph can upgrade
  the returned objects without changing `at` or `size` semantics.

### Safety

- measurement creates no transaction or dirty state;
- measurement does not change selection, focus, Pin visibility, viewport,
  Node position, compile state, or save state;
- exact Target context is retained on post-resolution warnings and errors;
- result ordering remains the ordering of the underlying Query;
- existing result-size limits apply without silent truncation or a changed
  object projection.

## Deferred Work

The following remain outside this read-side design:

- authoritative synthetic or headless Slate geometry;
- wire spline geometry, crossings, bubbles, shadows, tooltips, and screenshots;
- automatic layout planning and quality scoring;
- native align, distribute, stack, and straighten Patch operations;
- deterministic formatting, anchors, collision avoidance, and Comment fitting;
- reroute synthesis or deletion;
- persistent snapshots or cross-Query atomic geometry;
- exposed layout revisions or geometry fingerprints;
- cross-platform bitwise geometry equality.

Any later layout mutation must follow the shared mutation dry-run contract,
resolve every Patch target inside one exact Graph, validate its complete plan
before applying, use UE transactions and schema-aware movement, preserve Graph
behavior, and refuse partial application when its own declared prerequisites
are missing.
