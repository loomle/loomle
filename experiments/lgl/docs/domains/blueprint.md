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
bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
door = blueprint(
  asset: bpAsset,
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  ParentClass: "/Script/Engine.Actor",
  BlueprintNamespace: "Game.Doors",
  BlueprintCategory: "Doors",
  ImplementedInterfaces: [
    {Interface: "/Script/MyGame.Damageable", Graphs: [@damageable-graph-guid]}
  ]
)

eventGraph = graph(domain: blueprint, asset: bpAsset, id: "event-graph-guid", name: EventGraph, type: event_graph)
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: delegate_signature)
damageableGraph = graph(domain: blueprint, asset: bpAsset, id: "damageable-graph-guid", name: TakeDamage, type: interface_function)

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
| Blueprint asset | `name = asset(path: "...", type: blueprint)` | `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Blueprint binding | `name = blueprint(asset: ref, id: string, type: nativeEnum, nativeFields...)` | `door = blueprint(asset: bpAsset, id: "blueprint-guid", type: BPTYPE_Normal, ParentClass: "/Script/Engine.Actor")` |
| Implemented interfaces | native `ImplementedInterfaces` field | `ImplementedInterfaces: [{Interface: "/Script/MyGame.Damageable", Graphs: [@graph-guid]}]` |

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
    Graphs: [@take-damage-graph-guid]
  },
  {
    Interface: "/Script/MyGame.Interactable",
    Graphs: [@interact-graph-guid, @can-interact-graph-guid]
  }
]
```

`Interface` is the Interface Class Path. `Graphs` contains stable Graph `@id`
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
objects. Event, function, macro, delegate-signature, interface-function, and
construction-script semantics are expressed by the Graph `type`:

```lgl
openDoor = graph(domain: blueprint, asset: bpAsset, id: "function-graph-guid", name: OpenDoor, type: function_graph)
traceDoor = graph(domain: blueprint, asset: bpAsset, id: "macro-graph-guid", name: TraceDoor, type: macro_graph)
```

`id` maps to `UEdGraph::GraphGuid`. Graph name is readable and searchable but is
not identity. `domain`, `asset`, `id`, `name`, and the Graph-role `type` are LGL
common structure. Relevant authored `UEdGraph` fields retain their UE names and
native values, including `Schema`, `bEditable`, `bAllowDeletion`,
`bAllowRenaming`, and `InterfaceGuid`.

A Blueprint Graph read is a compact Graph identity and native description. It
does not include `Nodes`, `Pins`, `Edges`, or `SubGraphs`; Graph body inspection
belongs to the graph domain. Function signatures, macro tunnels, overrides,
custom events, and all other graph contents therefore remain Nodes and Pins.

Top-level graphs belong to the Blueprint asset. Nested graphs may additionally
identify their owning graph or node when that relation exists in UE, without
changing their Blueprint-plus-GraphGuid identity.

Implemented interfaces remain native Blueprint Class Contract data:

```lgl
ImplementedInterfaces: [
  {Interface: "/Script/MyGame.Damageable", Graphs: [@damageable-graph-guid]}
]
```

Graphs required by that interface are ordinary Graph objects with an
interface-function type. LGL does not create an Interface object or an
interface-specific ref constructor.

### Dispatchers

An Event Dispatcher is one semantic object backed by a multicast-delegate
`FBPVariableDescription` and a same-owned Delegate Signature Graph:

```lgl
door.OnOpened = dispatcher(
  id: "dispatcher-variable-guid",
  type: "<multicast-delegate FEdGraphPinType native text>",
  Category: "Events"
)
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: delegate_signature)
```

The Dispatcher binding, `id`, `type`, and remaining declaration fields use the
same `FBPVariableDescription` mapping as a Variable. Its `VarType` identifies a
multicast delegate. LGL does not add a `graph` field: UE associates the
Dispatcher with its same-named Graph in `DelegateSignatureGraphs`.

An exact Dispatcher read places that compact Signature Graph identity directly
after the Dispatcher as required navigation context, without returning the
Graph body. Signature parameters remain Pins in that Graph. Create, rename, and
remove must update both UE objects atomically; a missing or mismatched half is
an inconsistent Blueprint diagnostic, not a guessing opportunity.

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
stable `@id`. Getter/setter nodes, dispatcher nodes, component-bound events, and
Timeline nodes remain independently identified Nodes in their owning Graph.

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

Stable-id lookup remains the kind-independent exact form:

```lgl
query door
find @blueprint-guid

query door
find @variable-guid
```

The adapter resolves the id across the Blueprint itself and its owned concrete
objects, then selects exactly one complete primary object. The returned
constructor identifies whether it is a Blueprint, Variable, Dispatcher, Graph,
Component, or Timeline; the caller does not repeat the type in the query.
Required navigation context may follow that object: a Component includes its
shortest ancestor chain, and a Dispatcher includes its compact same-named
Signature Graph identity. This context does not expand a Graph body or other
related objects.

Blueprint exact name and stable-id reads do not accept `where`, `order by`, or
`page`. They accept the shared object-discovery expansion on a single resolved
object:

```lgl
query door
find @variable-guid
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
`find @id` operations is intentionally not specified in this document yet. It
must be reviewed before schema work; this text contract does not silently
introduce new normalized `kind` values. The current experiment also does not
implement this query model or `with schema`.

## Patch

This phase standardizes Blueprint reads only. The existing TypeScript experiment
still exposes member-based patch bindings and graph creation shortcuts. Those
forms do not match this design and must be replaced by concrete Blueprint
objects and exact Palette Entry creation before this section becomes normative.

Removing the read-side Member abstraction does not implicitly approve or remove
any patch constructor. Mutation design must start from the concrete Blueprint
objects above and the corresponding UE edit paths.

## Adapter Boundary

Pure LGL normalization may:

- preserve concrete Blueprint object bindings and component statement order
- normalize Blueprint query clauses into a structured query object

Pure LGL normalization must not:

- resolve parent classes or interface classes
- decide whether a class can be a Blueprint parent
- validate UE pin types
- validate function override eligibility
- synthesize inherited override signatures
- validate component attachment legality
- inspect or mutate SCS component templates
- compile Blueprints

The adapter or bridge owns those UE-dependent responsibilities and must use UE
APIs such as `FBlueprintEditorUtils`, `USimpleConstructionScript`, `USCS_Node`,
and `UK2Node_CustomEvent`. Generated Class and CDO access belongs to the Class
Reflection and Class Defaults designs.
