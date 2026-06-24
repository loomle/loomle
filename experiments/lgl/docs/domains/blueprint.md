# Blueprint Domain

## Scope

The blueprint domain describes Blueprint asset structure outside individual
graph nodes and edges. It covers class contract, implemented interfaces, class
defaults, member declarations, custom events, and SimpleConstructionScript
components.

Graph bodies still belong to the graph domain. A Blueprint function declaration
may create or reference a function graph, but editing the function body's nodes
and pins remains graph work.

Some graph nodes reference Blueprint members or components. Variable getter/setter
nodes, dispatcher nodes, and component-bound event nodes are graph nodes, but
their targets are declared by this domain. The graph domain owns the node
creation syntax; the blueprint domain owns the member and component identities
those nodes reference.

The TypeScript experiment implements `blueprint(...)` bindings, `query bp`,
`find members`, `find components`, canonical Blueprint result text, schema
validation, formatter roundtrip, patch normalization, and an in-memory
Blueprint adapter. UE-backed class/member/component integration belongs to the
UE-backed adapter.

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

LGL should preserve those UE boundaries. It should not flatten Blueprint into a
single graph model.

## Basic Form

Blueprint object text is a statement list:

```lgl
bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
door = blueprint(asset: bpAsset, parent: "/Script/Engine.Actor", namespace: "Game.Doors")

door.implements = ["/Script/MyGame.Damageable"]
door.default.Health = 100.0

door.Health = variable(type: float, default: 100.0, category: "Stats", replication: replicated)
door.OpenDoor = function(inputs: [speed: float], outputs: [success: bool], pure: false)
door.OnOpened = dispatcher(inputs: [instigator: Actor])
door.OnDoorOpened = event(inputs: [instigator: Actor], replication: server, reliable: true)

door.Root = component(class: "/Script/Engine.SceneComponent")
Root.Mesh = component(class: "/Script/Engine.StaticMeshComponent", staticMesh: "/Game/Meshes/SM_Door.SM_Door")
Root.Trigger = component(class: "/Script/Engine.BoxComponent", boxExtent: [100, 100, 200])
```

Member declarations use `blueprint.member = ...`. Component tree declarations
use the same `parent.child = ...` sugar as widget trees.

## Blueprint Object

| Object | Syntax | Example |
| --- | --- | --- |
| Blueprint asset | `name = asset(path: "...", type: blueprint)` | `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Blueprint binding | `name = blueprint(asset: ref, metadata...)` | `door = blueprint(asset: bpAsset, parent: "/Script/Engine.Actor")` |
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
  parent?: Ref;
  namespace?: string;
  category?: string;
  abstract?: boolean;
  deprecated?: boolean;
  interfaces?: Ref[];
  defaults?: Record<string, Value>;
  members?: Member[];
  components?: Component[];
}
```

## Class Contract

Class contract fields describe the generated Blueprint class:

```lgl
door = blueprint(
  asset: bpAsset,
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

Variable declaration and variable default storage are related but not the same
operation. The adapter must write them through the UE paths that actually own
the data.

## Members

Blueprint member declarations use member bindings:

| Member | Syntax | Example |
| --- | --- | --- |
| Variable | `bp.name = variable(args...)` | `door.Health = variable(type: float, default: 100.0)` |
| Function | `bp.name = function(args...)` | `door.OpenDoor = function(inputs: [speed: float], outputs: [success: bool])` |
| Macro | `bp.name = macro(args...)` | `door.TraceDoor = macro(inputs: [start: Vector], outputs: [hit: bool])` |
| Dispatcher | `bp.name = dispatcher(args...)` | `door.OnOpened = dispatcher(inputs: [instigator: Actor])` |
| Custom event | `bp.name = event(args...)` | `door.OnDoorOpened = event(inputs: [instigator: Actor])` |

Member aliases are the UE member names. A member may also carry a stable UE id
when UE exposes one, such as `guid` for variables or `graph` for function-like
members.

Normalized JSON:

```ts
type Member =
  | Variable
  | Function
  | Macro
  | Dispatcher
  | Event;
```

### Variables

Variables map to UE `FBPVariableDescription` entries and generated class
properties:

```lgl
door.Health = variable(
  type: float,
  default: 100.0,
  category: "Stats",
  replication: replicated,
  exposeOnSpawn: true,
  private: false
)
```

The declaration contains type, flags, category, metadata, replication, and
editor-facing exposure settings. Runtime default overrides may also appear as
`door.default.Health` when the object text is describing CDO state.

Normalized JSON:

```ts
interface Variable {
  kind: "variable";
  name: string;
  type: Ref | Name;
  default?: Value;
  category?: string;
  replication?: Name;
  exposeOnSpawn?: boolean;
  private?: boolean;
  metadata?: Record<string, Value>;
  guid?: string;
}
```

### Functions And Macros

Functions and macros are declarations plus owned graphs:

```lgl
door.OpenDoor = function(inputs: [speed: float], outputs: [success: bool], pure: false, const: false)
door.TraceDoor = macro(inputs: [start: Vector], outputs: [hit: bool])
```

Signature editing belongs to the blueprint domain. Body editing belongs to the
graph domain:

```lgl
g = graph(domain: blueprint, asset: bpAsset, graph: OpenDoor)
```

Inherited override functions are not the same as user-created functions:

```lgl
door.GetBodyMesh = function(override: true, owner: "/Script/Oasium.OasiumAvatarBase")
```

The adapter must resolve overrides through UE's override lookup path rather
than creating a same-named user function graph.

Normalized JSON:

```ts
interface Function {
  kind: "function";
  name: string;
  inputs?: Parameter[];
  outputs?: Parameter[];
  pure?: boolean;
  const?: boolean;
  override?: boolean;
  owner?: Ref;
  graph?: Ref;
  metadata?: Record<string, Value>;
}

interface Macro {
  kind: "macro";
  name: string;
  inputs?: Parameter[];
  outputs?: Parameter[];
  graph?: Ref;
  metadata?: Record<string, Value>;
}

interface Parameter {
  name: string;
  type: Ref | Name;
  default?: Value;
}
```

### Dispatchers

Dispatchers are multicast delegate variables plus delegate signature graphs:

```lgl
door.OnOpened = dispatcher(inputs: [instigator: Actor])
```

LGL should expose them as dispatchers, not as ordinary variables, even though UE
stores part of the state through member-variable machinery.

Normalized JSON:

```ts
interface Dispatcher {
  kind: "dispatcher";
  name: string;
  inputs?: Parameter[];
  graph?: Ref;
  metadata?: Record<string, Value>;
}
```

### Custom Events

Custom events are `UK2Node_CustomEvent` nodes in an ubergraph page, but their
signature and replication contract are Blueprint member data:

```lgl
door.OnDoorOpened = event(inputs: [instigator: Actor], replication: server, reliable: true)
```

Custom events support inputs, not return values. Graph placement and wiring are
graph concerns.

Normalized JSON:

```ts
interface Event {
  kind: "event";
  name: string;
  inputs?: Parameter[];
  replication?: Name;
  reliable?: boolean;
  graph?: Ref;
  metadata?: Record<string, Value>;
}
```

Component-bound events are different from custom events. They are graph nodes
bound to a component property and a multicast delegate property:

```lgl
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

The `Trigger` component is declared by the blueprint domain, but the `add`
operation belongs to the graph domain because the result is a graph node.
The adapter must resolve `Trigger` to a Blueprint component property and
`OnComponentBeginOverlap` to a multicast delegate property on that component
class.

## Component Tree

Blueprint components map to UE `USimpleConstructionScript` and `USCS_Node`.
They are not graph nodes.

Component tree object text should use member-binding tree sugar:

```lgl
door.Root = component(class: "/Script/Engine.SceneComponent")
Root.Mesh = component(class: "/Script/Engine.StaticMeshComponent", staticMesh: "/Game/Meshes/SM_Door.SM_Door")
Root.Trigger = component(class: "/Script/Engine.BoxComponent", boxExtent: [100, 100, 200])
```

Rules:

1. `blueprint.name = component(...)` creates or reads a root SCS component.
2. `parent.child = component(...)` creates or reads a child component.
3. The child alias is the UE component variable name.
4. Sibling order is the order of child component lines for that parent.
5. Canonical text normalizes tree sugar to explicit `parent`; sibling order
   remains the order of component statements in text.

Canonical text:

```lgl
Root = component(owner: door, class: "/Script/Engine.SceneComponent", parent: null)
Mesh = component(owner: door, class: "/Script/Engine.StaticMeshComponent", parent: Root, staticMesh: "/Game/Meshes/SM_Door.SM_Door")
Trigger = component(owner: door, class: "/Script/Engine.BoxComponent", parent: Root, boxExtent: [100, 100, 200])
```

Normalized JSON:

```ts
interface Component {
  kind: "component";
  alias: string;
  class: Ref;
  parent?: string | null;
  properties: Record<string, Value>;
}
```

Sibling order is derived from statement order for the same parent. LGL text
does not require agents to write explicit order fields.

Graph nodes may refer to component aliases from this tree:

```lgl
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

That reference is explicit. The graph adapter must not infer a component-bound
event target from nearby lines or from a previous palette query.

## Member-Backed Graph Nodes

Blueprint members commonly appear as graph nodes. LGL supports both editor-like
paths:

1. Semantic construction from the member or component.
2. Palette fallback construction from an exact Action Menu entry.

Semantic construction:

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

Palette fallback construction:

```lgl
add health = node(palette: "palette:blueprint:variable:get:Health")
```

Both forms may create the same UE node class. The semantic form is preferred
when the intent is a stable UE concept such as variable access or
component-bound events. Palette remains available when the exact UE Action Menu
choice matters or when no stable LGL constructor exists yet.

## Query

Blueprint queries use the shared query shape:

```lgl
query door
find members
where kind = variable or kind = function
with metadata, defaults
order by name asc
```

Supported `find` forms:

- `find class`
- `find members ["text"]`
- `find components ["text"]`

Exact member and component lookup use structured filters:

```lgl
find members
where name = Health

find components
where name = Trigger
```

Default result shapes:

- `find class`: Blueprint binding, parent, settings summary, interfaces, and
  explicit default overrides.
- `find members`: member identity, kind, type/signature summary, and graph name
  where relevant.
- `find components`: component identity, class, parent, and order.

`with metadata` expands metadata maps. `with defaults` expands default values.
`with properties` expands component template properties. `with graphs` expands
graph references for function-like members.

Supported `where` fields:

- `kind`
- `name`
- `type`
- `class`
- `parent`
- `text`

Supported `order by` keys:

- `name`
- `kind`
- `order`

Normalized JSON:

```ts
type Find =
  | FindClass
  | FindMembers
  | FindComponents;

interface FindClass {
  kind: "class";
}

interface FindMembers {
  kind: "members";
  text?: string;
}

interface FindComponents {
  kind: "components";
  text?: string;
}
```

Blueprint query text uses the shared `Query` envelope with `target.domain =
"blueprint"` and `find = Find`. `where`, `with`, `orderBy`, and `page` use the
shared query model from the language core. The blueprint domain validates
allowed fields, expansions, sort keys, and pagination defaults.

## Patch

Blueprint patch text is a statement list:

```lgl
patch door dry run

set door.parent = "/Script/Engine.Character"
add door.Health = variable(type: float, default: 100.0, category: "Stats")
set door.Health.replication = repNotify
rename door.Health to MaxHealth

add door.OpenDoor = function(inputs: [speed: float], outputs: [success: bool])
add door.GetBodyMesh = function(override: true, owner: "/Script/Oasium.OasiumAvatarBase")

add door.Root = component(class: "/Script/Engine.SceneComponent")
add Root.Trigger = component(class: "/Script/Engine.BoxComponent", boxExtent: [100, 100, 200])
move Trigger after Mesh
remove door.TraceDoor
```

Patch operation names should stay small:

| Operation | Syntax | Example |
| --- | --- | --- |
| Set class/member field | `set target = value` | `set door.Health.replication = repNotify` |
| Add member/component | `add binding` | `add door.Health = variable(type: float)` |
| Rename member/component | `rename old to new` | `rename door.Health to MaxHealth` |
| Move component | `move name before/after target` | `move Trigger after Mesh` |
| Remove member/component | `remove target` | `remove door.TraceDoor` |

UE-specific sub-operations remain encoded by the object constructor or target:
`function(override: true)` is a different semantic path from
`function(...)`.

`add binding` uses the shared patch sugar from the language core. Its canonical
form is a member or component binding followed by `add target`.

Normalized JSON:

```ts
type PatchOp =
  | Set
  | Add
  | Remove
  | Rename
  | Move;

interface Set {
  kind: "set";
  target: FieldPath;
  value: Expr;
}

interface Add {
  kind: "add";
  target: FieldPath;
}

interface Remove {
  kind: "remove";
  target: FieldPath;
}
```

Blueprint patch text uses the shared `Patch` envelope with `target.domain =
"blueprint"` and `ops = PatchOp[]`.

The TypeScript LGL experiment implements `add`, `set`, `remove`, `rename`, and
component `move` in the in-memory adapter. The UE-backed adapter must route the
same operations through UE-owned Blueprint, member, and component edit paths.

The adapter determines whether an operation target is a class field, member, or
component, then routes through the corresponding UE-owned path.

## Normalized JSON

Blueprint normalized JSON is defined beside each feature above. The summary
below shows the top-level blueprint-domain payloads:

```ts
// Blueprint object text
Blueprint

// Blueprint query and patch text
Query with target.domain = "blueprint" and find = Find
Patch with target.domain = "blueprint" and ops = PatchOp[]
```

Text is for agents. Normalized JSON is for schema validation, RPC, generated
types, and bridge adapters.

## Adapter Boundary

Pure LGL normalization may:

- convert component tree sugar into explicit `parent` while preserving
  component statement order
- normalize member bindings into typed member objects
- normalize Blueprint query clauses into a structured query object
- split patch text into class, member, and component operations

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
