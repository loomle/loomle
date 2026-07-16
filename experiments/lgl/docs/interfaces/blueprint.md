# blueprint

Inspect and edit one Blueprint's Class Settings, Variables, Dispatchers,
top-level Graph lifecycle, and owned SCS Components. This interface assumes the
resident LGL Core guide.

## Target

The first Query may resolve by exact Asset Path and returns `BlueprintGuid`:

```lgl
door = blueprint(asset: "/Game/BP_Door.BP_Door")

query door
summary
```

Later exact Queries and every Patch use both fields:

```lgl
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
```

The Path loads the asset and the Guid verifies it. Typed ids below are scoped
to this Blueprint and cannot replace the complete target.

## Query

Every Query starts with `query door` and chooses exactly one primary operation
from this surface:

```lgl
summary
variables ["text"]
dispatchers ["text"]
graphs ["text"]
components ["text"]
variable <name>
dispatcher <name>
graph <name>
component <name>
blueprint@id
variable@id
dispatcher@id
graph@id
component@id
palette entries ["text"]
palette @id
```

`summary` returns the compact Blueprint and counts naming the four collection
operations. Collections return compact identities, preserve UE authored order
by default, and use cursor pagination with a default limit of 50. They support
exact `where` predicates on `name`, `id`, and `type`, plus ordering by those
same keys. Fuzzy matching belongs to the optional primary search text.

Exact-name queries discover the current object and id. Both exact-name and
exact-id reads return complete compact state and may use `with schema`. Exact
reads do not accept `where`, ordering, or pagination.

```lgl
query door
variable@variable-guid
with schema
```

When `query door` selects `graph@id`, schema describes Blueprint-owned Graph
lifecycle. Bind that Graph as its own target and use `lgl.schema("graph")` for
Node, Pin, Edge, flow, and Graph-body operations.

## Palette And Creation

The target exposes one combined Palette of its valid direct creation
capabilities. Inspect an exact entry before creation when parameters or current
constraints are needed:

```lgl
query door
palette entries "Variable"

query door
palette @palette-entry-id
with schema
```

Copy returned constructors into one ordered Patch:

```lgl
patch door [dry run]

door.Health = variable(
  palette: "variable-palette-id",
  type: "<FEdGraphPinType native text>"
)
add door.Health

door.OnOpened = dispatcher(palette: "dispatcher-palette-id")
add door.OnOpened

OpenDoor = graph(palette: "graph-palette-id")
add OpenDoor

Mesh = component(palette: "component-palette-id")
add Mesh to component@root-guid
```

Palette and exact schema determine which constructors and arguments are valid
for the concrete Blueprint subclass. Timeline Nodes come from their target
Graph Palette, not the Blueprint Palette.

## Existing Objects

Class Settings use the target alias. Existing contained objects use typed ids:

```lgl
set door.BlueprintDescription = "Interactive door"
reset door.BlueprintDescription
set door.ParentClass = "/Script/Engine.Actor"

set variable@id.NativeField = value
reset variable@id.NativeField
move variable@id before variable@anchor-id
move dispatcher@id after dispatcher@anchor-id
move graph@id before graph@anchor-id

set component@id.NativeField = value
move component@id to component@parent-id

remove variable@id
remove dispatcher@id
remove graph@id
remove component@id
```

Exact `with schema` is authoritative for writable fields, reset behavior,
lifecycle availability, constraints, and effects.

Schema-discovered compound Operations include:

```lgl
invoke door ImplementInterface(Interface: "<Interface Class Path>")
invoke door RemoveInterface(
  Interface: "<Interface Class Path>",
  bPreserveFunctions: true
)
invoke door ImplementFunction(function: "<Function Path>")

invoke component@id MakeNewSceneRoot()
invoke component@id Duplicate() as copy
```

## Compile And Save

Explicit finalization is a separate terminal Patch:

```lgl
patch door [dry run]
compile
save
```

Valid terminal forms are `compile`, `save`, or `compile` followed by `save`.
They cannot be mixed with bindings or authored source mutations. `compile`
targets the whole Blueprint, never an individual Graph, and returns native
Status and ordered compiler diagnostics. `save` persists only the exact owning
Package.

## Handoffs

- Graph bodies, Dispatcher signatures, and Timeline Nodes use `graph`.
- A `UWidgetBlueprint` target composes this module with `widget`.
- Generated Class Reflection and effective Defaults use `class` after compile.
- Interface implementation is a Blueprint Operation, not a created Interface
  object or Palette Entry.
