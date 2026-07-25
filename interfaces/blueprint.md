# blueprint

Inspect and edit one Blueprint's Class Settings, Variables, Dispatchers,
top-level Graph lifecycle, and owned SCS Components.

## Target

The first Query may discover `BlueprintGuid` from an exact Asset Path:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door"
}

query door
summary
```

Canonical exact Queries and every Patch use both fields:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The Path loads; `id` verifies persisted `BlueprintGuid`.

## Identity

References are relative to the exact Blueprint Target:

| Object | StableRef |
| --- | --- |
| Variable, Dispatcher, top-level Graph, SCS Component, referenceable Node | `@Guid` |
| Function-local Variable | `@TopLevelFunctionGraphGuid/VarGuid` |

All one-segment categories share one identity environment. A collision across
categories is an identity conflict; a semantic tag does not disambiguate it.

## Query

```sal
target
summary
variables ["text"]
dispatchers ["text"]
graphs ["text"]
components ["text"]
variable <name>
dispatcher <name>
graph <name>
component <name>
@identity
references to <exact-subject> [in project]
palette entries ["text"]
palette @id
```

Collections preserve authored order and use cursor pagination. Exact `name`
operations discover current identity; exact StableRef reads may use
`with schema`.

```sal
query door
@variable-guid
with schema
```

`references` searches this Blueprint by default. `in project` uses the bounded
project index and returns independently locatable pages.

## Objects And Creation

Results use ordinary brace objects. A Domain may recommend an erasable tag for
presentation, but tags do not choose an object kind:

```sal
door.Health = {
  id: "33333333-3333-3333-3333-333333333333",
  type: "<FEdGraphPinType native text>",
  Category: "Stats"
}
```

Palette returns copyable object fields:

```sal
query door
palette entries "Variable"
```

```sal
patch door
door.Health = {
  palette: "variable-palette-id",
  type: "<FEdGraphPinType native text>"
}
add door.Health

door.OnOpened = { palette: "dispatcher-palette-id" }
add door.OnOpened

OpenDoor = { palette: "graph-palette-id" }
add OpenDoor

Mesh = { palette: "component-palette-id" }
add Mesh to @root-component-guid
```

The active Blueprint Domain, Palette identity, binding path, and `add`
destination provide creation meaning. No object tag or native Class selects the
Domain or lifecycle.

## Existing Objects

```sal
set door.BlueprintDescription = "Interactive door"
reset door.BlueprintDescription
set door.ParentClass = "/Script/Engine.Actor"

set @variable-guid.Category = Stats
move @variable-guid before @anchor-variable-guid
remove @variable-guid

set @component-guid.StaticMesh = "/Game/Meshes/SM_Door.SM_Door"
move @component-guid to @parent-component-guid
remove @component-guid
```

Exact schema is authoritative for field access, reset behavior, lifecycle
availability, constraints, operations, and native effects.

Compound operations include:

```sal
invoke door ImplementInterface(Interface: "<Interface Class Path>")
invoke door RemoveInterface(
  Interface: "<Interface Class Path>",
  bPreserveFunctions: true
)
invoke door ImplementFunction(function: "<Function Path>")

invoke @component-guid MakeNewSceneRoot()
invoke @component-guid Duplicate() as copy
```

## Compile And Save

Finalization is a separate terminal Patch:

```sal
patch door [dry run]
compile
save
```

Valid forms are `compile`, `save`, or `compile` followed by `save`. They cannot
be mixed with authored edits. Compile targets the whole Blueprint and returns
native Status plus ordered compiler diagnostics.

## Handoffs

- Graph bodies and signatures use an independent Graph Target.
- A `UWidgetBlueprint` WidgetTree uses an independent Widget Target.
- Generated Class Reflection and effective Defaults use a Class Target.

Results retain each destination in `relatedTargets` and name it with an
explicit handoff. Blueprint never composes another Domain implicitly.
