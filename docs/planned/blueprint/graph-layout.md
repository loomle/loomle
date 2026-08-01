# Blueprint Graph Layout

## Status

The current implementation covers the read-side `with layout` enrichment and
the explicit Graph `move` contract below. The read side keeps stored layout
facts and conditionally adds authoritative live Slate geometry. Graph movement
is narrowed to absolute `to`, with exact per-move planning and move-only rich
diffs; Graph `by` is retired. Neither part adds a Query operation, Patch
operation, result object, Target kind, or MCP tool.

This document is now the design and audit record for that implementation.
Interface-level automation covers the closed-Graph fallback and one
representative live Slate scenario. The acceptance matrix remains the required
behavior, not a claim that every live Slate variant has dedicated coverage.
Automatic layout planning and formatting are still outside the implementation.

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

Pin placement is Slate state. UE finds and arranges the individual `SGraphPin`
widgets when drawing connections and uses each Pin widget's allotted vertical
center for straightening. At normal detail the Pin presentation contains its
full horizontal row; at low Graph LOD, `SLevelOfDetailBranchNode` replaces that
row with the compact Pin image. The full-detail row is therefore not a universal
geometry source. Two Pins on opposite sides of one Node row may have the same Y
coordinate while remaining distinct Pin objects.

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
| Pin | `visualBounds` | Complete bounds of the currently arranged `SGraphPin` presentation in graph space: its full row at ordinary detail or its compact Pin presentation at low LOD. |
| Pin | `visualCenter` | Center of the current Pin presentation; its Y value is the alignment coordinate used by native straightening. |
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
Low LOD remains measurable from UE's arranged `SGraphPin` and Pin-image
geometry; the provider must not require the inactive full-detail row. If a
custom Node presentation makes the required `SGraphPin` itself unmeasurable,
authoritative enrichment is unavailable for the response rather than
fabricating a Pin position.

`placementAnchor` is useful for placement and wire reasoning, but it is not a
promise of the final rendered Bezier endpoint. A schema-specific connection
drawing policy may add spline and arrow offsets.

### Availability And Fallback

Authoritative visual geometry is available only from the actual live Graph
Editor surface presenting the exact Target Graph. Opening the Blueprint asset
alone is insufficient when that exact Graph has no live surface.

UE may host a standalone asset editor as a native child `SWindow` rather than
as an entry in Slate's top-level window array. Surface discovery therefore
walks each interactive top-level window and its recursive `GetChildWindows()`
hierarchy, matching UE's own `FSlateWindowHelper` traversal. Scanning only the
ordinary widget children of top-level windows can incorrectly report
`graph_not_open` for a visible, focused standalone Blueprint Editor.

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

## Explicit Graph Move Contract

### Scope And Syntax

This design covers only the existing Graph Patch `move` operation:

```sal
patch g dry run
move @first-node-guid to (320, 0)
move @second-node-guid to (720, 32)
```

It does not add `layout`, `format`, `align`, `distribute`, `stack`, or
`straighten` Patch operations. It does not redefine the planning, diff, or
apply semantics of other Graph Patch operations.

A Graph move targets exactly one Node in the bound exact Graph. Comments and
Knots are Nodes and can be moved by their own exact identities. A Pin, Graph,
editor selection, Query result, Query page, traversal boundary, viewport, or
Comment-contained set is not an implicit move target.

Each Graph move has exactly one absolute placement:

```sal
move @node-guid to (x, y)
```

Graph does not support relative `by (dx, dy)` movement. The domain-independent
SAL parser may still recognize that generic Move clause, but the Graph
interface does not advertise it and rejects it with
`capability.clause_unavailable`. An agent that wants relative movement first
reads the current stored `at`, computes an absolute target, and emits `to`.
This is an intentional breaking narrowing of the current Graph interface;
Graph documentation, examples, schemas, and tests must migrate existing `by`
usage to absolute `to`.

Absolute movement is idempotent: repeating the same move requests the same
stored position rather than accumulating another translation.

Patch statement order is authoritative. Multiple moves of the same Node are
evaluated sequentially, so each later statement observes the effective stored
position produced by the preceding statement.

### Coordinate Semantics

Every Graph move point must contain exactly two finite mathematical integers
within the signed 32-bit range:

```text
[-2147483648, 2147483647]
```

Fractional values and out-of-range values are invalid. The provider must not
truncate, round, saturate, wrap, or otherwise coerce them.

UE 5.7's current schema movement override accepts `FVector2f`, while stored
`NodePosX/NodePosY` are signed 32-bit integers. Converting a valid requested
component to the `FVector2f` component and back to double precision must produce
the same mathematical integer. Every integer in the continuous range below is
guaranteed to satisfy that rule:

```text
[-16777216, 16777216]
```

Some signed 32-bit integers outside that continuous range are also exactly
representable and remain valid. A value that does not round-trip exactly is
invalid rather than silently changing position. Loomle must use the modern
`FVector2f` schema override; it must not bypass that UE extension point through
the deprecated double-vector overload.

The before and after values exposed by move planning are stored `at`
coordinates, not visual bounds or estimated positions. Movement does not
change the meaning of `at` or `size`.

### Move Planning And Diff

The existing Graph Patch plan remains the enclosing result model. This design
adds fields only to `move` entries in `planned.operations`; it does not invent
a second move-plan object.

An absolute move entry is:

```json
{
  "index": 0,
  "operation": "move",
  "ref": "@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  "to": [320, 0],
  "before": { "at": [128, 0] },
  "after": { "at": [320, 0] },
  "changed": true
}
```

The field rules are:

- `index` is the zero-based Patch statement index;
- `ref` is the existing stable Node reference text, or the source local alias
  when the Node was created earlier in the same Patch;
- `to` preserves the normalized absolute request;
- `before.at` is the exact stored position immediately before this statement
  executes in preflight;
- for a changed move, `after.at` is the stored position read back after
  schema-aware movement in preflight and exactly equals `to`;
- for a no-op, `after.at` equals `before.at` without invoking schema movement;
- `changed` is exactly whether `before.at` and `after.at` differ.

Every valid move remains in `planned.operations`, including a no-op:

```json
{
  "index": 2,
  "operation": "move",
  "ref": "@cccccccc-cccc-cccc-cccc-cccccccccccc",
  "to": [900, 0],
  "before": { "at": [900, 0] },
  "after": { "at": [900, 0] },
  "changed": false
}
```

For a Patch whose statements are all moves, `diff` is a complete ordered move
change set:

```json
{
  "changedOperations": 1,
  "scope": "graph",
  "changes": [
    {
      "index": 0,
      "kind": "move",
      "target": {
        "kind": "stable_ref",
        "identityPath": [
          "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
        ],
        "semanticTag": "node"
      },
      "before": { "at": [128, 0] },
      "after": { "at": [320, 0] }
    }
  ]
}
```

`changedOperations` preserves the existing coarse count. `diff.changes` omits
no-op moves, preserves Patch statement order, and uses the shared mutation
contract's structured `target` identity.

For dry run, `changes` is mechanically derived from the `changed: true` move
entries in `planned.operations`. For successful live apply, it is built from
the verified actual before/after readback; the same-request parity checks below
guarantee that those values equal the live request's plan.

The rich `scope` and `changes` fields are returned only when every Patch
statement is a move. A mixed Graph Patch must not return a partial move
`changes` array as if it were complete. Existing coarse diff fields remain
unchanged, and rich diff coverage for other Graph operations remains separate
work.

The complete result matrix is:

| Patch shape | Execution | `diff` |
| --- | --- | --- |
| Move-only | Successful dry run | Preflight `changedOperations`, `scope: "graph"`, and planned `changes` |
| Move-only | Successful live apply | Actual `changedOperations`, `scope: "graph"`, and actual verified `changes` |
| Mixed | Successful dry run | Omitted |
| Mixed | Successful live apply | Existing coarse `changedOperations` only; `scope` and `changes` omitted |

A successful all-no-op move-only Patch still returns a complete empty diff:

```json
{
  "changedOperations": 0,
  "scope": "graph",
  "changes": []
}
```

A failed preflight returns no partial `planned` or `diff`. On a successful dry
run, the ordinary Object Text remains a readback of the current live touched
objects; it must not pretend to be a hypothetical after-state. The move
`planned` and `diff` metadata carry the hypothetical effects.

### Dry Run And Live Apply

A Graph move dry run is an advisory plan computed from asset state observed
during that request. It does not reserve the Graph, lock Node positions, or
produce a revision, plan token, or other value that a later request commits.

A later live Patch performs a fresh complete preflight against then-current
state. Its `planned` and `diff` belong to that live request and may differ from
an earlier dry run when the asset changed between requests. This design does
not expose `expectedRevision` or promise cross-request atomicity.

Within one live request, move apply must use the bound Graph's
`UEdGraphSchema::SetNodePosition` rather than writing `NodePosX/NodePosY`
directly. The schema call preserves UE's movement override point and its native
`Modify()` path. A move whose `before.at` already equals `to` is a no-op and
must skip `SetNodePosition` and `Modify()`, must not increment the Patch change
count, and must not itself cause dirtying, Graph notification, or Undo state.
An otherwise all-no-op Patch therefore has none of those effects.

A move-only live Patch whose complete preflight contains no changed moves must
return success before opening the live top-level transaction or calling
`Blueprint->Modify()` or `Graph->Modify()`. This keeps the all-no-op guarantee
true at the enclosing Graph Patch layer rather than only at the Node handler.

Preflight calls the same modern schema movement path for a changed move. If its
stored readback differs from the exact requested `to`, preflight fails rather
than treating schema adjustment, float coercion, snapping, or truncation as a
new effective request.

For every move, live apply verifies both:

1. the stored position immediately before the statement equals that move
   plan's `before.at`;
2. for a changed move, the stored position read back after `SetNodePosition`
   equals both that move plan's `after.at` and the requested `to`.

Any mismatch fails the Patch and uses the existing Graph Patch atomic
transaction to roll back the entire mutation. The move design relies on the
existing Graph Patch guarantees of one top-level transaction, no partial
success, and one Undo step; it does not redefine those guarantees.

When live preflight succeeded but move parity or a later statement fails during
apply, a successful rollback retains this request's complete preflight
`planned` as the attempted plan, omits `diff` and partial changed-object
readback, and returns `valid: false` and `applied: false`. Existing rollback
failure semantics remain unchanged.

On successful live apply, ordinary Object Text is the actual live touched-object
readback. A live Patch containing only no-op moves is valid with
`applied: false`; one or more changed moves produces `applied: true`.

Patch completion promises stored coordinates only. Slate geometry can update
asynchronously, so the Patch result does not promise refreshed `visualBounds`
or Pin visual fields. The agent re-runs the relevant Query with `with layout`
after apply to verify authoritative visual placement.

### Move Diagnostics

Move-specific failures use these existing or new diagnostics:

| Condition | Diagnostic |
| --- | --- |
| Point text is not exactly two finite numbers | `language.invalid_point` |
| Node reference cannot be resolved | Existing Graph `resolution.*` diagnostic |
| Resolved target is not a Node | `validation.invalid_target` |
| Point has a fractional or out-of-range component | `validation.layout_invalid` |
| Absolute position cannot round-trip exactly through `FVector2f` | `validation.layout_invalid` |
| Graph move uses the unsupported `by` clause | `capability.clause_unavailable` |
| The bound Graph has no usable schema movement path | `capability.operation_unavailable` |
| Schema readback differs from exact `to` or the live move plan | `validation.layout_apply_failed` |

`validation.layout_invalid` identifies the Node and preserves the requested
`to`. The unavailable-clause diagnostic tells the agent to read `at`, compute
the absolute target, and retry with `to`.
`validation.layout_apply_failed` reports the planned and actual position and
invalidates preflight or, during live apply, causes atomic rollback. Existing
transaction and rollback diagnostics retain their current meanings.

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

The following remains the acceptance matrix for the public `sal_query` path.
Interface-level automation verifies the closed-surface fallback, one unique
synchronized live surface, and one response-wide Pin-widget mismatch fallback.
The remaining bullets are requirements, not coverage implied by the aggregate
automation result.

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
- every measured Pin returns valid current-presentation bounds, center,
  placement anchor, and anchor kind at both ordinary and low Graph LOD;
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

### Explicit Move

The following remains the acceptance matrix for the public `sal_patch` path:

Dedicated release-blocker and public-routing automation verifies the core
absolute move, planning and diff, precision, no-op, live readback and Undo,
live parity rollback, dry-run isolation, and `by` rejection paths. The bullets
below remain the complete contract and are not all one-to-one tested.

- existing `move <node> to (x, y)` syntax remains unchanged and no automatic
  layout operation is introduced;
- Graph schema and guidance no longer advertise `by`, and Graph rejects it with
  an actionable `capability.clause_unavailable` diagnostic;
- exact ordinary Nodes, Comments, and Knots move only when explicitly named;
- Pins, Graphs, editor state, Query pages, and Comment membership never become
  implicit move targets;
- absolute moves are idempotent and repeated moves of one Node observe
  statement order;
- integer positions throughout the continuous `FVector2f`-exact range, exact
  representable positions outside it, and non-representable values around the
  precision boundary are distinguished correctly;
- `-16777216` and `16777216` are accepted, `-16777217` and `16777217` are
  rejected, `16777218` is accepted as an exactly representable value outside
  the continuous range, `-2147483648` is accepted, and `2147483647` is
  rejected;
- fractional, signed-32-bit-out-of-range, and non-`FVector2f`-exact positions
  fail without truncation, rounding, source mutation, dirtying, or Undo state;
- movement calls the modern Graph Schema `FVector2f` positioning path and
  accepts only exact requested-position readback;
- every valid move plan entry returns its original `to`, sequential
  `before.at`, exact `after.at`, and exact `changed` value;
- no-op moves remain in `planned.operations` and are omitted from
  `diff.changes`, while skipping schema movement, modification, dirtying,
  notification, and Undo;
- an all-no-op move-only live Patch skips the top-level transaction and outer
  Blueprint/Graph `Modify()` calls and returns a complete empty rich diff;
- a move-only Patch diff preserves `changedOperations`, is complete and
  ordered, uses `scope: "graph"` and structured Node targets, and corresponds
  exactly to its changed plan entries;
- a mixed dry run omits `diff`, while a mixed live Patch retains only the
  existing coarse `changedOperations` and omits move-only rich `scope` and
  `changes`;
- dry run leaves the source Blueprint, Package dirty state, compile state, and
  Undo history unchanged while returning current live Object Text and planned
  move effects;
- live apply re-preflights current state, uses one existing Graph Patch
  transaction, returns actual stored coordinates, and creates at most one Undo
  step;
- a live before/after mismatch or later Patch failure rolls back every move
  rather than returning partial success;
- sandbox readback that differs from exact `to` fails preflight without
  `planned` or `diff`, while a live-only parity mismatch retains the attempted
  `planned`, rolls back, and omits `diff` and partial changed-object readback;
- successful no-op apply is valid with `applied: false`, while any actual move
  produces `applied: true`;
- an earlier dry run supplies no cross-request reservation or revision
  guarantee;
- post-apply visual verification requires a later Query with `with layout`.

## Current Audit Conclusion

Implemented behavior:

- `with layout` preserves stored `at` and optional stored `size`, conditionally
  enriches the original Node and Pin objects from one exact usable live Graph
  surface, and applies response-wide visual fallback with
  `capability.layout_geometry_unavailable`;
- precise-use gating is the absence of that warning plus complete applicable
  visual field shapes; the fallback path remains useful only for rough
  placement;
- Graph movement accepts absolute `to`, uses exact stored-coordinate planning,
  and supplies move-only rich diffs without adding automatic layout policy.
- a vendor-neutral `format-unreal-blueprints` Agent Skill now captures the
  first agent-side formatting policy, SAL workflow, and golden examples; the
  repository-root source is copied into plugin `Resources/AgentSkills` during
  packaging and remains distinct from an engine-side automatic layout planner.

Known limitations and remaining audit work:

- the live surface discovery path observes visible `SGraphEditor` widgets
  reachable from interactive top-level Slate windows and their native child
  window hierarchies; background, non-standard, or otherwise unreachable Graph
  surfaces are not a separate authoritative source and can fall back as
  unavailable;
- second-pass-dependent Nodes, missing or custom widget geometry, active
  interactions, incomplete synchronization, and invalid measurements
  deliberately fall back for the entire response rather than being estimated;
- Pin capture follows UE's arranged `SGraphPin` path and remains precise when
  low Graph LOD replaces the inactive full-detail row with its compact Pin
  presentation;
- live visual geometry remains an observation of one Query response, with no
  persistent snapshot, revision, or cross-Query atomicity;
- dedicated headless automation covers stored fallback, synthetic `SGraphEditor`
  Node and Pin geometry, intentional Pin non-presentation, response-wide
  fallback after a widget-inventory mismatch, non-unit Graph zoom, absolute
  move planning/diffs, live readback, live-only parity rollback, and Undo
  without viewport, dirty-state, or transaction leakage;
- authoritative surface acceptance is separately covered by a rendered Editor
  automation case that opens a real standalone `FBlueprintEditor`, brings the
  exact Graph document to the foreground through UE's native
  `OpenGraphAndBringToFront` path, verifies its normal native window and focus
  path, sets low Graph LOD, and then exercises the same exact-Node `with layout`
  Query. A `UnrealEditor-Cmd -NullRHI` pass alone does not satisfy this gate;
- the July 31 macOS arm64 candidate built with the Installed UE 5.7 toolchain
  and passed all 138 then-current native tests, but its geometry case was the
  headless synthetic layer above rather than rendered Blueprint Editor proof;
- the August 1 corrective arm64 incremental build passed the rendered
  `LiveGeometry` gate 1/1 with the exact UE 5.7 `UnrealEditor` executable and no
  `-NullRHI`; the isolated headless suite then passed 140/140, including
  `HeadlessSyntheticGeometry` and an explicit non-rendering skip for the
  rendered-only case, with no failure, timeout, runner-classified log hazard,
  or new crash report;
- dedicated variants for ambiguous live surfaces, active interactions,
  off-viewport and custom widgets, second-pass-dependent Nodes, Comments,
  Knots, the remaining hidden-Pin reasons, row-edge anchors, and every Query
  projection remain incomplete even though unavailable cases fail closed and
  the measurable cases follow the same implementation path.

## Deferred Work

The following remain outside this design:

- authoritative synthetic or headless Slate geometry;
- wire spline geometry, crossings, bubbles, shadows, tooltips, and screenshots;
- engine-side automatic layout planning and quality scoring (the packaged
  Agent Skill provides agent-side policy and iterative visual scoring only);
- native align, distribute, stack, and straighten Patch operations;
- deterministic formatting, anchors, collision avoidance, and Comment fitting;
- reroute synthesis or deletion;
- persistent snapshots or cross-Query atomic geometry;
- exposed layout revisions or geometry fingerprints;
- rich before/after diffs for non-move Graph Patch operations;
- cross-platform bitwise geometry equality.

Any later automatic layout mutation must follow the shared mutation dry-run
contract, resolve every Patch target inside one exact Graph, validate its
complete plan before applying, use UE transactions and schema-aware movement,
preserve Graph behavior, and refuse partial application when its own declared
prerequisites are missing.
