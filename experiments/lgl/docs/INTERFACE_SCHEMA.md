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
- `lgl.schema()` returns this guide and the active domain list.
- `lgl.schema("graph")` returns one domain's query and patch interface.

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
  id: "6C96BAE143A7C89D15E532A9E98090D1",
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
  id: "6C96BAE143A7C89D15E532A9E98090D1"
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

## Graph Summary and Execution Flow

Start from a Graph binding returned by a Blueprint query:

```lgl
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "6C96BAE143A7C89D15E532A9E98090D1"
)
eventGraph = graph(
  asset: door,
  id: "36D2459943EBE9B98D4A46A1F31A67E2"
)

query eventGraph
summary
```

Possible result:

```lgl
beginPlay = node(
  graph: eventGraph,
  id: "DE2A1BF846D7C21FD84730B41564646A",
  type: "/Script/BlueprintGraph.K2Node_Event"
)

# Event BeginPlay
# nodes: 7
# pins: 18
# edges: 6
```

Continue from the returned entry Node:

```lgl
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "6C96BAE143A7C89D15E532A9E98090D1"
)
eventGraph = graph(
  asset: door,
  id: "36D2459943EBE9B98D4A46A1F31A67E2"
)

query eventGraph
exec flow from node@DE2A1BF846D7C21FD84730B41564646A depth 1
```

Possible result:

```lgl
beginPlay = node(
  graph: eventGraph,
  id: "DE2A1BF846D7C21FD84730B41564646A",
  type: "/Script/BlueprintGraph.K2Node_Event",
  EventReference: "<native FMemberReference text>"
)
# Event BeginPlay

beginPlay.OutputDelegate = pin(
  id: "612F74D44422E65C6DDDF38EFC4E723C",
  type: "<native FEdGraphPinType text>",
  direction: out
)
beginPlay.then = pin(
  id: "60F764A24E36687B1DE131A79F167474",
  type: "<native FEdGraphPinType text>",
  direction: out
)

sequence = node(
  graph: eventGraph,
  id: "C5014FC84D4DC2B696CA4094020A1E39",
  type: "/Script/BlueprintGraph.K2Node_ExecutionSequence"
)
# Sequence

sequence.execute = pin(
  id: "63AE2D3946AEAD03793D45B847EE7C88",
  type: "<native FEdGraphPinType text>",
  direction: in
)
sequence.then_0 = pin(
  id: "DB8346BE4ECA6D90D758F5B91526771B",
  type: "<native FEdGraphPinType text>",
  direction: out
)

beginPlay.then -> sequence.execute
```

Angle-bracketed values shorten long native UE text only in this guide. Real
results contain the complete native text.

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
and parentheses. `lgl.schema("<domain>")` lists the operations, fields,
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
`wrap`, or Blueprint `compile` come from `lgl.schema("<domain>")`.

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

```text
lgl.schema("<domain>")
  -> summary
  -> collection search
  -> exact object
  -> with schema
  -> Palette
  -> dry run
  -> Patch
```

`lgl.schema("<domain>")` explains one domain's targets, queries, patches, and
handoffs to related domains. It does not enumerate runtime objects or Palette
Entries.

An exact object or exact Palette Entry may request its usable fields and
operations:

```lgl
query eventGraph
node@DE2A1BF846D7C21FD84730B41564646A
with schema
```

The result remains ordinary Object Text followed by a schema comment. Summary,
collections, and ambiguous Palette searches do not accept `with schema`.

`lgl.schema()` ends with the currently active domain list. Core domains include
`asset`, `blueprint`, `class`, `graph`, and `widget` when their adapters are
available.
