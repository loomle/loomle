# Blueprint Domain

## Scope

The blueprint domain describes Blueprint asset structure outside individual
graph nodes and edges. It covers class contract, implemented interfaces, class
defaults, variables, dispatchers, graphs, SimpleConstructionScript components,
and timelines.

Function, macro, event, delegate-signature, interface-function, and construction
script graphs are all graph objects. Their nodes, pins, and edges remain graph
domain work.

Some graph nodes reference Blueprint-owned variables, dispatchers, components,
or timelines. The graph domain owns those nodes; the blueprint domain owns the
referenced object identities.

The TypeScript experiment still implements the earlier `members` array and
`find members` model. That implementation does not match this design and must
be replaced in a later schema/parser/formatter/adapter phase.

## UE Boundary

`UBlueprint` owns more than graphs. Important UE-owned storage includes:

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
door = blueprint(asset: bpAsset, type: normal, parent: "/Script/Engine.Actor")

door.implements = ["/Script/MyGame.Damageable"]
door.default.Health = 100.0

eventGraph = graph(domain: blueprint, asset: bpAsset, id: "event-graph-guid", name: EventGraph, type: event_graph)
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: delegate_signature)

door.Health = variable(id: "variable-guid", type: float, default: 100.0, category: "Stats", replication: replicated)
door.OnOpened = dispatcher(id: "dispatcher-variable-guid", graph: @signature-graph-guid)

door.Root = component(id: "root-component-guid", class: "/Script/Engine.SceneComponent")
Root.Mesh = component(id: "mesh-component-guid", class: "/Script/Engine.StaticMeshComponent", staticMesh: "/Game/Meshes/SM_Door.SM_Door")

door.OpenTimeline = timeline(id: "timeline-guid")
```

`blueprint.name = ...` is ordinary binding-path syntax. It expresses owner and
current name without creating a shared Member type. Component trees use the
same `parent.child = ...` shape to express hierarchy.

## Blueprint Object

| Object | Syntax | Example |
| --- | --- | --- |
| Blueprint asset | `name = asset(path: "...", type: blueprint)` | `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Blueprint binding | `name = blueprint(asset: ref, type: symbol, metadata...)` | `door = blueprint(asset: bpAsset, type: normal, parent: "/Script/Engine.Actor")` |
| Interface list | `blueprint.implements = [classPath, ...]` | `door.implements = ["/Script/MyGame.Damageable"]` |
| Class default | `blueprint.default.property = value` | `door.default.Health = 100.0` |

`blueprint(...)` identifies one Blueprint asset as a class-like editing target.
It is not a graph and it is not a replacement for `asset(...)`.

Normalized JSON:

```ts
interface Blueprint {
  kind: "blueprint";
  alias: string;
  asset: Ref;
  type: Name;
  parent?: Ref;
  namespace?: string;
  category?: string;
  abstract?: boolean;
  deprecated?: boolean;
  interfaces?: Ref[];
  defaults?: Record<string, Value>;
}
```

`type` maps directly to `EBlueprintType`: `normal`, `const`, `macro_library`,
`interface`, `level_script`, or `function_library`. The asset path is the
Blueprint's exact current identity. UE does not expose a persistent Blueprint
GUID, so LGL does not invent one.

## Class Contract

Class contract fields describe the generated Blueprint class:

```lgl
door = blueprint(
  asset: bpAsset,
  type: normal,
  parent: "/Script/Engine.Actor",
  namespace: "Game.Doors",
  category: "Doors",
  abstract: false,
  deprecated: false
)
```

Canonical text should keep constructor arguments named. Formatters may keep the
binding on one line when it is short.

Implemented interfaces are class contract data:

```lgl
door.implements = ["/Script/MyGame.Damageable", "/Script/MyGame.Interactable"]
```

Class defaults are stored on the generated class CDO, not in graph nodes:

```lgl
door.default.Health = 100.0
door.default.DisplayName = "Door"
```

This existing three-part class-default statement is Blueprint-domain syntax,
not a general MemberRef and not evidence of a Blueprint Member object.

Variable declaration and variable default storage are related but not the same
UE storage path. LGL exposes one `default`; the adapter maps it to
`FBPVariableDescription::DefaultValue` and/or the generated-class CDO as the
current Blueprint state requires.

## Concrete Blueprint Objects

Blueprint reads return concrete objects directly in the ordered LGL document.
There is no `Member`, `BlueprintMember`, `members[]`, or generic member result.
The six concrete object types are Blueprint, Variable, Dispatcher, Graph,
Component, and Timeline.

Every Blueprint-owned concrete object exposes one `id` mapped to its native UE
GUID. Its binding path supplies owner and current name. Names are readable and
searchable but are not cross-query identity.

### Variables

Variables map to UE `FBPVariableDescription` entries and generated class
properties. Only variables authored in `UBlueprint::NewVariables` are returned;
inherited, native, local, generated, and multicast-delegate declarations are
not duplicate Variable objects.

```lgl
door.Health = variable(
  id: "variable-guid",
  type: float,
  default: 100.0,
  category: "Stats",
  replication: replicated,
  exposeOnSpawn: true,
  private: false
)
```

`id` maps to `FBPVariableDescription::VarGuid`. The declaration contains type,
flags, category, metadata, replication, and editor-facing exposure settings.
LGL exposes one default value even though UE may source or stage that value
through both the variable description and the generated-class CDO.

```ts
interface Variable {
  kind: "variable";
  id: string;
  owner: Ref;
  name: string;
  type: Ref | Name;
  default?: Value;
  category?: string;
  replication?: Name;
  exposeOnSpawn?: boolean;
  private?: boolean;
  metadata?: Record<string, Value>;
}
```

### Graphs

Blueprint functions and macros are Graph objects, not additional declaration
objects. Event, function, macro, delegate-signature, interface-function, and
construction-script semantics are expressed by the Graph `type`:

```lgl
openDoor = graph(domain: blueprint, asset: bpAsset, id: "function-graph-guid", name: OpenDoor, type: function_graph)
traceDoor = graph(domain: blueprint, asset: bpAsset, id: "macro-graph-guid", name: TraceDoor, type: macro_graph)
```

`id` maps to `UEdGraph::GraphGuid`. Graph name is readable and searchable but is
not identity. Function signatures, macro tunnels, overrides, custom events, and
all other graph contents remain nodes and pins in the graph domain.

Top-level graphs belong to the Blueprint asset. Nested graphs may additionally
identify their owning graph or node when that relation exists in UE, without
changing their Blueprint-plus-GraphGuid identity.

Implemented interfaces remain a Blueprint relation:

```lgl
door.implements = ["/Script/MyGame.Damageable"]
```

Graphs required by that interface are ordinary Graph objects with an
interface-function type. LGL does not create an Interface object or an
interface-specific ref constructor.

### Dispatchers

An Event Dispatcher is one semantic object backed by a multicast-delegate
`FBPVariableDescription` and a same-owned Delegate Signature Graph:

```lgl
signatureGraph = graph(domain: blueprint, asset: bpAsset, id: "signature-graph-guid", name: OnOpened, type: delegate_signature)
door.OnOpened = dispatcher(id: "dispatcher-variable-guid", graph: @signature-graph-guid)
```

Dispatcher `id` maps to the delegate variable's `VarGuid`. Its `graph` relation
points to the associated Graph by GraphGuid. Signature parameters live in that
Graph and are not copied into the Dispatcher object. Create, rename, and remove
must update both UE objects atomically; a missing or mismatched half is an
inconsistent Blueprint diagnostic, not a guessing opportunity.

```ts
interface Dispatcher {
  kind: "dispatcher";
  id: string;
  owner: Ref;
  name: string;
  graph: IdRef;
  metadata?: Record<string, Value>;
}
```

### Timelines

Timeline is the only new concrete object constructor in this read design. It
maps directly to `UTimelineTemplate`:

```lgl
door.OpenTimeline = timeline(
  id: "timeline-guid",
  length: 1.0,
  loop: false,
  autoplay: false,
  replicated: false,
  ignoreTimeDilation: false
)
```

`id` maps to `UTimelineTemplate::TimelineGuid`. Event, float, vector, and linear
color tracks are ordered Timeline details rather than Blueprint-level objects.
Their text form remains deliberately unspecified until track editing is designed.

```ts
interface Timeline {
  kind: "timeline";
  id: string;
  owner: Ref;
  name: string;
  length?: number;
  loop?: boolean;
  autoplay?: boolean;
  replicated?: boolean;
  ignoreTimeDilation?: boolean;
}
```

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
door.Root = component(id: "root-component-guid", class: "/Script/Engine.SceneComponent")
Root.Mesh = component(id: "mesh-component-guid", class: "/Script/Engine.StaticMeshComponent", staticMesh: "/Game/Meshes/SM_Door.SM_Door")
Root.Trigger = component(id: "trigger-component-guid", class: "/Script/Engine.BoxComponent", boxExtent: [100, 100, 200])
```

Rules:

1. `blueprint.name = component(...)` creates or reads a root SCS component.
2. `parent.child = component(...)` creates or reads a child component.
3. `id` maps to `USCS_Node::VariableGuid`.
4. The child binding name is the current UE component variable name.
5. Parent statements precede their children.
6. Sibling order is the order of child component statements for that parent.
7. Component-template properties are fields on the same Component object; LGL
   does not emit every inherited class default.

Normalized JSON:

```ts
interface Component {
  kind: "component";
  id: string;
  owner: Ref;
  name: string;
  alias: string;
  class: Ref;
  parent?: Ref | null;
  properties: Record<string, Value>;
}
```

Sibling order is derived from statement order for the same parent. LGL text
does not require agents to write explicit order fields.

`UBlueprint::ComponentTemplates` entries used by AddComponent mechanisms are
not SCS Component Tree objects. Native or inherited components are not returned
as if the current Blueprint owned them; an external-parent reference requires a
separate design only when a concrete workflow needs it. LGL does not introduce
a `component_property` object.

Graph nodes may refer to component aliases from this tree:

```lgl
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

That reference is explicit. The graph adapter must not infer a component-bound
event target from nearby lines or from a previous palette query.

## Blueprint-Backed Graph Nodes

Blueprint-owned objects commonly appear as graph nodes. The current patch
language supports both semantic construction and palette fallback:

1. Semantic construction from a concrete Blueprint-owned object.
2. Palette fallback construction from an exact Action Menu entry.

Semantic construction:

```lgl
add health = get(variable: @variable-guid)
add setHealth = set(variable: @variable-guid)
add overlap = event(component: @trigger-component-guid, event: OnComponentBeginOverlap)
```

Palette fallback construction:

```lgl
add health = node(palette: "palette:blueprint:variable:get:Health")
```

Both forms may create the same UE node class. Their mutation syntax is not
redesigned in this read-model phase; it must be reviewed separately before the
existing constructors become part of the confirmed contract.

## Query

Blueprint reads have one orientation form and one exact-object form.

Orientation uses the shared summary statement:

```lgl
summary door
```

The Blueprint adapter returns an ordered, compact directory of the Blueprint
and its owned Variables, Dispatchers, Graphs, Components, and Timelines. It uses
the concrete object text defined above plus comments; it does not return a
summary object, section object, or generic Member item.

Exact reads use the concrete object's stable id:

```lgl
query door
find @variable-guid
```

The adapter resolves the id across the concrete objects owned by that Blueprint
and returns exactly one complete object. The returned constructor identifies
whether it is a Variable, Dispatcher, Graph, Component, or Timeline; the caller
does not repeat the type in the query.

Blueprint exact reads do not accept `where`, `with`, `order by`, or `page`.
Object-specific detail expansions are unnecessary in the first design: an exact
read returns the complete semantic object. Component reads include properties
actually set on the component template rather than all inherited class defaults.

Timeline Track text is still deliberately undefined, so complete Timeline exact
read cannot be implemented until that separate object-detail design is
confirmed. The adapter must report that capability gap rather than omit Tracks
while claiming a complete Timeline.

Zero matches return an unknown-object diagnostic. If invalid UE state produces
more than one match, the adapter returns an ambiguity diagnostic rather than
guessing from name or object type.

The normalized JSON representation of `find @id` is intentionally not specified
in this document yet. It must be reviewed before schema work; this text contract
does not silently introduce a new normalized `kind`.

## Patch

This phase standardizes Blueprint reads only. The existing TypeScript experiment
still exposes member-based patch bindings and `function(...)`, `macro(...)`, and
event creation shortcuts. Those mutation forms are not confirmed by this read
model and must be redesigned separately before this section becomes normative.

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
`UK2Node_CustomEvent`, and generated class CDO property access.
