# Blueprint Domain

## Scope

The blueprint domain describes Blueprint asset structure outside individual
graph nodes and edges. It covers the Blueprint Editor's Class Settings,
variables, dispatchers, graphs, SimpleConstructionScript components, and
Blueprint-owned backing state required by special Graph Nodes such as Timeline
Nodes. Effective `UClass` Reflection and Class Default Object state belong to
the class domain and are not duplicated here.

Function, macro, event, delegate-signature, interface-function, and construction
script graphs are all graph objects. Their nodes, pins, and edges remain graph
domain work.

Some Graph Nodes reference Blueprint-owned Variables, Dispatchers, or
Components. Timeline differs: `UBlueprint::Timelines` stores the backing
`UTimelineTemplate`, but the public LGL identity remains its owning Timeline
Node. The Graph domain owns that Node identity; the Blueprint adapter resolves
and flattens the backing pair.

The TypeScript experiment still implements the earlier `members` array and
member-oriented query model. That implementation does not match this design
and must be replaced in a later schema/parser/formatter/adapter phase.

## UE Boundary

`UBlueprint` owns more than graphs. Important UE-owned storage includes:

- `BlueprintGuid`
- `BlueprintType`
- `ParentClass`
- Class Settings stored on `UBlueprint`
- `ImplementedInterfaces`
- `NewVariables`
- `FunctionGraphs`
- `MacroGraphs`
- `DelegateSignatureGraphs`
- `UbergraphPages`
- `SimpleConstructionScript`
- `Timelines`

The read model exposes five concrete Blueprint objects: Blueprint, Variable,
Dispatcher, Graph, and Component. It does not introduce a common Blueprint
`Member` object. Timeline Templates are backing state of Timeline Nodes rather
than a sixth public object. Generated classes, generated variables,
intermediate graphs, and compiled event graphs are derived state rather than
additional authored objects.

## Basic Form

Blueprint object text is a statement list:

```lgl
bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
door = blueprint(
  asset: bpAsset,
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  ParentClass: "/Script/Engine.Actor",
  BlueprintNamespace: "Game.Doors",
  BlueprintCategory: "Doors",
  ImplementedInterfaces: [
    {Interface: "/Script/MyGame.Damageable", Graphs: [graph@damageable-graph-guid]}
  ]
)

eventGraph = graph(domain: blueprint, asset: bpAsset, id: "event-graph-guid", name: EventGraph, type: GT_Ubergraph)
# UBlueprint::UbergraphPages
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: GT_Function)
# UBlueprint::DelegateSignatureGraphs
damageableGraph = graph(domain: blueprint, asset: bpAsset, id: "damageable-graph-guid", name: TakeDamage, type: GT_Function)
# FBPInterfaceDescription::Graphs

door.Health = variable(id: "variable-guid", type: "<FEdGraphPinType native text>", Category: "Stats")
door.OnOpened = dispatcher(id: "dispatcher-variable-guid", type: "<FEdGraphPinType native text>")

door.Root = component(id: "root-component-guid", type: "/Script/Engine.SceneComponent")
Root.Mesh = component(id: "mesh-component-guid", type: "/Script/Engine.StaticMeshComponent", StaticMesh: "/Game/Meshes/SM_Door.SM_Door")
```

`blueprint.name = ...` is ordinary binding-path syntax. It expresses owner and
current name without creating a shared Member type. Component trees use the
same `parent.child = ...` shape to express hierarchy.

Angle-bracketed strings in examples are documentation placeholders for native
UE text. They are not returned literally and do not introduce LGL syntax.

## Blueprint Object

| Object | Syntax | Example |
| --- | --- | --- |
| Blueprint asset | `name = asset(path: "...", type: nativeClassPath)` | `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")` |
| Blueprint binding | `name = blueprint(asset: ref, id: string, type: nativeEnum, nativeFields...)` | `door = blueprint(asset: bpAsset, id: "blueprint-guid", type: BPTYPE_Normal, ParentClass: "/Script/Engine.Actor")` |
| Implemented interfaces | native `ImplementedInterfaces` field | `ImplementedInterfaces: [{Interface: "/Script/MyGame.Damageable", Graphs: [graph@graph-guid]}]` |

`blueprint(...)` identifies one Blueprint asset as a class-like editing target.
It is not a graph and it is not a replacement for `asset(...)`.

`id` maps to the persisted `UBlueprintCore::BlueprintGuid`. UE creates a new
Guid for a new or duplicated Blueprint, while asset rename and move change only
the Asset Path. `type` maps directly to the native `EBlueprintType` text:
`BPTYPE_Normal`, `BPTYPE_Const`, `BPTYPE_MacroLibrary`, `BPTYPE_Interface`,
`BPTYPE_LevelScript`, or `BPTYPE_FunctionLibrary`.

The Asset Path remains the current load location; it is not substituted for
Blueprint identity. The normalized Blueprint object replacement is
intentionally not specified until the shared schema phase.

## Class Settings

Class Settings is the Blueprint Editor surface backed by the current
`UBlueprint` asset. It is distinct from the class domain: Class Settings are
authored source state, while the generated `UClass`, effective Reflection, and
Class Defaults are compiled or derived state.

The read model follows the editor's Parent Class, Blueprint Options, Class
Options, Interfaces, and Imports sections without introducing a Class Settings
object:

```lgl
door = blueprint(
  asset: bpAsset,
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  ParentClass: "/Script/Engine.Actor",
  bRunConstructionScriptOnDrag: true,
  bGenerateAbstractClass: false,
  ShouldCookPropertyGuidsValue: Inherit,
  CompileMode: Default,
  BlueprintNamespace: "Game.Doors",
  BlueprintCategory: "Doors",
  HideCategories: ["Rendering"],
  ImportedNamespaces: ["Game.Combat", "Game.UI"]
)
```

Canonical text should keep constructor arguments named. Formatters may keep the
binding on one line when it is short. Fields equal to defined UE defaults may
be omitted; `with schema` reports every usable field, its native type, default,
access, and instance-level availability.

### Parent Class

`ParentClass` is the authored parent Class Path. It remains an ordinary
Blueprint field even though changing it requires UE's full reparent workflow.
Its mutation contract is defined below rather than translated into a parallel
lifecycle Operation.

### Blueprint Options

Blueprint Options retain their exact `UBlueprint` field names:

| Field | UE default | Meaning |
| --- | --- | --- |
| `bRunConstructionScriptOnDrag` | `true` | Continuously rerun an Actor Construction Script while dragging |
| `bRunConstructionScriptInSequencer` | `false` | Continuously rerun an Actor Construction Script from Sequencer |
| `BlueprintDisplayName` | `""` | Override the Blueprint display name in editor UI |
| `BlueprintDescription` | `""` | Supply the Content Browser description and generated Class tooltip |
| `BlueprintNamespace` | `""` | Assign the Blueprint's Namespace |
| `BlueprintCategory` | `""` | Categorize the generated Blueprint Class in palette surfaces |
| `HideCategories` | `[]` | Add generated Class categories to the inherited hidden set |

Class Settings for a Level Blueprint does not expose
`bRunConstructionScriptOnDrag`; exact schema therefore reports that field as
unavailable for writes on a Level Blueprint. `BlueprintNamespace` remains a
field even though its adapter path also updates the Namespace Registry and any
open Blueprint Editor context.

### Class Options

Class Options likewise preserve their exact native names and enum values:

| Field | UE default | Meaning |
| --- | --- | --- |
| `bGenerateConstClass` | `false` | Add `CLASS_Const` to the generated Class during compilation |
| `bGenerateAbstractClass` | `false` | Add `CLASS_Abstract` to the generated Class during compilation |
| `bDeprecate` | `false` | Deprecate the Blueprint-generated Class |
| `ShouldCookPropertyGuidsValue` | `Inherit` | Resolve whether generated Property Guids are retained while cooking |
| `CompileMode` | `Default` | Select development or final-release handling for explicitly disabled Nodes |

`CompileMode` is always readable. Exact schema makes it writable only when UE's
explicit impure Node disabling feature is enabled, matching the Class Settings
UI. The compiler may synchronize inherited `CLASS_Abstract` and
`CLASS_Deprecated` state back to the corresponding Blueprint fields; LGL
returns that native state rather than inventing separate local and effective
Class Option fields.

### Interfaces

Implemented Interfaces preserve the native `FBPInterfaceDescription` shape:

```lgl
ImplementedInterfaces: [
  {
    Interface: "/Script/MyGame.Damageable",
    Graphs: [graph@take-damage-graph-guid]
  },
  {
    Interface: "/Script/MyGame.Interactable",
    Graphs: [graph@interact-graph-guid, graph@can-interact-graph-guid]
  }
]
```

`Interface` is the Interface Class Path. `Graphs` contains stable `graph@id`
references and preserves UE order; the Graph objects remain separate. This
replaces the lossy `blueprint.implements` translation. Only interfaces directly
declared by this Blueprint appear here; inherited effective interfaces belong
to Class Reflection. `ImplementedInterfaces` is readable native relationship
state, not a directly writable array; its compound mutation operations are
defined below.

### Imports

`ImportedNamespaces` is the persisted `UBlueprint` `TSet<FString>` containing
explicit imports. Canonical LGL sorts its strings for deterministic text; that
order has no UE meaning. The empty global Namespace is implicit and never
appears in this set.

The editor's Default Namespaces list is derived rather than stored. It includes
shared editor and project imports, the current Blueprint Namespace, parent
Class Namespaces, and, when enabled by UE, explicit imports inherited from
parent Blueprints. Exact Blueprint text returns that useful effective state as
an adjacent comment instead of fabricating a `DefaultNamespaces` field:

```lgl
###
Default Namespaces
Game.Doors
Game.Framework
###
```

When UE's Namespace Editor Features are disabled, `ImportedNamespaces` remains
readable but exact schema marks it unavailable for writes, matching the absent
Imports section in Class Settings.

`GeneratedClass` is derived navigation information and is returned as an
immediately following comment with its exact Class Path. Compile status and
errors are diagnostic comments. `SkeletonGeneratedClass`, transient compile
state, internal versioning, editor-session state, generated collections, and
caches are not Class Settings fields.

`CategorySorting` is persisted My Blueprint panel presentation state. It is
maintained internally while categories are discovered, removed, or reordered;
it does not participate in Class Settings, generated Class semantics, or the
public Blueprint object and is not exposed through `with schema`.

`ComponentClassOverrides` and `InheritableComponentHandler` belong to
Component Template resolution. They are not flattened into Class Settings or
Class Defaults fields.

Variable declaration and CDO storage remain distinct.
`FBPVariableDescription::DefaultValue` is compiler staging text used during
structural changes such as creation, rename, or type migration. Full Blueprint
compilation imports it into the generated Class Default Object and normally
clears it. It is therefore not the durable source of an effective Variable
default and must not be returned as one. A non-empty staging value may be
preserved under its native field name when it actually exists, with schema
comments identifying its transient compiler role. Effective values are read
and edited through Class Defaults. The old `blueprint.default` statement is not
part of the confirmed language.

## Concrete Blueprint Objects

Blueprint reads return concrete objects directly in the ordered LGL document.
There is no `Member`, `BlueprintMember`, `members[]`, or generic member result.
The five concrete object types are Blueprint, Variable, Dispatcher, Graph, and
Component.

Every concrete object exposes one `id` mapped to its native UE GUID, including
the Blueprint's own `BlueprintGuid`. Binding paths supply owner and current
names where applicable. Names are readable and searchable but are not
cross-query identity.

### Variables

Variables map to UE `FBPVariableDescription` entries and generated class
properties. Only variables authored in `UBlueprint::NewVariables` are returned;
inherited, native, local, generated, and multicast-delegate declarations are
not duplicate Variable objects.

```lgl
door.Health = variable(
  id: "variable-guid",
  type: "<FEdGraphPinType native text>",
  FriendlyName: "Health",
  Category: "Stats",
  PropertyFlags: "<EPropertyFlags native text>",
  RepNotifyFunc: None,
  MetaDataArray: "<FBPVariableMetaDataEntry array native text>"
)
```

The binding name maps to `FBPVariableDescription::VarName`, `id` maps to
`VarGuid`, and `type` contains the canonical native text of `VarType`. The
remaining authored declaration fields retain their UE names and native values:
`FriendlyName`, `Category`, `PropertyFlags`, `RepNotifyFunc`, `MetaDataArray`,
and other durable `FBPVariableDescription` state. LGL does not translate those
fields into `default`, `private`, `exposeOnSpawn`, `replication`, or
`metadata`. `DefaultValue` is returned only when UE currently carries non-empty
compiler staging text; it is never presented as the effective default.

Fields equal to defined UE defaults may be omitted from a compact exact read.
`with schema` reports all fields available on the resolved Variable, including
their native type, access, and default behavior.

### Graphs

Blueprint functions and macros are Graph objects, not additional declaration
objects. Graph `type` is the exact native `EGraphType` reported by the Graph's
Schema; it does not encode the Graph's lifecycle role:

```lgl
openDoor = graph(domain: blueprint, asset: bpAsset, id: "function-graph-guid", name: OpenDoor, type: GT_Function)
# UBlueprint::FunctionGraphs
traceDoor = graph(domain: blueprint, asset: bpAsset, id: "macro-graph-guid", name: TraceDoor, type: GT_Macro)
# UBlueprint::MacroGraphs
```

`id` maps to `UEdGraph::GraphGuid`. Graph name is readable and searchable but is
not identity. `domain`, `asset`, `id`, and `name` are LGL common structure;
`type` preserves the native `EGraphType` text such as `GT_Function`,
`GT_Ubergraph`, or `GT_Macro`. Relevant authored `UEdGraph` fields retain their
UE names and native values, including `Schema`, `bEditable`, `bAllowDeletion`,
`bAllowRenaming`, and `InterfaceGuid`.

UE ownership and references determine the role: `FunctionGraphs`,
`MacroGraphs`, `UbergraphPages`, `DelegateSignatureGraphs`, and
`FBPInterfaceDescription::Graphs` are distinct native owners. Override state
comes from the Function Entry reference to the parent `UFunction`.
Construction Script and Collapsed Graph roles likewise come from native
ownership and flags. LGL does not add a second role enum. Consequently an
ordinary Function Graph, Dispatcher Signature Graph, and Interface Graph may
all correctly return `type: GT_Function`.

A Blueprint Graph read is a compact Graph identity and native description. It
does not include `Nodes`, `Pins`, `Edges`, or `SubGraphs`; Graph body inspection
belongs to the graph domain. Function signatures, macro tunnels, overrides,
custom events, and all other graph contents therefore remain Nodes and Pins.

For an editable signature-bearing Graph, the Graph is also the semantic target
for creating input or output parameters. A Function Graph stores that authored
signature in Function Entry and Function Result `UserDefinedPins`; a Macro
Graph or nested Collapsed Graph uses its Entry and Exit Tunnel Pins.
Those Pins remain the returned state and identity. LGL does not add a Function
Signature, Graph Boundary, or Parameter object merely to hide that storage.
Existing parameter edits continue to target their exact authoritative Pins;
the graph adapter owns the required signature propagation to call sites, Macro
Instances, or an outer Composite Node.

Top-level graphs belong to the Blueprint asset. Nested graphs may additionally
identify their owning graph or node when that relation exists in UE, without
changing their Blueprint-plus-GraphGuid identity.

Implemented interfaces remain native Class Settings data:

```lgl
ImplementedInterfaces: [
  {Interface: "/Script/MyGame.Damageable", Graphs: [graph@damageable-graph-guid]}
]
```

Graphs required by that interface are ordinary Graph objects owned by
`FBPInterfaceDescription::Graphs`; their native Graph type remains
`GT_Function`. LGL does not create an Interface object, an interface-specific
type, or an interface-specific ref constructor.

### Dispatchers

An Event Dispatcher is one semantic object backed by a multicast-delegate
`FBPVariableDescription` and a same-owned Delegate Signature Graph:

```lgl
door.OnOpened = dispatcher(
  id: "dispatcher-variable-guid",
  type: "<multicast-delegate FEdGraphPinType native text>",
  Category: "Events"
)
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: GT_Function)
# UBlueprint::DelegateSignatureGraphs
```

The Dispatcher binding, `id`, `type`, and remaining declaration fields use the
same `FBPVariableDescription` mapping as a Variable. Its `VarType` identifies a
multicast delegate. LGL does not add a `graph` field: UE associates the
Dispatcher with its same-named Graph in `DelegateSignatureGraphs`.

An exact Dispatcher read places that compact Signature Graph identity directly
after the Dispatcher as required navigation context, without returning the
Graph body. The Signature Graph is the editing target for existing signature
parameters, which remain Pins on its Function Entry. It supports input
parameters only; Dispatcher call arguments are not Function outputs. Create,
rename, and remove of the Dispatcher itself must update both the Variable and
Graph atomically and belong to Blueprint asset Patch design. A missing,
duplicated, or mismatched half is an inconsistent Blueprint diagnostic, not a
guessing opportunity.

### Timeline Nodes

Timeline does not introduce a `timeline(...)` object, `timeline@id`, or
Blueprint-owned Timeline query. UE implements one authored Timeline through a
`UK2Node_Timeline` in a Graph plus a same-named `UTimelineTemplate` in
`UBlueprint::Timelines`. LGL presents that backing pair as one complex Node:

```lgl
openTimeline = node(
  graph: eventGraph,
  id: "timeline-node-guid",
  type: "/Script/BlueprintGraph.K2Node_Timeline",
  TimelineName: OpenTimeline,
  TimelineLength: 1.0,
  LengthMode: TL_TimelineLength,
  bAutoPlay: false,
  bLoop: false,
  bReplicated: false,
  bIgnoreTimeDilation: false,
  TimelineTickGroup: TG_PrePhysics,
  FloatTracks: [
    {
      TrackName: Alpha,
      bIsExternalCurve: false,
      CurveFloat: {
        FloatCurve: {
          Keys: [
            {Time: 0.0, Value: 0.0, InterpMode: RCIM_Linear, TangentMode: RCTM_Auto, TangentWeightMode: RCTWM_WeightedNone},
            {Time: 1.0, Value: 1.0, InterpMode: RCIM_Linear, TangentMode: RCTM_Auto, TangentWeightMode: RCTWM_WeightedNone}
          ]
        }
      }
    }
  ]
)
```

`id` maps to `UEdGraphNode::NodeGuid`; `type` is the native Timeline Node Class
Path. `TimelineName` is the shared authored name resolved across the Node and
Template. Template fields are flattened onto the Node with their UE names:
`TimelineLength`, `LengthMode`, `bAutoPlay`, `bLoop`, `bReplicated`,
`bIgnoreTimeDilation`, `MetaDataArray`, and `TimelineTickGroup`. The Template is
authoritative; same-named transient caches on `UK2Node_Timeline` are synchronized
by the adapter rather than returned as a second field set.

Tracks remain nested native Node state rather than LGL objects. Exact reads may
include `EventTracks`, `FloatTracks`, `VectorTracks`, `LinearColorTracks`, and
the authored editor order `TrackDisplayOrder`. Track names are unique across
all four arrays and are the exact selectors used by Timeline Operations. LGL
does not add `track(...)`, `track@id`, a Track query, or a Track type mapping.

Internal Curve Keys retain the native `FRichCurveKey` fields and source order.
Vector channels use the UE names `X`, `Y`, and `Z`; Linear Color channels use
`R`, `G`, `B`, and `A`. `FKeyHandle` is completely transient in UE and is not an
LGL identity. A Key is selected by Track name, native channel when present, and
time. If invalid source data contains multiple Keys at the same selected time,
the adapter returns an ambiguity diagnostic rather than choosing one.

For an internal Curve, the exact Node read includes its authored Keys. For an
external Curve, it returns `bIsExternalCurve: true` and the native Asset
reference without recursively expanding or mutating that independent Asset.
Pure Timeline editor presentation state such as Track expansion and curve-view
synchronization is omitted.

The Node and Template `TimelineGuid` fields exist for UE copy/paste matching and
Template duplication. They are not stable LGL identity and are not writable.
Cached names derived from `TimelineName`, including `DirectionPropertyName`,
`UpdateFunctionName`, `FinishedFunctionName`, per-Track Function names, and
per-Track Property names, are derived state and are not returned as authored
fields.

A missing Template, orphan Template, duplicate name pairing, or otherwise
non-unique Node/Template relationship is an inconsistent Blueprint diagnostic.
The adapter does not expose the halves independently or repair them by guessing.

### Graph-Owned Nodes

Custom events, override events, component-bound events, function entry/result
nodes, macro tunnels, and Timeline nodes are Graph Nodes. They are not Blueprint
objects and are not repeated in the Blueprint object model.

Graph nodes may reference a Variable, Dispatcher, or Component by its typed
stable reference, such as `variable@id` or `component@id`. Getter/setter nodes,
dispatcher nodes, component-bound events, and Timeline Nodes remain
independently identified Nodes in their owning Graph.

## Component Tree

Blueprint components map to UE `USimpleConstructionScript` and `USCS_Node`.
They are not graph nodes.

Component tree object text uses binding paths plus statement order:

```lgl
door.Root = component(id: "root-component-guid", type: "/Script/Engine.SceneComponent")
Root.Mesh = component(id: "mesh-component-guid", type: "/Script/Engine.StaticMeshComponent", StaticMesh: "/Game/Meshes/SM_Door.SM_Door")
Root.Trigger = component(id: "trigger-component-guid", type: "/Script/Engine.BoxComponent", BoxExtent: "(X=100,Y=100,Z=200)")
```

Rules:

1. In materialized Object Text, `blueprint.name = component(...)` describes a
   top-level SCS Component in the Blueprint Actor context. This includes the
   Scene Root and actor-level non-Scene Components.
2. `parent.child = component(...)` describes an attached child Scene
   Component. Patch creation still uses a local binding followed by `add`.
3. `id` maps to `USCS_Node::VariableGuid`.
4. `type` maps to `USCS_Node::ComponentClass` as its native class path.
5. The child binding name maps to `USCS_Node::InternalVariableName`.
6. Binding paths express parent-child relationships; a parent statement
   precedes its children.
7. Sibling order is the statement order of children under the same parent.
8. Component-template properties are flattened onto the same Component object
   with their exact UE property names and native values.
9. An exact child read includes the shortest ancestor chain required to make
   its binding path unambiguous.

Sibling order is derived from statement order for the same parent. LGL text
does not require agents to write explicit order fields or a `ChildNodes` array,
and this readable order does not imply that UE exposes a corresponding reorder
operation.

Meaningful native `USCS_Node` fields retain their UE names and values, including
`CategoryName`, `AttachToName`, `ParentComponentOrVariableName`,
`ParentComponentOwnerClassName`, `bIsParentComponentNative`, and
`MetaDataArray`. The mapped identity fields, `ChildNodes`, deprecated fields,
and derived `CookedComponentInstancingData` are not repeated. The
`ComponentTemplate` pointer is not returned because its editable properties are
already fields on the Component.

Relationship-derived parent fields are read-only. `AttachToName` and other
native fields are writable only when the exact Component's `with schema`
result reports a native edit path; returning a field does not by itself make it
writable. Parent-child structure is edited through Component lifecycle
operations rather than by assigning cached SCS relationship fields.

If a native `USCS_Node` field and a component-template property have the same
name, the adapter reports an ambiguity instead of inventing a path syntax.
`with schema` identifies each usable field's source.

`UBlueprint::ComponentTemplates` entries used by AddComponent mechanisms are
not SCS Component Tree objects. Native or inherited components are not returned
as if the current Blueprint owned them; an external-parent reference requires a
separate design only when a concrete workflow needs it. LGL does not introduce
a `component_property` object.

Graph Nodes may carry native references to Components from this tree. Creation
uses the exact context-resolved Palette Entry; the graph adapter must not infer
a component-bound event target from nearby lines.

## Blueprint-Backed Graph Nodes

Blueprint-owned objects commonly appear as Graph Nodes. Their creation uses one
path: an exact UE Action Menu entry discovered and resolved by the Graph
adapter.

```lgl
query eventGraph
palette entries "Get Health"

query eventGraph
palette @P_GetHealth
with pins, defaults, schema

patch eventGraph
health = node(palette: "P_GetHealth")
add health
```

The Palette Entry identity records the exact variable, dispatcher, component,
function, event, or other UE creation action selected in that Graph context.
LGL does not maintain parallel `get(...)`, `set(...)`, `event(...)`, or
`call(...)` constructors.

## Query

Blueprint reads use the shared summary, collection search, exact local-name,
and exact stable-id model.

Orientation uses the shared `summary` primary operation:

```lgl
query door
summary
```

The Blueprint adapter returns the compact Blueprint object followed by comments
with counts for its owned Variables, Dispatchers, Graphs, and Components. It
does not expand those complete collections or return a summary object, section
object, or generic Member item:

```lgl
door = blueprint(asset: bpAsset, id: "blueprint-guid", type: BPTYPE_Normal, ParentClass: "/Script/Engine.Actor")

# variables: 8
# dispatchers: 2
# graphs: 5
# components: 4
```

The count labels also name the available collection operations. Collections
enumerate or search one concrete object kind:

```lgl
query door
variables ["text"]

query door
dispatchers ["text"]

query door
graphs ["text"]

query door
components ["text"]
```

Without search text, each operation enumerates its complete owned collection
through cursor pagination while preserving the corresponding UE authored
order. With search text, it performs adapter-owned search over the current
name, type, and relevant native descriptive fields. Collection results contain
compact concrete object identities rather than full Graph bodies, component
templates, or schemas.

Palette enumerates the creation capabilities valid for the bound Blueprint:

```lgl
query door
palette entries "Variable"

query door
palette @P_BlueprintVariable
with schema
```

The result is ordinary ordered Blueprint Object Text whose binding value is the
complete constructor `Call` to copy into a Patch:

```lgl
Variable = variable(
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
)

Dispatcher = dispatcher(palette: "P_BlueprintDispatcher")

FunctionGraph = graph(palette: "P_FunctionGraph")
```

The constructor name and argument shape are returned data rather than a
creation vocabulary the agent must memorize. The exact Palette read with
`with schema` describes required parameters and context constraints through
structured comments. A Patch may reuse a previously returned Palette id, but
`add` re-resolves it in the current Blueprint context. This section currently
confirms Variable, Dispatcher, and directly created Graph entries. Component
creation follows the same Blueprint Palette rule. Timeline Node creation uses
the Palette of its target Graph because Timeline is not a Blueprint lifecycle
object. Implementing an existing Interface Class changes the Blueprint Class
Contract rather than creating an Interface object, so it is not a Palette
entry.

Singular operations resolve one object by its current local name inside the
bound Blueprint:

```lgl
query door
variable Health

query door
dispatcher OnOpened

query door
graph EventGraph

query door
component DoorMesh
```

The operation supplies the concrete kind, so the adapter never guesses among
same-named kinds. Name lookup returns zero-match or ambiguity diagnostics
rather than falling back to another kind. Names are convenient current
locators, not rename-stable identity.

Stable-id lookup uses the exact returned object kind:

```lgl
query door
blueprint@blueprint-guid

query door
variable@variable-guid
```

The adapter resolves the typed id inside the Blueprint and selects exactly one
complete primary object. The object word must match the kind returned by the
earlier query; the adapter does not search other kinds or infer one from the id.
Required navigation context may follow that object: a Component includes its
shortest ancestor chain, and a Dispatcher includes its compact same-named
Signature Graph identity. This context does not expand a Graph body or other
related objects.

Blueprint exact name and stable-id reads do not accept `where`, `order by`, or
`page`. They accept the shared object-discovery expansion on a single resolved
object:

```lgl
query door
variable@variable-guid
with schema
```

Without `with schema`, the adapter returns the object's compact complete state;
native fields equal to their defined UE defaults may be omitted. `with schema`
keeps that object text and adds comments covering every usable LGL common and UE
native field, its native type text, access, default behavior, source, and known
constraints. It does not add a Blueprint schema object.

For a Component, schema is instance- and class-sensitive and may include the
component template properties the adapter can faithfully read or edit. The
ordinary exact read includes properties actually set on the component template
rather than all inherited class defaults.

Timeline discovery and exact reads use the Graph query model:

```lgl
query door
graphs "EventGraph"

query eventGraph
nodes "OpenTimeline"

query eventGraph
nodes
where type = "/Script/BlueprintGraph.K2Node_Timeline"

query eventGraph
node@timeline-node-guid
with schema
```

Timeline-aware Node search includes `TimelineName` in its adapter-owned search
metadata. The exact Node read returns its current Pins and flattened Timeline
state described above. There is no Blueprint `timelines`, `timeline <name>`, or
`timeline@id` operation and no Timeline-specific Graph query shortcut.

Zero matches return an unknown-object diagnostic. If invalid UE state produces
more than one match, the adapter returns an ambiguity diagnostic rather than
guessing from name or object type.

The normalized JSON representation of Blueprint collection, exact-name, and
typed `<object>@<id>` operations is intentionally not specified in this
document yet. It must be reviewed before schema work; this text contract does
not silently introduce new normalized `kind` values. The current experiment
also does not implement this query model or `with schema`.

## Patch

Blueprint Patch uses the shared Core lifecycle operations for its concrete
objects. Ordinary lifecycle does not turn UE editor utility names into parallel
`invoke` Operations. Target-local `invoke` remains reserved for native compound
behavior whose primary meaning is not direct object lifecycle. This section
confirms Class Settings mutation, Graph, Variable, Dispatcher, and
Component lifecycle plus the Blueprint-specific compound state of Timeline
Nodes.

### Class Settings Field Mutation

Existing Blueprint and Class Options use shared field `set` and `reset` with
their exact UE names:

```lgl
patch door

set blueprint@blueprint-guid.BlueprintDescription = "A usable door"
reset blueprint@blueprint-guid.BlueprintDescription
set blueprint@blueprint-guid.bGenerateAbstractClass = true
set blueprint@blueprint-guid.ShouldCookPropertyGuidsValue = Yes
```

`reset` restores the field's UE default. Setting the current value or resetting
an already-default field is a successful no-op and does not dirty the asset.
Ordinary Class Settings writes use native property change notification and mark
the Blueprint modified. They do not imply a full Blueprint compile. Fields that
affect Class flags or generated Class Metadata take effect in that derived
state when the Blueprint is subsequently compiled; mutation results report the
resulting dirty and compile state honestly.

`bDeprecate` follows the Blueprint Editor's specialized path and structurally
modifies the Blueprint, including Skeleton Class regeneration. A Blueprint
whose parent Class is deprecated cannot clear or reset this field. Level
Blueprint exact schema marks it unavailable. This structural update is still
not a full Blueprint compile.

`BlueprintNamespace` retains ordinary field syntax:

```lgl
set blueprint@blueprint-guid.BlueprintNamespace = "Game.Doors"
reset blueprint@blueprint-guid.BlueprintNamespace
```

The adapter additionally refreshes the Blueprint Namespace Registry, registers
a new non-empty Namespace, updates any open Blueprint Editor import context,
and applies UE's related explicit-import changes. Those changes appear as
ordinary Blueprint field effects; LGL does not add a Namespace operation.

`ImportedNamespaces` also uses whole-field `set` and `reset`:

```lgl
set blueprint@blueprint-guid.ImportedNamespaces = [
  "Game.Combat",
  "Game.UI"
]

reset blueprint@blueprint-guid.ImportedNamespaces
```

The adapter validates every newly added Namespace against UE's registered
Namespace set, diffs the old and requested `TSet`, and applies native import and
removal behavior. It updates an open Blueprint Editor context and loads newly
in-scope Blueprint libraries when required. If the Blueprint Editor is closed,
the persistent set is authoritative and UE rebuilds the context when the asset
is next opened. `reset` clears only explicit imports; derived Default Namespaces
remain effective. Creating a new Namespace belongs to setting
`BlueprintNamespace`, not inserting an unknown value into
`ImportedNamespaces`. No parallel `ImportNamespace` or `RemoveNamespace`
Operation is added.

### Parent Class Mutation

`ParentClass` remains an ordinary writable Blueprint field:

```lgl
patch door

set blueprint@blueprint-guid.ParentClass = "/Script/Engine.Actor"
```

The value is the exact native Class Path. A native Class uses a path such as
`/Script/Engine.Actor`; a Blueprint parent uses its Generated Class Path, such
as `/Game/Framework/BP_BaseActor.BP_BaseActor_C`, not the Blueprint Asset Path.
Setting the current parent again is a no-op.

Exact `with schema` output marks `ParentClass` as read and write, but not reset.
UE declares it as `TSubclassOf<UObject>` with `NoResetToDefault`, so LGL does
not invent a reset value. There is no parallel `Reparent` Operation and no
`force` or confirmation syntax. The explicit `set` authorizes the native
consequences of changing the authored parent.

Preflight resolves the Class and applies the same semantic restrictions used
by UE's reparent picker and Blueprint compiler mappings. It rejects at least:

- Interface and Function Library Blueprints, for which the editor does not
  expose reparenting
- unresolved, deprecated, newer-version, Interface, or non-Blueprint-base
  parent Classes
- the Blueprint's own Generated Class and any descendant that would create an
  inheritance cycle
- a Class requiring a different `UBlueprint` or
  `UBlueprintGeneratedClass` family
- a Class outside the resolved Blueprint's native `GetReparentingRules`
- family violations such as leaving the Actor, Anim Instance, Actor Component,
  or Level Script Actor hierarchy; Level Script reparenting remains native-only

`FKismetEditorUtilities::CanCreateBlueprintOfClass` is only one part of this
validation. It is not sufficient by itself to establish a legal reparent.

Application routes the field write through one adapter-owned reparent path
equivalent to the Blueprint editor action:

1. Begin one transaction and modify the Blueprint, its
   `USimpleConstructionScript`, and owned `USCS_Node` objects.
2. Gather old default Namespace imports, assign `ParentClass`, then preserve
   imports that cease to be defaults as explicit imports.
3. Ensure the Blueprint is current, including Construction Script, SCS root,
   parent-call, event, and Interface conformance.
4. Reconstruct all Nodes, mark the Blueprint modified, and prepare generated
   Sparse Class Data for the new parent.
5. Fully compile with UE's reparent flags
   `UseDeltaSerializationDuringReinstancing` and
   `SkipNewVariableDefaultsDetection`.
6. Ensure the Blueprint is current again against the newly generated Class.

The adapter must plan and report every determinable native effect. Important
effects include:

- a directly declared Interface already implemented by the new parent becoming
  inherited, with its implementation Graphs promoted to ordinary override
  Function Graphs
- parent Function Call Nodes being rebound to the authoritative new parent or
  removed when no matching parent Function exists
- invalid override Event Nodes being rebound or converted to Custom Events
- Default Scene Root and inherited Component attachment changes
- Node reconstruction, including changed Pins and broken or remapped Edges
- Class Default state being rebased onto the new parent, including loss of
  authored values for Properties that do not exist in the new hierarchy

Dry run resolves the same target, validates the same rules, and reports those
determinable effects without mutating or compiling the Blueprint. It does not
claim that a later full compile is guaranteed to succeed.

UE keeps the new `ParentClass` when the full compile completes with Blueprint
warnings or errors. LGL preserves that behavior: the mutation remains applied,
the returned ordinary Blueprint object contains the new `ParentClass`, and its
compile state and messages follow as comments. A compile diagnostic is
resulting Blueprint state rather than a partially applied Patch. Failure of the
adapter execution itself, or inability to complete a consistent reparent,
remains an apply failure and restores the whole Patch.

### Implemented Interface Operations

`ImplementedInterfaces` remains an ordered, readable
`TArray<FBPInterfaceDescription>`. It is not writable or resettable as one
field because its `Graphs` are UE-created owned structure. LGL does not add an
Interface object, `interface@id`, `interfaces` collection query, Interface
constructor, or Palette entry.

Adding an existing Interface Class in Class Settings uses one
schema-discovered Blueprint Operation:

```lgl
patch door

invoke blueprint@blueprint-guid ImplementInterface(
  Interface: "/Script/MyGame.Damageable"
)
```

`ImplementInterface` follows the editor action of that name and routes through
`FBlueprintEditorUtils::ImplementNewInterface`. `Interface` is the exact native
Interface Class Path and matches `FBPInterfaceDescription::Interface`; LGL does
not translate it into a friendly type or Asset Path.

Preflight requires `DoesSupportImplementingInterfaces` and rejects Macro
Library, Interface, Level Script, and Function Library Blueprints. The resolved
Class must be a non-deprecated, current, Blueprint-implementable Interface. It
must not carry `CannotImplementInterfaceInBlueprint`, be prohibited by the
parent Class's `ProhibitedInterfaces` metadata, or already be implemented
directly or through inheritance. An existing implementation is a conflict that
identifies its direct or inherited owner; it is not reported as a newly added
local declaration.

Before mutation, the adapter enumerates all inherited Interface Functions and
validates the complete generated surface. Animation Interface Functions require
an Anim Blueprint. Every required Function Graph name must be free of Graph,
Function, Variable, and other declaration conflicts that would make Interface
conformance invalid. UE's `ImplementNewInterface` creates Function Graphs as it
iterates and may encounter a later conflict after earlier Graphs already exist;
LGL therefore completes this validation before calling the native path and
rolls back any unexpected partial native result.

For each Interface Function that is overrideable but cannot be placed as an
Event, UE creates an Interface-owned Function Graph, sets its `InterfaceGuid`,
and makes it non-deletable. Animation Interface Functions use their native
Animation Graph path. Event-compatible Interface Functions create no Event Node
during this operation; their implementation remains the separately confirmed
`ImplementFunction` behavior. The new `FBPInterfaceDescription` is appended
only after all required Graphs are created, then the Blueprint is structurally
modified.

Removal is available only for an Interface directly present in
`ImplementedInterfaces`; an inherited Interface cannot be removed from the
child Blueprint. The caller must explicitly choose the native preservation
mode:

```lgl
invoke blueprint@blueprint-guid RemoveInterface(
  Interface: "/Script/MyGame.Damageable",
  bPreserveFunctions: true
)
```

`bPreserveFunctions` maps directly to
`FBlueprintEditorUtils::RemoveInterface` and is required even though the C++
API has a default. Omitting it is a validation error rather than silently
choosing the destructive mode. There is no additional confirmation or `force`
syntax.

With `bPreserveFunctions: false`, UE removes each Interface-owned Function
Graph and deletes each placed Interface Event Node. Their bodies, incident
Edges, and other native Graph-removal effects are included in dry run and the
mutation result.

With `bPreserveFunctions: true`, UE preserves implementation logic while
removing the Interface from Class Settings:

- each Interface Function Graph becomes an ordinary Function Graph
- `InterfaceGuid` is invalidated and the Graph becomes editable, renameable,
  and deletable
- callable, event, and public Function flags are added
- Function Entry and Result parameters become ordinary user-defined Pins
- animation linked-input poses are promoted when present
- each placed Interface Event Node becomes a same-named Custom Event with its
  position, signature, and Pin links preserved

After either mode, UE removes the direct `FBPInterfaceDescription`, refreshes
all Nodes, structurally modifies the Blueprint, and marks loaded child
Blueprints for reference fixup. The adapter must not invoke UE's modal prompt
for unloaded children. It marks and reports loaded children, reports unloaded
child Asset Paths as deferred refresh, and lets normal load-time conformance
repair them later rather than automatically loading and dirtying an unbounded
asset hierarchy.

Resolvable dependent Interface calls, casts, child Blueprints, removed Graphs,
converted Events, Pins, and Edges are reported as effects. The operation does
not silently delete unrelated external call sites. Both operations regenerate
structural Blueprint state but do not imply a full Blueprint compile.

Neither Operation defines primary output aliases. The returned ordinary
Blueprint Object Text contains the updated `ImplementedInterfaces` field and
interleaves every created or promoted compact Graph identity needed for
navigation. Removed Graphs and converted or removed graph-owned Event Nodes
remain mutation effects rather than new Blueprint object kinds. Graph-body
edits continue in a following Graph Patch using the returned `graph@id`.

### Graph Lifecycle

A directly authored top-level Graph is a first-class lifecycle object. Every
creation capability comes from the Palette of the bound Blueprint:

```lgl
query door
palette entries "Function Graph"

query door
palette @P_FunctionGraph
with schema

FunctionGraph = graph(palette: "P_FunctionGraph")
```

The opaque Palette id fixes the native Graph Class, Schema, ownership
collection, and creation path. The constructor therefore does not repeat
`type`, `Schema`, or a separate lifecycle-role value. `with schema` reports the
resulting native `type`, ownership, generated default Nodes, name constraints,
Blueprint capability constraints, and available lifecycle operations through
comments around the ordinary constructor binding.

Creation uses a local alias followed by shared `add`:

```lgl
patch door

OpenDoor = graph(palette: "P_FunctionGraph")
add OpenDoor
```

The local alias supplies the exact requested Graph name. `add` re-resolves the
Palette capability in the bound Blueprint and materializes exactly one Graph.
The first confirmed K2 capabilities follow their distinct native paths:

- Function Graph uses `CreateNewGraph` followed by `AddFunctionGraph`; the
  Schema creates its Function Entry and Result Nodes and applies the native
  function flags.
- Macro Graph uses `CreateNewGraph` followed by `AddMacroGraph`; the Schema
  creates its tunnel terminators and applies Macro Library public state where
  required.
- Event Graph uses `CreateNewGraph` followed by `AddUbergraphPage`; it creates
  the page but does not invent an Event Node.

The Palette is capability-driven rather than restricted to these three labels.
Blueprint subclasses and domain adapters may return other direct native Graph
creation capabilities when their real editor path is available. Availability
comes from the resolved Blueprint's native `SupportsFunctions`,
`SupportsMacros`, `SupportsEventGraphs`, and domain hooks rather than a fixed
LGL list. LGL does not infer the path from `EGraphType`: Function, Interface,
Override, and Dispatcher Signature Graphs may all report `GT_Function` while
having different owners and lifecycle rules.

Creation requires a valid unused name. `CreateNewGraph` can otherwise rename an
existing same-named object out of the way, and the editor commonly generates a
unique suffix. LGL permits neither implicit behavior: preflight returns a name
conflict and changes nothing. Function creation also invokes child-Blueprint
validation in UE. If that validation would silently rename a child Variable,
Component, Timeline, or override Graph, LGL returns a conflict until the caller
resolves it explicitly.

Success returns the materialized compact Graph identity in ordinary Blueprint
Object Text, including its generated `graph@id`, current name, native `type`,
`Schema`, and ownership comment. Generated default Nodes and other native
effects belong to the mutation effects or comments; the Blueprint result does
not expand the new Graph body. The returned `graph@id` is immediately usable as
a following Graph query or Patch target.

Existing Graph rename uses the common `name` field rather than an invented UE
field:

```lgl
set graph@graph-id.name = OpenDoorInternal
```

The adapter requires native rename permission and routes the operation through
`FBlueprintEditorUtils::RenameGraph`. It must update Function Entry and Result
references, local-variable scopes, dependent call Nodes, child override Graphs,
and other UE-maintained references. A collision or an undeclared child repair
fails preflight. All determinable affected Graphs, Nodes, and Blueprint assets
participate in the same atomic Patch and appear in the mutation effects.

Graph ordering uses shared `move` only where UE exposes authored ordering:

```lgl
move graph@open-id before graph@close-id
move graph@open-id after graph@initialize-id
```

The Graph and anchor must belong to the same Blueprint and the same native
collection. The adapter routes Function Graph and Macro Graph ordering through
`MoveGraphBeforeOtherGraph`. Event, Interface, Dispatcher Signature,
Construction Script, nested, and other non-reorderable Graphs reject `move`
with a capability diagnostic rather than directly rearranging an internal
array.

Deletion uses shared `remove` for a directly owned deletable Graph:

```lgl
remove graph@graph-id
```

Preflight checks the Graph's real owner, role, `bAllowDeletion`, and Schema.
Application gives `UEdGraphSchema::TryDeleteGraph` the first opportunity to
perform domain-specific deletion, then follows
`FBlueprintEditorUtils::RemoveGraph` when the Schema does not handle it. Native
effects such as removal of Macro Instance Nodes, nested Graphs, Timeline Nodes,
delegate refreshes, and incident Edges must be planned and reported.
Construction Script, Interface Graph, Dispatcher Signature Graph, and nested
Graph ownership reject direct Blueprint Graph removal and identify the owning
operation instead.

Deleting a user-authored Function Graph that is still called would follow UE's
confirmation path and may leave invalid call Nodes. LGL does not reproduce an
interactive confirmation or add `force`: preflight returns a conflict and
identifies the usage Nodes so the caller can remove or replace them explicitly.
Deleting an override Function Graph is allowed when resolution proves that the
parent Function remains the effective implementation. Macro deletion follows
UE's native removal of its Macro Instance Nodes and reports those deletions.

Not every Graph is directly created or owned by this lifecycle:

- Override implementation is the native compound `ImplementFunction` behavior.
  UE may create an Override Event Node in an existing Event Graph or a Function
  Graph, so it is a Blueprint `invoke`, not `add graph`:

  ```lgl
  invoke blueprint@blueprint-id ImplementFunction(
    function: "/Script/Engine.Actor:ReceiveAnyDamage"
  )
  ```

  The exact Function Path identifies the effective reflected Function. The
  operation returns the actual created or existing Node or Graph and is a no-op
  when that exact implementation already exists.
- Interface Graphs are effects of implementing or removing an Interface and
  remain owned by that Blueprint compound behavior.
- Dispatcher Signature Graphs remain effects of Dispatcher lifecycle.
- Construction Script is editor-maintained Blueprint structure. Its Graph body
  is editable when UE permits, but it has no direct create, rename, move, or
  remove capability.
- Collapsed and other nested Graphs remain effects of their owning Node or Graph
  transformation rather than top-level Blueprint Graph creation.

`Schema`, `bEditable`, `bAllowDeletion`, `bAllowRenaming`, and `InterfaceGuid`
describe native Graph state but are not ordinary writable fields. `id` and
`type` remain read-only common fields. Exact `with schema` output reports those
permissions and the operations available on the resolved Graph; callers cannot
enable a lifecycle operation by setting an internal flag.

All confirmed Graph lifecycle paths call UE's structural-modification path,
which regenerates the Skeleton Class, notifies observers, and marks the package
dirty. This is not a full Blueprint compile. Graph mutation results must report
the resulting compile state honestly; the separate Blueprint compile and
diagnostic policy remains to be designed.

### Variable Lifecycle

A Blueprint-authored Variable is a first-class lifecycle object. Creation uses
an unmaterialized member-path binding followed by `add`:

```lgl
patch door

door.Health = variable(
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
)

add door.Health
```

The binding owner selects the Blueprint and the binding member supplies the
exact requested `FBPVariableDescription::VarName`. `palette` selects the exact
parameterized Blueprint Variable creation capability returned by Palette, and
`type` supplies `FBPVariableDescription::VarType`. The member path is an
ordinary creation-time alias, not a `Member` object. After `add`, it is
materialized and may be used by later ordered statements in the same Patch.
The mutation result returns the UE-generated `VarGuid` as the Variable's stable
`id`; later requests use `variable@id`.

The Palette-returned Variable constructor accepts the required `palette` and
`type`. It does not accept `id`, which UE generates, or duplicate writable
declaration fields. Those use ordinary ordered `set` statements after
materialization:

```lgl
set door.Health.Category = Stats
set door.Health.FriendlyName = "Player Health"
```

The whole Patch is atomic, so this separation does not expose a partially
configured Variable. Creation uses the exact requested name and returns a
conflict rather than silently applying `FindUniqueKismetName` or another suffix.
The adapter calls the native `FBlueprintEditorUtils::AddMemberVariable` path,
which creates the GUID and native initial `FriendlyName`, flags, category,
type-specific metadata, then marks the Blueprint structurally modified. That
regenerates the Skeleton Class but is not a full Blueprint compile.

The constructor does not expose `DefaultValue`. That field is compiler staging
text in `FBPVariableDescription`, not the effective Variable default. Effective
defaults remain Class Defaults and are edited through the Class domain after
the generated Property exists. A future cross-domain compound-operation design
may make that multi-step workflow atomic without redefining Variable
`DefaultValue`.

Existing Variable declarations use shared `set` and `reset`:

```lgl
set variable@variable-id.VarName = MaxHealth
set variable@variable-id.type = "<FEdGraphPinType native text>"
set variable@variable-id.Category = Stats
set variable@variable-id.PropertyFlags = "<EPropertyFlags native text>"
set variable@variable-id.RepNotifyFunc = OnRep_MaxHealth
reset variable@variable-id.RepNotifyFunc
```

`VarName` routes through `FBlueprintEditorUtils::RenameMemberVariable`; the
adapter must not mutate the description field directly. Compact result text
continues to express the current name through the Variable binding path rather
than duplicating `VarName` as a constructor field. `type` routes through
`FBlueprintEditorUtils::ChangeMemberVariableType`. `id` is read-only. Every
other native field is writable or resettable only when the exact Variable's
`with schema` result says so; `reset` is not a blanket assignment of an empty
value.

UE's native rename path opens a confirmation dialog and clears
`RepNotifyFunc` when the Variable has an associated RepNotify function. LGL
does neither implicitly. Such a rename fails preflight until the caller makes
the loss explicit and orders it before the rename:

```lgl
reset variable@variable-id.RepNotifyFunc
set variable@variable-id.VarName = MaxHealth
```

An explicit `set ...type` authorizes the native consequences of changing the
Variable type; there is no `force` or confirmation syntax. Preflight and the
mutation result must report all determinable effects, including reconstruction
of referencing Variable Nodes in the Blueprint and loaded child Blueprints,
broken incompatible Edges, Boolean `FriendlyName` adjustment, and clearing
replication state that UE does not permit for Map or Set types. All affected
loaded Blueprints participate in the same atomic Patch.

Variable display order is authored order in `UBlueprint::NewVariables`. It uses
the shared `move` operation rather than target-local Operations:

```lgl
move variable@health-id before variable@armor-id
move variable@health-id after variable@stamina-id
```

The Variable and anchor must belong to the same Blueprint. The adapter routes
these forms through `MoveVariableBeforeVariable` and
`MoveVariableAfterVariable` and performs the required structural-modification
path.

Deletion uses the shared lifecycle operation:

```lgl
remove variable@variable-id
```

The adapter routes it through `FBlueprintEditorUtils::RemoveMemberVariable`.
Native deletion removes the `FBPVariableDescription`, all getter and setter
Nodes referencing that Variable and their incident Edges, relevant Field
Notify metadata, and marks the Blueprint structurally modified. Explicit
`remove` is the deletion authorization; LGL adds no `force` argument. Preflight
must enumerate these determinable effects, and any failure leaves the entire
Patch unchanged.

`AddMemberVariable` and `RenameMemberVariable` normally call
`ValidateBlueprintChildVariables`, which can silently rename a conflicting
child Blueprint Variable, Component, Timeline, or Function Graph. LGL treats
that repair as an undeclared cross-object mutation. Preflight must resolve the
relevant child Blueprints and return a conflict before apply whenever add or
rename would trigger such a repair. The caller must explicitly resolve the
child collision and retry; the adapter must not let behavior depend on which
child assets happened to be loaded.

Variable therefore exposes no lifecycle `invoke` Operations. Its closed
mutation surface is binding plus `add`, field `set` and `reset`, collection
`move`, and `remove`.

### Dispatcher Lifecycle

A Dispatcher is one first-class lifecycle object with two required UE backing
objects: a multicast-delegate `FBPVariableDescription` and a same-owned,
same-named Delegate Signature Graph. Every lifecycle operation validates and
updates that pair atomically.

Creation uses an unmaterialized member-path binding followed by `add`:

```lgl
patch door

door.OnOpened = dispatcher(palette: "P_BlueprintDispatcher")

add door.OnOpened
```

The binding owner selects the Blueprint and the binding member supplies the
exact name for both backing objects. `palette` selects the native composite
Event Dispatcher creation capability. The returned call needs no `type`
argument because that capability fixes the backing Variable to UE's
multicast-delegate Pin type. The adapter follows the native Event Dispatcher
creation path: create the member Variable, create the Signature Graph and its
default terminator Nodes, make its Function Entry signature-editable, add the
required function and Property flags, register the Graph in
`DelegateSignatureGraphs`, and mark the Blueprint structurally modified.
Failure at any point rolls back both objects and the rest of the Patch.

Dispatcher creation is unavailable for Blueprint Interface, Macro Library, and
Function Library assets, matching the native editor action. The requested name
must be valid and unused by both the Blueprint declaration namespace and its
Graph objects. `FBlueprintEditorUtils::CreateNewGraph` can otherwise rename an
existing same-named Graph out of the way; LGL preflight returns a conflict
instead and never permits that implicit repair.

The mutation result uses the same ordered object text as an exact Dispatcher
read: the materialized Dispatcher is followed immediately by its compact
Signature Graph identity. The caller therefore receives both the Dispatcher's
stable Variable `id` and the Signature Graph's `graph@id` without another
query. This does not add a `graph` field to Dispatcher or another Patch output
syntax.

Existing Dispatcher declaration fields use shared `set` and `reset`:

```lgl
set dispatcher@dispatcher-id.VarName = OnClosed
set dispatcher@dispatcher-id.Category = Events
```

`VarName` routes through `FBlueprintEditorUtils::RenameMemberVariable` and must
atomically rename the backing Variable and Signature Graph, update same-context
Dispatcher Nodes and dependent Variable references, and structurally modify
every affected loaded Blueprint. Preflight validates the destination Variable
and Graph namespaces before either half changes and applies the same child
Blueprint collision policy as Variable rename.

Dispatcher `id` and `type` are read-only. Changing `type` would convert only the
backing Variable and orphan the Signature Graph, so it is not a supported
conversion. Required multicast-delegate Property flags likewise cannot be
cleared. Remaining native declaration fields are writable or resettable only
as reported by the exact Dispatcher's `with schema` result.

Dispatcher authored order uses the shared `move` operation:

```lgl
move dispatcher@opened-id before dispatcher@closed-id
move dispatcher@opened-id after dispatcher@damaged-id
```

Both Dispatchers must belong to the same Blueprint and Dispatcher collection.
The adapter routes the reorder through the native member-variable ordering path
without allowing a cross-section Variable anchor.

Deletion uses the shared lifecycle operation:

```lgl
remove dispatcher@dispatcher-id
```

The native editor removes the backing member Variable and Signature Graph as
one user action, but it does not delete every `UK2Node_BaseMCDelegate` usage
that will become invalid. LGL neither leaves those errors silently nor expands
`remove dispatcher` into non-native deletion of unrelated Graph Nodes. If any
resolvable Dispatcher usage Node would remain invalid, preflight returns a
conflict and identifies those Nodes. The caller explicitly removes the usages
through their owning Graph Patch, then retries Dispatcher removal.

Once unused, removal atomically follows
`FBlueprintEditorUtils::RemoveMemberVariable` and
`FBlueprintEditorUtils::RemoveGraph`, including the normal Graph deletion
cleanup and structural-modification path. If the backing Variable or Signature Graph is
missing, duplicated, differently named, or otherwise mismatched, the adapter
returns an inconsistent-Blueprint diagnostic and changes neither half.

Dispatcher signature parameters remain authoritative Pins on the returned
Signature Graph. Editing them stays in a following Graph Patch and reuses the
confirmed signature Operations:

```lgl
patch graph@signature-graph-id
invoke graph@signature-graph-id AddInputParameter(
  name: Damage,
  type: "<FEdGraphPinType native text>"
)
```

LGL does not add a cross-domain Patch form merely to combine Dispatcher
creation and signature editing. Dispatcher therefore exposes no lifecycle
`invoke` Operations: binding plus `add`, field `set` and `reset`, collection
`move`, and `remove` own its declaration lifecycle; Graph `invoke` owns only
its signature contents.

### Component Lifecycle

A Blueprint-authored Component is one first-class lifecycle object backed by a
`USCS_Node` and its Component Template. This lifecycle covers only SCS
Components owned by the bound Blueprint. Native, inherited, instanced, and
Child Actor subtree Components remain outside this ownership boundary and are
never presented as directly editable local Components.

Every direct Component creation capability comes from the Blueprint Palette:

```lgl
query door
palette entries "Static Mesh Component"

query door
palette @P_StaticMeshComponent
with schema
```

The exact Palette result is an ordinary Component constructor binding:

```lgl
StaticMeshComponent = component(palette: "P_StaticMeshComponent")
```

The opaque Palette id fixes the native `UActorComponent` Class and may also fix
an asset-backed initialization path discovered through
`FComponentAssetBrokerage`. The constructor does not repeat `type` or invent a
separate Component-kind value. `with schema` reports the resulting native
Class path, whether the Component is a Scene Component, valid placement,
initial fields, and available lifecycle Operations.

Creation keeps the local alias separate from placement:

```lgl
patch door

Mesh = component(palette: "P_StaticMeshComponent")
add Mesh to component@root-guid

Audio = component(palette: "P_AudioComponent")
add Audio
```

The local alias supplies the exact requested Component variable name. The
adapter validates that name before mutation, then folds native
`AddNewSubobject` creation and `RenameComponentMemberVariable` into the same
atomic transaction. UE may otherwise generate a unique suffix or rename a
colliding child-Blueprint declaration; LGL treats either implicit repair as a
preflight conflict.

`add binding to component@parent-id` requires both objects to be compatible
Scene Components in the same Blueprint. It attaches through UE's native SCS
path. If the explicit parent is invalid, LGL rejects the operation instead of
accepting `FindParentForNewSubobject` fallback to another Scene Root.

Bare `add binding` creates from the Blueprint Actor context. A non-Scene
Component remains actor-level. A Scene Component attaches to UE's current
default attach or Scene Root Component, or becomes a top-level SCS Component
when no attachable Scene Root exists. The result always returns the actual
materialized binding path. Component creation has no `before` or `after` form.

Existing declaration and Component Template fields use shared `set` and
`reset`:

```lgl
set component@mesh-guid.name = DoorMesh
set component@mesh-guid.StaticMesh = "/Game/Meshes/SM_Door.SM_Door"
reset component@mesh-guid.StaticMesh
```

The common `name` field maps to `USCS_Node::InternalVariableName` and routes
through `FBlueprintEditorUtils::RenameComponentMemberVariable`. Native rename
updates same-Blueprint variable references and dependent inheritable Component
Templates while preserving `VariableGuid`. A collision, invalid exact name,
or undeclared child-Blueprint rename fails preflight.

Component `id` and `type` are read-only. SCS parent fields are derived
relationship state and cannot be assigned to move a Component. Other returned
SCS and Component Template fields are writable or resettable only as reported
by the exact Component's schema and must use their native edit path.

LGL exposes no SCS Component class conversion. UE's
`ChangeSubobjectClass` path applies only to supported native Component Class
overrides and does not change an ordinary SCS Component. That inherited/native
override model requires its own object design rather than a misleading
`set component.type`.

An existing Scene Component changes parent through shared `move`:

```lgl
move component@trigger-guid to component@root-guid
```

The adapter validates `CanReparent`, same-Blueprint ownership, cycle safety,
`CanAttachAsChild`, Mobility, Editor-only state, and the remaining native
attachment rules, then routes the edit through `ReparentSubobjects`. It uses a
deterministic preview Actor context for UE's world-to-relative transform
conversion and native propagation to instances. If that context cannot be
resolved, the operation returns a capability diagnostic instead of editing SCS
arrays directly. Moving to the current parent is a no-op.

Component `move` has no `before`, `after`, actor-level destination, `detach`,
`attach`, or `reparent` alias. UE exposes readable `USCS_Node::ChildNodes`
order, but its Component editor does not expose a stable authored sibling
reorder path. Detaching a Scene Component in the editor means reattaching it to
the current Scene Root, which is already expressed by an explicit
`move component to component@root-id`.

Promoting an existing Scene Component to Actor Root is native compound behavior
discovered through that Component's schema:

```lgl
invoke component@mesh-guid MakeNewSceneRoot()
```

The operation follows `USubobjectDataSubsystem::MakeNewSceneRoot`: it validates
that the current Root is replaceable, clears the new Root's attachment socket,
resets its relative location and rotation through UE's path, promotes it, and
reattaches the old Root below it. Replacing a local Default Scene Root removes
or hides that default node as UE requires. All hierarchy and transform changes
are returned as mutation effects. LGL introduces no writable `Root` field and
does not overload ordinary `move` with this behavior.

Deletion uses shared `remove`:

```lgl
remove component@mesh-guid
```

Preflight requires native `CanDelete`. Application follows
`DeleteSubobjects` and `USimpleConstructionScript::RemoveNodeAndPromoteChildren`:
only the target Component is deleted. For a non-root target, UE moves its
children directly to the removed node's parent at the removed position. For an
SCS Root target, UE may promote one compatible child to the Root set and move
the remaining children below it before validating the Scene Root set. LGL does
not reinterpret either path as subtree deletion; the complete planned
hierarchy change appears in dry run and mutation effects.

UE removes variable getter and setter Nodes for the deleted Component, and LGL
reports those native deletions. UE does not remove Component Bound Event Nodes
that become invalid. If any such usage is resolvable, LGL preflight returns a
conflict and identifies the Nodes so the caller can remove them explicitly in
their owning Graph Patch before retrying Component removal. Direct removal of
Default Scene Root is unavailable; replacement uses `MakeNewSceneRoot()`.

Native single-Component duplication remains a target-local Operation:

```lgl
invoke component@mesh-guid Duplicate() as copy
```

It follows `DuplicateSubobjects` for exactly one target, copies the Component
Template state, creates a new name and `VariableGuid`, and attaches the result
under the corresponding native parent. It does not implicitly duplicate the
source's descendants. The output alias names the returned Component and may be
renamed by a following `set copy.name = ...` statement. Availability follows
native `CanDuplicate` and requires a representable destination.

Duplicate is an effect of an existing object's native Operation, not a second
direct constructor path; direct creation still requires Palette. `Cut`,
`Copy`, and `Paste` remain editor clipboard commands rather than LGL Patch
operations.

The complete current Component lifecycle surface is:

| Operation | Meaning |
| --- | --- |
| `add binding [to component]` | create one Palette-backed SCS Component |
| `set component.field = value` | rename or write one schema-approved native field |
| `reset component.field` | restore one field through its schema-approved native reset path |
| `move component to component` | reattach one existing owned Scene Component |
| `remove component` | delete one Component through native child-promotion behavior |
| `invoke component MakeNewSceneRoot()` | promote one existing Scene Component through UE's root replacement path |
| `invoke component Duplicate()` | duplicate one existing Component through UE's native template-copy path |

All creation, rename, reparent, root replacement, deletion, and duplication
paths structurally modify the Blueprint and report affected Component
Templates, Graph Nodes, child Blueprints, and instances when determinable.
They do not imply a full Blueprint compile.

### Timeline Node Lifecycle and Operations

A Timeline is created through the Palette of its target Graph. The returned
constructor is still `node(...)`; LGL does not add a Timeline constructor:

```lgl
query eventGraph
palette entries "Timeline"

query eventGraph
palette @P_Timeline
with schema

OpenTimeline = node(
  palette: "P_Timeline",
  TimelineName: OpenDoor,
  TimelineLength: 1.0,
  LengthMode: TL_TimelineLength,
  bLoop: false
)
```

Creation uses the ordinary Graph binding plus `add`:

```lgl
patch eventGraph

openTimeline = node(
  palette: "P_Timeline",
  TimelineName: OpenDoor,
  TimelineLength: 1.0,
  LengthMode: TL_TimelineLength
)

add openTimeline
```

`TimelineName` is required for deterministic creation. Other schema-writable
Timeline fields may be supplied as initial fields. Track arrays and display
order are read-only structural state and are not accepted in the constructor.
Application follows the native Timeline Node spawner and
`FBlueprintEditorUtils::AddNewTimeline`, producing the Node and Template as one
atomic result.

The Palette entry is available only when the Blueprint is Actor-based,
supports Event Graphs, and `FBlueprintEditorUtils::DoesSupportTimelines`
succeeds. `UK2Node_Timeline::IsCompatibleWithGraph` further restricts placement
to an Event Graph or a compatible nested Composite whose outer chain ultimately
belongs to an Ubergraph. A stale Palette id is revalidated during `add`.

Ordinary writable Timeline state uses `set` and `reset`:

```lgl
set node@timeline-node-guid.TimelineName = OpenDoor
set node@timeline-node-guid.TimelineLength = 2.0
set node@timeline-node-guid.LengthMode = TL_TimelineLength
set node@timeline-node-guid.bAutoPlay = false
set node@timeline-node-guid.bLoop = true
set node@timeline-node-guid.bReplicated = false
set node@timeline-node-guid.bIgnoreTimeDilation = false
set node@timeline-node-guid.TimelineTickGroup = TG_PrePhysics
set node@timeline-node-guid.MetaDataArray = []
```

`TimelineName` routes through `FBlueprintEditorUtils::RenameTimeline`. The
native path updates the Node, Template object and cached names, Timeline
Variable Nodes and Pins, and child-variable validation. A name or Template-name
collision fails preflight. `TimelineLength` must be greater than
`KINDA_SMALL_NUMBER`. `LengthMode`, `TimelineTickGroup`, Metadata, and Boolean
values preserve their UE native text and behavior. Exact `with schema` output
reports field access and reset support for the resolved instance.

`EventTracks`, `FloatTracks`, `VectorTracks`, `LinearColorTracks`, and
`TrackDisplayOrder` are readable but not directly writable or resettable. Their
coordinated edits use target-local Operations.

New internal Tracks use the UE Track kinds directly:

```lgl
invoke node@timeline-node-guid AddFloatTrack(
  TrackName: Alpha
) as alpha

invoke node@timeline-node-guid AddVectorTrack(
  TrackName: Position
) as position

invoke node@timeline-node-guid AddEventTrack(
  TrackName: OnHalfway
) as onHalfway

invoke node@timeline-node-guid AddLinearColorTrack(
  TrackName: Tint
) as tint
```

`TrackName` is required and must be unique across all Track arrays and the
Timeline's fixed Pins. A new Track is appended to native display order. Each
Operation reconstructs the Timeline Node and has one primary output: the final
Track Pin. Single-output `as <alias>` infers that Pin kind from schema.

An existing Curve Asset can create an external Track:

```lgl
invoke node@timeline-node-guid AddTrackFromCurve(
  Curve: "/Game/Curves/C_DoorAlpha.C_DoorAlpha"
) as alpha
```

The native Curve Asset name becomes `TrackName`. `UCurveFloat` creates a Float
Track unless `bIsEventCurve` is true, in which case it creates an Event Track;
`UCurveVector` and `UCurveLinearColor` create their corresponding Tracks. A
different desired name is a following `RenameTrack` Operation.

Existing Track structure uses three Operations:

```lgl
invoke node@timeline-node-guid RenameTrack(
  TrackName: Alpha,
  NewName: Opacity
)

invoke node@timeline-node-guid MoveTrack(
  TrackName: Opacity,
  Before: Position
)

invoke node@timeline-node-guid RemoveTrack(
  TrackName: Opacity
)
```

`MoveTrack` requires exactly one of `Before` or `After` and resolves both names
in the same Timeline. It changes only `TrackDisplayOrder` and resulting Pin
display order. `RenameTrack` updates the generated Pin and native derived names
while preserving links. `RemoveTrack` removes the Track, display record, and
generated Pin; links incident to that Pin are removed as native effects.

Internal Curve Keys use Track name, native channel when present, and time as
their exact selector. `FKeyHandle` remains transient and never crosses the LGL
boundary:

```lgl
invoke node@timeline-node-guid AddKey(
  TrackName: Position,
  Channel: X,
  Time: 1.0,
  Value: 100.0,
  InterpMode: RCIM_Cubic,
  TangentMode: RCTM_Auto
)

invoke node@timeline-node-guid SetKey(
  TrackName: Position,
  Channel: X,
  Time: 1.0,
  NewTime: 1.5,
  Value: 150.0,
  InterpMode: RCIM_Linear
)

invoke node@timeline-node-guid RemoveKey(
  TrackName: Position,
  Channel: X,
  Time: 1.5
)
```

Vector Tracks require `Channel: X|Y|Z`; Linear Color Tracks require
`Channel: R|G|B|A`. Float and Event Tracks reject `Channel`. Non-event Tracks
require a float `Value`. Event Keys may omit `Value`, which then uses the
native `FRichCurveKey` default `0.0`; explicit values remain readable and
writable for lossless state preservation.

`AddKey` also accepts the optional native `FRichCurveKey` fields
`InterpMode`, `TangentMode`, `TangentWeightMode`, `ArriveTangent`,
`ArriveTangentWeight`, `LeaveTangent`, and `LeaveTangentWeight`. Omitted fields
use the UE defaults `RCIM_Linear`, `RCTM_Auto`, `RCTWM_WeightedNone`, and zero
tangent values. `SetKey` modifies only explicitly supplied fields; `Time`
selects the current Key and optional `NewTime` moves it. Adding at an occupied
time, moving onto an occupied time, or resolving duplicate source times returns
a conflict or ambiguity diagnostic rather than overwriting or merging Keys.
Native time sorting and tangent recalculation are preserved.

Key Operations are available only for internal Curves. Existing Tracks switch
between Curve ownership modes through two Operations:

```lgl
invoke node@timeline-node-guid UseExternalCurve(
  TrackName: Position,
  Curve: "/Game/Curves/C_Position.C_Position"
)

invoke node@timeline-node-guid UseInternalCurve(
  TrackName: Position
)
```

`UseExternalCurve` replaces the current Curve reference and sets
`bIsExternalCurve = true`; it does not copy the current internal Keys into that
Asset. Dry run must expose the replaced internal state. Event and Float Tracks
accept `UCurveFloat`, Vector accepts `UCurveVector`, and Linear Color accepts
`UCurveLinearColor`. `UseInternalCurve` is available only for an external Track
and follows UE's native path to create an owned Curve and copy every current
`FRichCurveKey` before clearing `bIsExternalCurve`.

External Curve Keys are not expanded or mutated through the Timeline Node.
Creating a new external Curve Asset is also not a Timeline Operation: Asset
creation remains Palette-backed and the independent Curve Asset must own any
future editing contract.

Native Timeline duplication remains a target-local Operation:

```lgl
invoke node@timeline-node-guid Duplicate() as copy
```

UE creates a new Node Guid, unique Timeline name, Template, and internal
Timeline Guid. It copies Timeline fields, Tracks, display order, and internal
Curves while preserving external Curve references. Connections to other Graph
Nodes are not duplicated. The single output alias resolves to the final copied
Node and may be renamed, moved, or connected by following Patch statements.
Duplicate is not a second constructor path; direct creation still requires
Palette.

Deletion uses ordinary Graph Node lifecycle:

```lgl
remove node@timeline-node-guid
```

The adapter must call the Timeline Node deletion path.
`UK2Node_Timeline::DestroyNode` removes and relocates the backing Template before
deleting the Graph Node and its incident Edges. Calling
`FBlueprintEditorUtils::RemoveTimeline` alone is insufficient because that API
does not remove the Graph Node. Every operation first validates a unique
Node/Template pair; missing or duplicated halves invalidate the entire Patch.

The complete Timeline surface is:

| Operation | Meaning |
| --- | --- |
| Graph `nodes` / `node@id` | discover or exactly read the compound Timeline Node |
| Graph Palette plus `add node` | create one Node and backing Template |
| `set/reset node.field` | edit one schema-approved Timeline field |
| `invoke node Add*Track()` | append one internal native Track |
| `invoke node AddTrackFromCurve()` | append one external Track from an existing Curve Asset |
| `invoke node RenameTrack()` | rename one Track and generated Pin |
| `invoke node MoveTrack()` | change native Track display order |
| `invoke node RemoveTrack()` | remove one Track and generated Pin |
| `invoke node AddKey/SetKey/RemoveKey()` | edit one internal Curve Key |
| `invoke node UseExternalCurve/UseInternalCurve()` | switch Curve ownership using UE behavior |
| `invoke node Duplicate()` | duplicate the compound Timeline Node |
| `remove node` | delete the Node and backing Template |

## Adapter Boundary

Pure LGL normalization may:

- preserve concrete Blueprint object bindings and component statement order
- normalize Blueprint query clauses into a structured query object
- normalize schema-approved Blueprint and Class Option field `set` and `reset`
  without interpreting their UE semantics
- normalize `set blueprint.ParentClass` as an ordinary field write
- preserve Blueprint `ImplementInterface` and `RemoveInterface` invokes and
  their named native values without interpreting them
- normalize confirmed Graph, Variable, Dispatcher, and Component bindings and
  common Patch operation forms
- preserve Timeline Node native fields and `invoke` text without interpreting
  their UE meaning

Pure LGL normalization must not:

- derive Default Namespaces or validate registered Namespace paths
- apply Blueprint Namespace Registry or editor-context side effects
- decide instance-level Class Settings availability or property editability
- resolve parent classes or interface classes
- decide whether a class can be a Blueprint parent
- validate Blueprint family and reparenting rules or determine reparent
  cascades and compile results
- decide whether a Blueprint can implement an Interface, validate the complete
  Interface Function surface, or determine add, remove, preservation, and child
  Blueprint effects
- validate UE pin types
- validate Variable names or inherited and child Blueprint collisions
- determine Variable edit cascades or affected Blueprint assets
- decide which Graph creation capabilities the resolved Blueprint supports
- infer Graph lifecycle role from `EGraphType`
- validate Graph names, ownership, lifecycle permissions, or child-Blueprint
  collisions
- determine Graph creation, rename, reorder, removal, or override effects
- resolve whether `ImplementFunction` produces an Event Node or Function Graph
- validate Dispatcher backing-object consistency, availability, or usage Nodes
- determine Dispatcher rename, removal, and dependent-Blueprint effects
- resolve Component Palette Classes or asset-backed creation capabilities
- validate Component names, placement, attachment, root replacement,
  deletion, duplication, or affected child Blueprints and Graph Nodes
- resolve the preview Actor context required for native Component reparenting
- determine Component child-promotion, transform, template, or instance
  effects
- pair a Timeline Node with its Template or validate Timeline Graph
  compatibility
- resolve Timeline Palette availability, Track names, native Curve channels,
  Key selectors, or external Curve Asset compatibility
- determine Timeline rename, reconstruction, duplication, deletion, or Curve
  ownership effects
- validate function override eligibility
- synthesize inherited override signatures
- inspect or mutate SCS component templates
- compile Blueprints

The adapter or bridge owns those UE-dependent responsibilities and must use UE
APIs such as `FKismetEditorUtilities`, `FBlueprintEditorUtils`,
`USimpleConstructionScript`, `USCS_Node`, `UK2Node_CustomEvent`, and
`UK2Node_Timeline`. Generated Class and CDO access belongs to the Class
Reflection and Class Defaults designs.

The current Bridge `addInterface` and `removeInterface` helpers do not yet
satisfy this contract. Their dry run does not perform live Interface validation,
addition can inherit UE's partial-Graph failure path, missing or inherited
removal is treated as success, and removal may enter UE's interactive child-load
prompt. They must be replaced by the adapter-owned non-interactive planning and
transaction path above before this LGL surface is implemented.
