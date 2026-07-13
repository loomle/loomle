# Blueprint Domain

## Scope

The blueprint domain describes Blueprint asset structure outside individual
graph nodes and edges. It covers class contract, implemented interfaces, class
options, variables, dispatchers, graphs, SimpleConstructionScript components,
and timelines. Effective Class Default Object state belongs to the class
domain and is not duplicated here.

Function, macro, event, delegate-signature, interface-function, and construction
script graphs are all graph objects. Their nodes, pins, and edges remain graph
domain work.

Some graph nodes reference Blueprint-owned variables, dispatchers, components,
or timelines. The graph domain owns those nodes; the blueprint domain owns the
referenced object identities.

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

The read model exposes six concrete objects: Blueprint, Variable, Dispatcher,
Graph, Component, and Timeline. It does not introduce a common Blueprint
`Member` object. Generated classes, generated variables, intermediate graphs,
and compiled event graphs are derived state rather than additional authored
objects.

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

door.OpenTimeline = timeline(id: "timeline-guid", type: "/Script/Engine.TimelineTemplate")
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

## Class Contract

Class contract fields describe what the current `UBlueprint` authors for its
generated class:

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
  HideCategories: ["Rendering"]
)
```

Canonical text should keep constructor arguments named. Formatters may keep the
binding on one line when it is short.

Persisted authored options retain their exact UE names. Supported fields include
`ParentClass`, `bRunConstructionScriptOnDrag`,
`bRunConstructionScriptInSequencer`, `bGenerateConstClass`,
`bGenerateAbstractClass`, `bDeprecate`, `ShouldCookPropertyGuidsValue`,
`CompileMode`, `BlueprintDisplayName`, `BlueprintDescription`,
`BlueprintNamespace`, `BlueprintCategory`, `HideCategories`,
`ImportedNamespaces`, and `CategorySorting`. Fields equal to defined UE
defaults may be omitted; `with schema` reports all usable fields.

Implemented interfaces preserve the native `FBPInterfaceDescription` shape:

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
to Class Reflection.

`GeneratedClass` is derived navigation information and is returned as an
immediately following comment with its exact Class Path. Compile status and
errors are diagnostic comments. `SkeletonGeneratedClass`, transient compile
state, internal versioning, editor-session state, generated collections, and
caches are not authored Class Contract fields.

`ComponentClassOverrides` and `InheritableComponentHandler` belong to
Component Template resolution. They are not flattened into Class Contract or
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
The six concrete object types are Blueprint, Variable, Dispatcher, Graph,
Component, and Timeline.

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

Implemented interfaces remain native Blueprint Class Contract data:

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

### Timelines

Timeline is the only new concrete object constructor in this read design. It
maps directly to `UTimelineTemplate`:

```lgl
door.OpenTimeline = timeline(
  id: "timeline-guid",
  type: "/Script/Engine.TimelineTemplate",
  TimelineLength: 1.0,
  LengthMode: TL_TimelineLength,
  bAutoPlay: false,
  bLoop: false,
  bReplicated: false,
  bIgnoreTimeDilation: false,
  TimelineTickGroup: TG_PrePhysics
)
```

The binding name maps to `UTimelineTemplate::VariableName`, `id` maps to
`TimelineGuid`, and `type` is the object's native UE class path. Authored fields
retain their UE names and native values: `TimelineLength`, `LengthMode`,
`bAutoPlay`, `bLoop`, `bReplicated`, `bIgnoreTimeDilation`, `MetaDataArray`, and
`TimelineTickGroup`.

Tracks remain native Timeline fields rather than new LGL objects:
`EventTracks`, `FloatTracks`, `VectorTracks`, `LinearColorTracks`, and the
authored editor order `TrackDisplayOrder`. Their values use UE native text,
preserving Track names, event keys, and curve references without introducing a
`track(...)` constructor. Curve key data owned by a referenced Curve remains on
that Curve object. Inspecting or editing it requires Curve/object support rather
than copying it into a new Timeline-specific object.

Cached names derived from the Timeline name, such as `DirectionPropertyName`,
`UpdateFunctionName`, and `FinishedFunctionName`, are not returned as authored
fields. Pure editor presentation state such as Track expansion and curve-view
synchronization is also omitted.

### Graph-Owned Nodes

Custom events, override events, component-bound events, function entry/result
nodes, macro tunnels, and Timeline nodes are Graph Nodes. They are not Blueprint
objects and are not repeated in the Blueprint object model.

Graph nodes may reference a Variable, Dispatcher, Component, or Timeline by its
typed stable reference, such as `variable@id` or `component@id`.
Getter/setter nodes, dispatcher nodes, component-bound events, and Timeline
nodes remain independently identified Nodes in their owning Graph.

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

1. `blueprint.name = component(...)` creates or reads a root SCS component.
2. `parent.child = component(...)` creates or reads a child component.
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
does not require agents to write explicit order fields or a `ChildNodes` array.

Meaningful native `USCS_Node` fields retain their UE names and values, including
`CategoryName`, `AttachToName`, `ParentComponentOrVariableName`,
`ParentComponentOwnerClassName`, `bIsParentComponentNative`, and
`MetaDataArray`. The mapped identity fields, `ChildNodes`, deprecated fields,
and derived `CookedComponentInstancingData` are not repeated. The
`ComponentTemplate` pointer is not returned because its editable properties are
already fields on the Component.

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

Orientation uses the shared summary statement:

```lgl
summary door
```

The Blueprint adapter returns the compact Blueprint object followed by comments
with counts for its owned Variables, Dispatchers, Graphs, Components, and
Timelines. It does not expand those complete collections or return a summary
object, section object, or generic Member item:

```lgl
door = blueprint(asset: bpAsset, id: "blueprint-guid", type: BPTYPE_Normal, ParentClass: "/Script/Engine.Actor")

# variables: 8
# dispatchers: 2
# graphs: 5
# components: 4
# timelines: 1
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

query door
timelines ["text"]
```

Without search text, each operation enumerates its complete owned collection
through cursor pagination while preserving the corresponding UE authored
order. With search text, it performs adapter-owned search over the current
name, type, and relevant native descriptive fields. Collection results contain
compact concrete object identities rather than full Graph bodies, component
templates, Timeline tracks, or schemas.

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
```

The constructor name and argument shape are returned data rather than a
creation vocabulary the agent must memorize. The exact Palette read with
`with schema` describes required parameters and context constraints through
structured comments. A Patch may reuse a previously returned Palette id, but
`add` re-resolves it in the current Blueprint context. This section currently
confirms Variable and Dispatcher entries; other Blueprint lifecycle objects
must follow the same Palette rule after their native creation paths are
reviewed.

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

query door
timeline OpenTimeline
```

The operation supplies the concrete kind, so the adapter never guesses among
same-named kinds. Name lookup returns zero-match or ambiguity diagnostics
rather than falling back to another kind. Names are convenient current
locators, not rename-stable identity.

Stable-id lookup uses the exact returned object kind:

```lgl
query door
find blueprint@blueprint-guid

query door
find variable@variable-guid
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
find variable@variable-guid
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

Timeline exact reads include the native Track array fields. Those fields expose
inline event keys and references to Curve objects; they do not recursively
expand Curve-owned data.

Zero matches return an unknown-object diagnostic. If invalid UE state produces
more than one match, the adapter returns an ambiguity diagnostic rather than
guessing from name or object type.

The normalized JSON representation of Blueprint collection, exact-name, and
typed `find <object>@<id>` operations is intentionally not specified in this
document yet. It must be reviewed before schema work; this text contract does
not silently introduce new normalized `kind` values. The current experiment
also does not implement this query model or `with schema`.

## Patch

Blueprint Patch uses the shared Core lifecycle operations for its concrete
objects. It does not turn UE editor utility names into parallel `invoke`
Operations. This section currently confirms Variable and Dispatcher lifecycle;
Graph, Component, and Timeline lifecycle remain to be reviewed against their
own native UE paths.

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
type-specific metadata, and structurally recompiles the Blueprint.

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
`MoveVariableAfterVariable` and performs the required structural compile.

Deletion uses the shared lifecycle operation:

```lgl
remove variable@variable-id
```

The adapter routes it through `FBlueprintEditorUtils::RemoveMemberVariable`.
Native deletion removes the `FBPVariableDescription`, all getter and setter
Nodes referencing that Variable and their incident Edges, relevant Field
Notify metadata, and structurally recompiles the Blueprint. Explicit `remove`
is the deletion authorization; LGL adds no `force` argument. Preflight must
enumerate these determinable effects, and any failure leaves the entire Patch
unchanged.

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
`DelegateSignatureGraphs`, and structurally recompile the Blueprint. Failure at
any point rolls back both objects and the rest of the Patch.

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
Dispatcher Nodes and dependent Variable references, and structurally recompile
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
cleanup and structural compile. If the backing Variable or Signature Graph is
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

## Adapter Boundary

Pure LGL normalization may:

- preserve concrete Blueprint object bindings and component statement order
- normalize Blueprint query clauses into a structured query object
- normalize confirmed Variable bindings and common Patch operation forms

Pure LGL normalization must not:

- resolve parent classes or interface classes
- decide whether a class can be a Blueprint parent
- validate UE pin types
- validate Variable names or inherited and child Blueprint collisions
- determine Variable edit cascades or affected Blueprint assets
- validate Dispatcher backing-object consistency, availability, or usage Nodes
- determine Dispatcher rename, removal, and dependent-Blueprint effects
- validate function override eligibility
- synthesize inherited override signatures
- validate component attachment legality
- inspect or mutate SCS component templates
- compile Blueprints

The adapter or bridge owns those UE-dependent responsibilities and must use UE
APIs such as `FBlueprintEditorUtils`, `USimpleConstructionScript`, `USCS_Node`,
and `UK2Node_CustomEvent`. Generated Class and CDO access belongs to the Class
Reflection and Class Defaults designs.
