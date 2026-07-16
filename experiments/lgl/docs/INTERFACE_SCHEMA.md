# LGL

LGL is Loomle's agent-facing, line-oriented text language for reading and
modifying Unreal Engine objects.

LGL makes UE objects and editor operations readable without replacing UE's
object model. UE field names, enum values, Class Paths, property types, and
native values remain unchanged. Existing objects use native Paths or typed
stable references inside their owner scopes. Objects created directly through
`add` use aliases and constructors returned by Palette.

Each Query or Patch is one self-contained LGL Text. Both return ordered LGL
Object Text. Result Text may reuse aliases from its request; returned objects
can supply the exact locator fields needed by later self-contained requests.

## Calls

- `lgl.query(text)` executes one Query Text.
- `lgl.patch(text)` executes one ordered Patch Text.
- `lgl.schema()` returns only the active interface-module index.
- `lgl.schema("graph")` returns one static query and patch interface.

## Blueprint Query

```lgl
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

Possible result:

```lgl
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  Status: BS_UpToDate,
  ParentClass: "/Script/Engine.Actor"
)

# variables: 3
# dispatchers: 1
# graphs: 4
# components: 2
```

## Blueprint Patch

Use `dry run` to resolve, validate, and plan without changing UE state:

```lgl
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door dry run
set door.BlueprintDescription = "Interactive door"
```

Possible result:

```lgl
# dry run: valid
# would set door.BlueprintDescription = "Interactive door"
```

```text
dryRun = true
valid = true
applied = false
```

## Graph Execution Flow

Start from a Graph identity returned by Blueprint `summary` or `graph <name>`:

```lgl
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
eventGraph = graph(
  asset: door,
  id: "graph-guid"
)

query eventGraph
exec flow from node@begin-node-guid depth 1
```

Possible result:

```lgl
beginPlay = node(
  graph: eventGraph,
  id: "begin-node-guid",
  type: "/Script/BlueprintGraph.K2Node_Event"
)
# Event BeginPlay

beginPlay.then = pin(
  id: "then-pin-guid",
  type: "<native FEdGraphPinType text>",
  direction: out
)

sequence = node(
  graph: eventGraph,
  id: "sequence-node-guid",
  type: "/Script/BlueprintGraph.K2Node_ExecutionSequence"
)
# Sequence

sequence.execute = pin(
  id: "execute-pin-guid",
  type: "<native FEdGraphPinType text>",
  direction: in
)

beginPlay.then -> sequence.execute
```

Angle-bracketed placeholders stand for complete native UE text in real results.

## Object Text

```lgl
alias = constructor(namedArgument: value)
owner.name = constructor(namedArgument: value)
alias = constructor(RelatedObject: object@stable-id)
sourcePin -> targetPin
```

- `id` and `type` are common LGL fields; other fields keep their UE names.
- Existing objects with native stable ids use typed references such as
  `node@id`, `pin@id`, and `graph@id`. Bare `@id` is invalid.
- Typed ids are scoped selectors, not complete request targets. A request first
  binds the full owner locator chain, then uses the typed id inside that scope.
- Objects without native stable ids keep their exact UE Path or an exact name
  inside an already resolved owner.
- Local aliases and member paths are readable handles inside one LGL Text.
- Values may be null, booleans, numbers, strings, native UE names, arrays,
  inline objects, typed references, or constructor calls.
- An Edge describes state. A Patch requires an explicit operation such as
  `connect` or `disconnect`.

Comments carry titles, counts, schema guidance, navigation, and diagnostics:

```lgl
# single-line comment

###
multi-line comment
###
```

Long statements may wrap inside `()`, `[]`, or `{}`. Wrapped and single-line
forms are equivalent.

## Query Text

```lgl
query <target>
<primary operation>
[where <condition>]
[with <detail>, ...]
[order by <field> [asc|desc], ...]
[page limit <count>]
[page after "<cursor>"]
```

Every Query has exactly one primary operation:

```lgl
summary
<objects> ["text"]
<object> <name>
<object>@<id>
palette entries ["text"]
palette @<id>
```

Plural operations enumerate or search. Singular operations resolve a current
name. Typed references resolve stable identity. Domains may add operations such
as `context`, `exec flow`, and `data flow`.

Conditions support `=`, `!=`, `~=`, `>`, `>=`, `<`, `<=`, `not`, `and`, `or`,
and parentheses. `lgl.schema("<module>")` lists the operations, fields,
expansions, ordering, pagination, depth, and clauses supported by that domain.

## Patch Text

```lgl
patch <target> [dry run]
<binding or operation>
<binding or operation>
```

Core operations:

```lgl
add <binding>
remove <object>
set <object>.<field> = <value>
reset <object>.<field>
move <domain-defined operands>
invoke <object> <Operation>(namedArguments) [as <alias>]
save
```

Patch statements execute in written order. The complete Patch is resolved and
validated before mutation. Domain operations such as Graph `connect`, Widget
`wrap`, or Blueprint `compile` come from `lgl.schema("<module>")`.

Every object created directly through `add` starts from Palette:

```lgl
query <target>
palette entries "search text"
```

Copy the returned constructor into Patch Text:

```lgl
patch eventGraph
print = node(palette: "P_PrintString")
add print
```

Do not guess constructor names, UE Classes, Pins, fields, Palette ids, or
operation parameters.

## Schema Discovery

Schema discovery has three layers:

1. This resident guide provides the minimum LGL mental model.
2. `lgl.schema()` lists the interface modules active in the current server.
3. `lgl.schema("<module>")` returns one compact static interface card.

A static card explains locators, queries, Object Text, Palette, Patch, and
handoffs. It does not load UE objects or inspect one concrete instance.

Dynamic discovery uses an exact object or exact Palette Entry:

```lgl
query eventGraph
graph@graph-guid
with schema
```

The same exact-object rule applies to Nodes, Pins, Widgets, Properties, and
Palette Entries through each module's singular syntax. The result remains
ordinary Object Text followed by a schema comment containing the fields,
constraints, and Operations available in that UE context. Summary, collections,
and ambiguous Palette searches do not accept `with schema`.

Diagnostics should close the same discovery loop:

- unknown syntax points to `lgl.schema("<module>")`;
- unknown fields or Operations point to exact `with schema`;
- stale ids point to the relevant summary, collection, or tree;
- unavailable capabilities give a reason and copyable next query when possible.

The initial interface modules are `asset`, `blueprint`, `class`, `graph`, and
`widget` when their adapters are active. Module names organize documentation;
they are not target-routing fields.
