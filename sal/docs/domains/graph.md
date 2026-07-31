# Graph Domain

## Scope

Graph Domain exposes one exact UE Graph:

- Graph native state;
- Nodes and Pins;
- Edges;
- topology, execution-flow, and data-flow queries;
- Graph Palette;
- Graph-local field, layout, connection, lifecycle, and exact operations.

Blueprint declaration lifecycle and finalization remain Blueprint Domain work.

## Target

Discovery may use GraphGuid or exact current name and may omit `blueprintId`:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  name: "EventGraph"
}
```

Canonical exact readback and every Patch use:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

`asset` is the durable Blueprint container, `blueprintId` verifies its
`BlueprintGuid`, and `id` is `UEdGraph::GraphGuid`. If discovery supplies both
`id` and `name`, name is a strict state assertion; canonical readback drops it.

Targets are flat. Top-level, nested, and collapsed Graphs use this same form.
UE recursively enumerates child Graphs; Graph Domain does not create a nested
Graph Domain or put `graphs` in identity paths.

A duplicate GraphGuid inside one Blueprint is an identity conflict. It is not
resolved by current name or owning Node title.

## Identity Environment

### Nodes

```sal
@node-guid
```

NodeGuid is native identity inside the exact Graph. Copy/paste must create a new
NodeGuid.

### Pins

```sal
@node-guid/pin-guid
```

PinId is owner-local by native contract. The owning NodeGuid is always present,
even if the current Graph happens to contain only one matching PinId.

Duplicate PinId inside the owning Node is `resolution.pin_ambiguous`. Reuse on
another Node or Graph is unrelated.

### Owning-Blueprint Declarations

Graph Domain may expose explicitly declared owning-Blueprint identities needed
for references, Palette context, and Node native member resolution. Function
locals use `@TopLevelFunctionGraphGuid/VarGuid` unless the exact top-level
Function Graph Target already supplies that owner, in which case `@VarGuid`
may be canonical.

Resolving an owning declaration proves identity, not Blueprint mutation
authority.

All categories sharing one path shape are audited together. Tags never
disambiguate them.

## Graph State

The Target table carries exact Graph identity. Requested native state is
ordinary ObjectExpr:

```sal
graphState = {
  id: "22222222-2222-2222-2222-222222222222",
  name: EventGraph,
  type: GT_Ubergraph,
  Schema: "/Script/BlueprintGraph.EdGraphSchema_K2",
  bEditable: true
}
```

`type` is native `EGraphType`. It does not encode Function, Dispatcher
Signature, Interface, Override, Construction Script, or collapsed ownership
role.

The exact Graph itself has no synthetic StableRef:

```sal
query g
target
with schema
```

## Nodes

```sal
print = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_CallFunction",
  FunctionReference: "<FMemberReference native text>",
  EnabledState: Disabled,
  NodeComment: "Temporarily disabled"
}
# Print String
```

The `node` tag is optional and erasable. `id`, `type`, native fields, and Graph
context supply semantics.

Titles from `GetNodeTitle()` are comments, not `name` or translated type.
Authored `UEdGraphNode` and subclass fields retain exact native names.

### Layout Detail

`with layout` enriches only the Node and Pin objects the selected operation
already returns. It does not widen the projection or add a synthetic layout,
status, snapshot, or region object.

Every returned Node receives its stored layout:

```sal
at: [640, 0]
size: [240, 120]
```

`at` is the exact stored integer Node position. `size` is optional and appears
only when UE stores non-zero Node dimensions. Their meaning never changes when
the Graph Editor opens or closes: they are useful for conservative rough
placement, but are not rendered collision bounds.

When a usable live Graph Editor surface presents the exact Target Graph, the
same objects also receive authoritative graph-space visual facts:

- each Node has `visualBounds: [left, top, right, bottom]`;
- a measured Pin has `visualState: measured`, `visualBounds`,
  `visualCenter`, `placementAnchor`, and `placementAnchorKind`;
- a Pin intentionally absent under current presentation rules has
  `visualState: intentionally_not_presented` and non-empty
  `geometryReasons`, with no visual coordinates.

`placementAnchorKind` is `pin_image_center` or
`pin_row_edge_midpoint`. The closed ordered `geometryReasons` values are
`hidden_native`, `hidden_advanced`, `hidden_unconnected`, and
`hidden_unconnected_no_default`.

If the exact Graph surface is closed, ambiguous, interacting, not yet
synchronized, or any applicable object cannot be measured safely, the entire
response removes all `visualBounds`, `visualState`, `visualCenter`,
`placementAnchor`, `placementAnchorKind`, and `geometryReasons` fields and
emits the warning:

```text
capability.layout_geometry_unavailable
```

The warning's `actual.reason` identifies the availability failure and its
suggestion tells the agent to open or focus the exact Graph, finish the
interaction, wait for synchronization, or retry. A Blueprint asset being open
without that exact Graph surface is not sufficient.

The closed `actual.reason` values are `slate_unavailable`, `graph_not_open`,
`surface_ambiguous`, `interaction_in_progress`, `visual_sync_pending`,
`layout_scale_unavailable`, `node_widget_unavailable`,
`pin_widget_unavailable`, `second_pass_layout_unavailable`,
`unsupported_widget_geometry`, `prepass_incomplete`, `non_finite_geometry`,
and `non_positive_bounds`.

An agent may perform precise layout only when both conditions hold:

1. the response has no `capability.layout_geometry_unavailable` warning; and
2. every applicable returned Node and Pin has one complete visual field shape.

Otherwise, only stored `at` and optional `size` remain available, and they
authorize rough placement only. A response never mixes authoritative visual
geometry with visual fallback.

Pins, layout-native duplicates, compiler/upgrade messages, transient caches,
and deprecated data are not flattened as arbitrary Node fields.

## Pins

```sal
delay.Duration = pin {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "<FEdGraphPinType native text>",
  direction: in,
  DefaultValue: "1.0",
  AutogeneratedDefaultValue: "0.0"
}
```

`pin` is an erasable tag. Member binding normally uses native `PinName`.
Unrepresentable or colliding names use a unique local alias plus exact native
`PinName` data; aliases are not cross-request identity.

Default state preserves:

- `DefaultValue`
- `AutogeneratedDefaultValue`
- `DefaultObject`
- `DefaultTextValue`

Persisted fields such as hidden/connectable/default-read-only/advanced/
deprecated/orphaned state, `PinFriendlyName`, `PersistentGuid`, and
`ReferencePassThroughConnection` retain native names. `PersistentGuid` is
reconstruction data, not StableRef identity.

Split Pins remain objects:

```sal
makeVector.Vector = pin {
  id: "cccccccc-cccc-cccc-cccc-cccccccccccc",
  type: "<FVector PinType native text>",
  direction: in,
  bHidden: true
}
makeVector.X = pin {
  id: "dddddddd-dddd-dddd-dddd-dddddddddddd",
  type: "<float PinType native text>",
  direction: in,
  ParentPin: @eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee/cccccccc-cccc-cccc-cccc-cccccccccccc
}
```

Parent precedes children; child order follows UE `SubPins`. `OwningNode` comes
from the binding path and `LinkedTo` becomes Edge text.

Future Pins in exact Palette results have no `id` because they do not yet
exist. Every existing Pin must include its native id.

## Edges

Edges connect Pins:

```sal
begin.then -> delay.execute
delay.then -> print.execute
```

UE has no Edge UObject. One relationship exists symmetrically in endpoint
`LinkedTo` arrays, so SAL emits it once and gives it no id or type.

Cross-request operations use full StableRefs:

```sal
@begin-node-guid/then-pin-guid ->
  @print-node-guid/execute-pin-guid
```

Direction is Output to Input. Native link order is preserved. Asymmetric,
cross-Graph, direction-invalid, or schema-invalid native state is diagnosed,
not repaired during Query.

## Health Comments

Outside Summary, every returned existing Node or Pin carries adjacent current
UE health:

- Node compiler `ErrorMsg` and `ErrorType`;
- `NodeUpgradeMessage`;
- visual warning and tooltip;
- Pin `DeprecationMessage`.

These are comments, not fields, objects, operations, or execution diagnostics.
Query does not compile, reconstruct, refresh, or dirty the owner.

When a Blueprint owner is `BS_Dirty` or `BS_Unknown`, stored compiler
annotations are marked potentially stale. Fresh complete compiler diagnostics
require a Blueprint compile handoff.

Summary uses one compact complete health index keyed by canonical Node
StableRefs instead of expanding full messages.

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

Operation capabilities:

| Operation | Clauses |
| --- | --- |
| `target` | optional `with schema` |
| `summary` | none |
| `nodes` | supported `where`, `order by`, `page`, optional `with layout` |
| exact Node or Pin StableRef | optional `with schema`, `with layout` |
| exact owning-Blueprint declaration StableRef | optional `with schema` |
| `context`, flows | optional depth and layout |
| `references` | page only |
| `palette entries` | supported filters/order/page |
| exact Palette | optional schema |

Unsupported combinations are errors.

### Summary

Summary returns semantic entry Nodes, one representative for every
disconnected semantic region, compact counts, and the complete health index.
Pure editor-presentation objects do not become topology regions.

### Node Search

`nodes` returns compact Node identities without Pins or Edges:

```sal
query g
nodes "Print"
where type = "/Script/BlueprintGraph.K2Node_CallFunction" and
  not id = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
order by type asc, id asc
page limit 50
```

Filters:

| Field | Operators |
| --- | --- |
| `type`, `id` | `=`, `!=` |
| `NodeComment` | `=`, `!=`, `~=` |

Without explicit sort, enumeration preserves Graph Node order and search uses
adapter relevance.

### Exact Reads

```sal
query g
@node-guid
with schema
```

Exact Node returns all current Pins but no adjacent Nodes or Edges. Exact Pin
returns compact owner Node plus that complete Pin.

Exact schema is live and instance-sensitive. It reports fields, constraints,
operations, arguments, output roles, and availability from native Class,
editor actions, and Graph Schema.

### Context

```sal
query g
context @node-guid depth 3
```

Context traverses all Edge kinds in both directions. Depth counts crossed
Edges, defaults to one, visits cycles once, and keeps only Pins required for
target, endpoints, and boundaries. A Pin target seeds only that Pin.

The adapter returns the complete requested depth or fails with a size
diagnostic; it never silently truncates.

### Execution Flow

```sal
exec flow from @node-guid depth 5
exec flow to @node-guid/pin-guid depth 5
```

Forward follows Exec outputs; reverse follows Exec inputs. Graph Schema
identifies Exec Pins. Traversal stays inside the Target Graph and preserves
branch/link order.

Calls to another Graph produce a related Graph Target and navigation handoff,
not an implicit traversal across Domain Target scope.

### Data Flow

```sal
data flow to @node-guid depth 3
data flow from @node-guid/pin-guid depth 3
```

`to` traces upstream non-Exec producers and includes unconnected defaults as
leaves. `from` is conservative downstream Node-level impact because UE does
not universally map an input to exact affected outputs. Comments disclose that
boundary.

### References

```sal
query g
references to @variable-get-node-guid
```

Providers resolve native member facts such as `VariableReference`,
`FunctionReference`, `MacroGraphReference`, and `DelegateReference`. A Node
with several facts requires exact member qualification.

Local scope is exactly this Graph. Project scope uses the bounded shared
provider. Graph never silently ascends to Blueprint.

## Palette

```sal
query g
palette entries "Print String"
page limit 50

query g
palette @P_PrintString
with schema
```

Results use ordinary ObjectExpr:

```sal
PrintString = node { palette: "P_PrintString" }
```

The tag is erasable. The opaque id identifies one exact UE Action Menu
capability in the current Graph context. It is not a future Node id or Domain
selector.

Identity includes every native semantic fact needed to distinguish active
actions. Variable actions, for example, retain owning BlueprintGuid and
VarGuid or an exact native field path. Localized menu text, friendly name,
transient Class Path, and list position are never identity.

Search follows UE Action Menu semantics and native ranking. Exact/prefix title
matches form the primary tier before native weight and source order.

Pin context:

```sal
palette entries "Branch" from @source-node-guid/source-pin-guid
```

`from` requires an Output Pin and `to` requires an Input Pin. Both map to the
Pin set used to construct UE's action context and are mutually exclusive.
The resolved Pin and Graph context participate in the opaque Palette identity,
so exact `palette @id` and later creation do not repeat them.

Structured filters are:

| Filter | Accepted value | UE context |
| --- | --- | --- |
| `widget` | exact current Widget name or Graph-resolvable declaration ref | generated Widget `FObjectProperty` in selected objects |
| `component` | exact SCS Component name or Graph-resolvable declaration ref | Blueprint Component property in selected objects |
| `actor` | exact Actor UObject name, only in an authored Level Graph scope | exact `AActor` in selected objects |
| `contextSensitive` | Boolean, default `true` | UE Action Menu context-sensitivity flag |

`widget`, `component`, and `actor` accept only `=` and are mutually exclusive.
They may combine with `contextSensitive` through `and`;
`contextSensitive` supports `=` and `!=`. No other Palette filter fields or
ordered comparisons are accepted. Names mean exact native UObject or variable
names, never display labels, Classes, or localized text. Zero matches and
ambiguity fail instead of falling back to another object kind.

Graph object-context refs are owning-Blueprint declarations that Graph Domain
can resolve; their resolution does not grant mutation authority in the
Blueprint or Widget Domain. Actor name is valid only when the Level Graph
Target establishes its authored World scope; SAL does not invent a general
Actor StableRef.

The exact Palette id incorporates resolved Pin/object context and is
revalidated at exact read and creation. Rename, deletion, type change, Graph
context change, or action-database change makes it stale rather than triggering
a fresh fuzzy search.

Explicit ordering supports `name`, `category`, and Palette id. Without it, UE
Action Menu ranking is preserved: exact or prefix localized/source-title match
is the primary tier, then higher native filtered weight, then native source
order. Search token matching uses UE's full action search text and sanitized
display form. This keeps English source-title discovery independent of Editor
localization.

Pagination is cursor-based and defaults to 50. Search text is optional. With
no text, Pin context, or structured filter, `palette entries` enumerates the
current context-sensitive menu through the same pagination contract. Exact
Palette read returns every future Pin shape determinable from native template
and spawner data; future Pins have no native id.

Exact Palette returns every future Pin determinable from native template and
spawner data. Creation re-resolves and revalidates the same capability.

### Bound Events

Widget, SCS Component, and Level Actor multicast events remain Graph Nodes.
Palette uses an exact object context and the explicit Graph Target:

```sal
query g
palette entries "OnClicked"
where widget = @button-widget-guid
```

One bound-event Node is allowed for the same native object/delegate across the
owning Blueprint. If it exists elsewhere, Palette returns the related Graph
Target and scoped Node ref. It does not return another creation binding,
silently move the existing Node, or no-op.

## Patch

Graph Patch is ordered. Bindings declare future objects; materializing
operations perform mutation:

```sal
patch g dry run

delay = { palette: "P_Delay" }
insert @source-node-guid/source-pin-guid ->
  delay.execute / delay.then ->
  @target-node-guid/target-pin-guid
set delay.Duration.DefaultValue = "1.0"
move delay to (640, 0)
```

Operation surface:

```sal
add binding
add binding @source-node-id/source-pin-id -> binding.input
add binding binding.output -> @target-node-id/target-pin-id

connect @source-node-id/source-pin-id -> @target-node-id/target-pin-id
disconnect @source-node-id/source-pin-id -> @target-node-id/target-pin-id
break @node-id/pin-id
insert @source-node-id/source-pin-id ->
  binding.input / binding.output ->
  @target-node-id/target-pin-id

set @node-id.NativeField = value
set @node-id/pin-id.NativeField = value
reset @node-id.NativeField
reset @node-id/pin-id.NativeField
move @node-id to (x, y)
remove @node-id

invoke g Operation(namedArguments) [as alias]
invoke @node-id Operation(namedArguments) [as alias]
invoke @node-id/pin-id Operation(namedArguments) [as alias]
```

Graph Node movement accepts only the absolute `to (x, y)` placement. For
relative intent, first Query the Node with `with layout`, read its stored `at`,
compute the absolute destination, and emit `to`; `by` is not a Graph
capability.

Each coordinate must be a signed 32-bit mathematical integer that round-trips
exactly through UE's `FVector2f` Schema path. Every integer from `-16777216`
through `16777216` is safe; outside that interval, only exactly representable
values are valid.

Every valid move, including a no-op, has one ordered entry in
`planned.operations`:

```json
{
  "index": 0,
  "operation": "move",
  "ref": "@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  "to": [640, 0],
  "before": { "at": [320, 0] },
  "after": { "at": [640, 0] },
  "changed": true
}
```

When every Patch statement is a move, `diff` is complete and uses
`changedOperations`, `scope: "graph"`, and ordered `changes`. Each change
contains `index`, `kind: "move"`, a shared structured stable Node `target`,
`before: {at}`, and `after: {at}`. Its target shape is
`{"kind":"stable_ref","identityPath":["<NodeGuid>"],"semanticTag":"node"}`.
A no-op stays in the plan, skips mutation, and is omitted from `diff.changes`;
an all-no-op move Patch therefore returns an empty rich diff and
`applied: false`. Mixed Graph Patches do not return a partial rich move
`changes` array. Patch results promise stored coordinates only; Query again
with `with layout` for refreshed visual geometry.

Raw Pin creation is invalid. Graph Schema owns connection compatibility,
break-others behavior, conversion creation, and type promotion. Every native
effect is planned.

`insert` requires one existing Edge and atomically creates a Node, removes the
old Edge, and creates two exact new Edges. It rejects unrelated conversion or
link breaking.

Node removal follows ordinary UE deletion and does not reconnect neighbors.
Dynamic Pin removal is an exact schema operation, not Graph `remove`.

Fields share read/write/reset checks with schema. Identity, Target ownership,
and structural direction fields are not writable.

## Exact Operations

### Dynamic Pins

Exact Node/Pin schema exposes target-local native editor actions, for example:

```sal
invoke @sequence-node-guid AddExecutionPin() as next
connect next -> @target-node-guid/target-pin-guid

invoke @map-node-guid AddKeyValuePair()
  as key: newKey, value: newValue

invoke @vector-node-guid/vector-pin-guid SplitStructPin()
  as subpins.X: x, subpins.Z: z
```

Operation names are not global interfaces. The resolved object schema defines
arguments, output roles, and current availability.

Creation normally belongs to the Node whose editor action owns the selection
context. Insertion or removal of one selected Pin belongs to that exact Pin.
Select remains a Node exception because UE removes its last option rather than
an arbitrarily selected option. The initial K2 matrix is:

| Exact target | Operation | Primary outputs |
| --- | --- | --- |
| Sequence or MultiGate Node | `AddExecutionPin()` | `pin` |
| Sequence Pin | `InsertExecutionPinBefore()` | `pin` |
| Sequence Pin | `InsertExecutionPinAfter()` | `pin` |
| removable Sequence or Switch Pin | `RemoveExecutionPin()` | none |
| Switch Node | `AddExecutionPin()` | `pin` |
| Select Node | `AddOptionPin()` | `pin` |
| Select Node | `RemoveOptionPin()` | none |
| Make Array Node | `AddArrayElementPin()` | `pin` |
| Array element Pin | `RemoveArrayElementPin()` | none |
| Make Set Node | `AddSetElementPin()` | `pin` |
| Set element Pin | `RemoveSetElementPin()` | none |
| Make Map Node | `AddKeyValuePair()` | `key`, `value` |
| Map key or value Pin | `RemoveKeyValuePair()` | none |
| commutative or promotable operator Node | `AddInputPin()` | `pin` |
| removable operator input Pin | `RemoveInputPin()` | none |
| DoOnce MultiInput Node | `AddInputPin()` | `input`, `output` |
| removable DoOnce MultiInput input Pin | `RemoveInputPin()` | none |

DoOnce MultiInput illustrates target-local meaning: its `AddInputPin()` creates
the corresponding execution input and output together. Only its removable
input Pin exposes `RemoveInputPin()`; an output Pin does not. Native removal
may update the additional-input count, rename surviving Pins, disconnect
Edges, and structurally compile the Blueprint. Those are effects, not a
synthetic promise that one symmetric pair is always deleted.

User-defined Pins on Function Entry, Function Result, Custom Event, Dispatcher
Signature, and Tunnel Nodes are excluded from this matrix. They alter a
signature or boundary and use the contracts below.

Struct split creates direct children only. Recombine targets one child and
returns its surviving parent:

```sal
invoke @vector-node-guid/x-pin-guid RecombineStructPin()
  as vector
```

Native schema rules prohibit hidden implicit disconnect/migration.

### Function And Event Signatures

An editable Function Graph uses Graph-level operations:

```sal
invoke g AddInputParameter(
  name: Damage,
  type: "<FEdGraphPinType native text>"
) as damage

invoke g AddOutputParameter(
  name: WasApplied,
  type: "<FEdGraphPinType native text>"
) as pin: wasApplied, result: returnNode
```

Parameters remain Pins on Function Entry/Result. A Custom Event uses a
Node-level `AddParameter`. Existing parameter edit uses full Pin StableRef:

```sal
set @entry-node-guid/damage-pin-guid.PinName = BaseDamage
set @entry-node-guid/damage-pin-guid.type =
  "<FEdGraphPinType native text>"
invoke @entry-node-guid/damage-pin-guid RemoveParameter()
```

Both creation arguments are required. `name` is an exact requested `FName`;
collision is a diagnostic rather than permission to accept a UE-generated
suffix. `type` is complete native `FEdGraphPinType` text. Semantic Function
input becomes an Output Pin on Function Entry; semantic Function output becomes
an Input Pin on every Function Result.

`AddInputParameter` returns the final Entry Pin.
`AddOutputParameter` returns the final parameter Pin on UE's primary Result
Node and that Node as selector `result`. If no Result Node exists, the native
path may create and position one and connect execution where permitted.
Corresponding Pins on other Result Nodes and any automatic Node or Edge changes
are effects.

Custom Event parameters have signature-input meaning and become physical
Output Pins. `AddParameter` is unavailable for inherited or otherwise
non-editable Events and for native Pin types the Event rejects.

Existing signature Pins support exact ordering operations:

```sal
invoke @entry-node-guid/damage-pin-guid MoveParameterBefore(
  anchor: @entry-node-guid/instigator-pin-guid
)
invoke @entry-node-guid/damage-pin-guid MoveParameterAfter(
  anchor: @entry-node-guid/context-pin-guid
)
```

The anchor must be on the same semantic side of the same Function or Event.
Ordering changes native `UserDefinedPins`; numeric indices are never public
identity. For output parameters represented on several Result Nodes, targeting
any corresponding Result Pin applies rename, type, default, remove, or ordering
by native parameter identity across every Result. Removed Pins and their local
aliases become invalid after the statement.

Native propagation may reconstruct call sites, mirror Result Pins, change ids,
and affect loaded dependent Blueprints. Output aliases resolve only after
reconstruction and every effect is planned.

Dispatcher Signature Graphs support input parameters and native
`CopySignatureFrom`; output parameter creation is absent:

```sal
invoke g CopySignatureFrom(
  function: "/Script/MyGame.DamageSource:OnDamage"
)
```

The exact UFunction must be in the Dispatcher property's allowed Class scope,
delegate-compatible, and contain no output parameter. The operation removes
all old user-defined Pins and recreates inputs in native Function declaration
order. It has no primary outputs; final readback contains the complete new
signature, and every old parameter ref or alias is invalid.

Preflight proves the authored multicast-delegate Variable and its exactly one
same-owned, same-named Signature Graph, exactly one editable Function Entry,
and no Function Result or output parameter. Generated Reflection is not an
alternate source, and inconsistency is diagnosed instead of repaired by name.
Propagation reconstructs matching loaded bind, unbind, call, clear, assign,
linked Custom Event, and CreateDelegate Nodes. Those cascades are effects.

Macro and collapsed Graph boundaries use the same semantic input/output
operations on authoritative Tunnel Pins, but their topology is distinct:

| Graph | Authoritative Pins | Derived Pins |
| --- | --- | --- |
| Macro Graph | Entry and Exit Tunnel Pins | loaded Macro Instance Pins |
| Collapsed Graph | inner Entry and Exit Tunnel Pins | outer Composite Node Pins |

Semantic input is an Entry Tunnel Output; semantic output is an Exit Tunnel
Input. `AddInputParameter` and `AddOutputParameter` return only that final
authoritative Tunnel Pin. Derived Macro Instance or Composite Pins are effects,
not output aliases in the current Graph Target.

Name uniqueness spans both Tunnel sides. Existing authoritative Pins support
the signature-aware `set`, `reset`, `RemoveParameter`,
`MoveParameterBefore`, and `MoveParameterAfter` contract above, with an anchor
on the same semantic side. Derived outer or instance Pins are read-only for
signature structure.

Preflight requires exactly one editable Entry and Exit Tunnel with legal
directions. Both Collapsed-Graph Tunnels must reference the same outer
Composite. A Macro Instance is never authoritative. Broken or ambiguous
topology fails instead of choosing by position or title. Apply uses the
complete K2 parameter-change path so loaded Macro Instances or the outer
Composite reconstruct; local raw-Pin edits are insufficient. Loaded cross-asset
Macro effects participate in the atomic plan, while unloaded assets retain
UE's native later-load behavior.

### Timeline

Timeline is one compound Node plus backing Template. Exact schema exposes
native fields and operations:

```sal
invoke @timeline-node-guid AddFloatTrack(
  TrackName: Alpha
) as alpha

invoke @timeline-node-guid RenameTrack(
  TrackName: Alpha,
  NewName: Opacity
)

invoke @timeline-node-guid AddKey(
  TrackName: Opacity,
  Time: 1.0,
  Value: 1.0
)

invoke @timeline-node-guid UseExternalCurve(
  TrackName: Opacity,
  Curve: "/Game/Curves/C_Opacity.C_Opacity"
)
```

The complete operation families are:

| Operation | Contract |
| --- | --- |
| `AddFloatTrack`, `AddVectorTrack`, `AddEventTrack`, `AddLinearColorTrack` | append one internal Track and return its final Pin |
| `AddTrackFromCurve` | append an external Track from an existing compatible Curve Asset |
| `RenameTrack` | rename Track, derived names, and generated Pin while preserving valid links |
| `MoveTrack` | require exactly one of `Before` or `After`; change native display order |
| `RemoveTrack` | remove Track, display record, generated Pin, and native incident links |
| `AddKey`, `SetKey`, `RemoveKey` | edit one exact internal Curve Key |
| `UseExternalCurve`, `UseInternalCurve` | switch Curve ownership through UE behavior |
| `Duplicate` | duplicate the compound Node/Template and return the final Node |

Track names are unique across all Track arrays and fixed Timeline Pins; UE
numeric suffix generation is not accepted as success. New Tracks append to
native `TrackDisplayOrder`. `EventTracks`, `FloatTracks`, `VectorTracks`,
`LinearColorTracks`, and `TrackDisplayOrder` are coordinated read-only fields,
not directly settable arrays.

`AddTrackFromCurve(Curve: path)` accepts `UCurveFloat`,
`UCurveVector`, or `UCurveLinearColor`. A Float Curve marked as an event Curve
creates an Event Track; otherwise its native Curve name becomes the initial
Track name. A following `RenameTrack` supplies a different exact name.

Key identity is Track name, native channel when required, and time;
`FKeyHandle` is transient. Vector uses channel `X|Y|Z`, Linear Color uses
`R|G|B|A`, and Float/Event reject `Channel`. Non-event Tracks require `Value`;
Event may omit it and retain UE's native default. `AddKey` accepts native rich
curve interpolation, tangent mode, tangent-weight mode, and tangent values.
`SetKey` changes only supplied fields and may use `NewTime`. Occupied or
duplicate times are conflicts, never implicit overwrite or merge.

Key editing is available only for internal Curves. `UseExternalCurve` replaces
the Curve reference without copying internal Keys into that external Asset.
`UseInternalCurve` is available only on an external Track and copies current
native Keys into a newly owned internal Curve. External Curve Assets are
references and are never mutated through Timeline.

`Duplicate()` creates a new NodeGuid, unique Timeline name, backing Template,
internal Timeline Guid, and owned internal Curves while preserving external
Curve references; it does not duplicate connections. Ordinary Node removal
must use the Timeline Node destruction path so the backing Template is removed
with the Node. Every Timeline action first validates exactly one matching
Node/Template pair plus Track/display-order and generated-Pin consistency.

## Preflight And Transactions

Alias states are unbound, bound/unmaterialized, and materialized. A creation
binding must precede use and be consumed exactly once by add or insert.
Operation outputs become usable only after their statement.

Preflight:

1. parses ordered statements;
2. resolves Target, StableRefs, and Palette ids;
3. builds provisional Nodes/Pins and operation outputs in statement order;
4. resolves later aliases against provisional state;
5. validates native field/operation/schema behavior and complete effects;
6. verifies live state still matches;
7. applies atomically or returns dry-run plan.

The Bridge duplicates the complete owning Blueprint, isolates generated
Classes, Timeline Templates, and internal Curves, and executes the same native
edit functions. Transient ids never escape.

Live apply requires one top-level transaction. Failure closes and explicitly
undoes it; cancel is not rollback. An all-no-op Patch does not dirty the asset.

If exact native effects or generated Pins cannot be planned, the operation
fails with a capability diagnostic.

## Finalization Handoff

Graph Domain does not compile or save its owning Blueprint:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

A following Blueprint request performs compile/save. Graph Domain never
switches Domain because an operation reaches Blueprint-owned state.
