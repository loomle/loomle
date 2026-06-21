# Graph Domain

## Scope

The graph domain describes UE graph-shaped assets in LGL. It covers graph
identity, node text, pin text, edge/path text, graph queries, graph patches,
shortcut node creation, and palette fallback creation.

Implementation migration notes live in
[`../notes/graph-migration.md`](../notes/graph-migration.md). This document
describes the target domain shape.

## Basic Form

Graph object text is a statement list:

```lgl
bp = asset(path: "/Game/BP_LGLExample.BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)

begin = node(graph: g, type: EventBeginPlay, id: "A001")
print = node(graph: g, type: PrintString, id: "A003", InString: "Ready")
print.InString = pin(type: string, direction: in, value: "Ready")

begin.Then -> print.Exec
```

Returned graph text should be standalone and copyable by default. Query
results repeat required asset and graph bindings unless a future option
explicitly requests contextless output.

## Graph Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Asset binding | `name = asset(path: "...", type: symbol)` | `bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Graph binding | `name = graph(domain: symbol, asset: ref, graph: name)` | `g = graph(domain: blueprint, asset: bp, graph: EventGraph)` |
| Node object text | `name = node(graph: ref, type: symbol, id: string, fields...)` | `delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)` |
| Pin object text | `node.pin = pin(type: symbol, direction: in/out, metadata...)` | `delay.Duration = pin(type: float, direction: in, value: 1.0)` |
| Edge sugar | `pin -> pin` | `begin.Then -> print.Exec` |
| Edge canonical | `edge(pin, pin)` | `edge(begin.Then, print.Exec)` |

Graph ownership is explicit. Nodes use `graph: g`; ownership is not inferred
from source position.

## Graph Identity

Canonical graph identity uses an asset binding plus a graph binding:

```lgl
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
```

The normalized target keeps the existing public names:

```ts
interface Target {
  domain: string;
  asset: string;
  graph: GraphRef;
}

type GraphRef =
  | { kind: "name"; name: string }
  | { kind: "id"; id: string };
```

Graph refs may be names or stable ids:

```lgl
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
gById = graph(domain: blueprint, asset: bp, graph: id(id: "graph-id"))
```

`query g` and `patch g` normalize by resolving the local graph binding into
this target object. The bridge receives the expanded target, not the local
alias.

## Nodes

Existing graph nodes use `type`:

```lgl
begin = node(graph: g, type: EventBeginPlay, id: "A001")
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0, at: [320, 0], size: [200, 100])
print = node(graph: g, type: PrintString, id: "A003", InString: "Ready")
```

Normalized JSON:

```ts
interface Node {
  alias: string;
  id?: string;
  type: string;
  fields: Record<string, Expr>;
  at?: Point;
  size?: Point;
}
```

`at` and `size` are graph editor metadata. LGL does not introduce a separate
layout object.

## Pins

Pin object text uses member bindings and named metadata:

```lgl
delay.Exec = pin(type: exec, direction: in)
delay.Duration = pin(type: float, direction: in, value: 1.0, anchor: [320, 72])
delay.Completed = pin(type: exec, direction: out, anchor: [500, 24])
```

Normalized JSON:

```ts
interface Pin {
  node: string;
  name: string;
  type: string;
  direction: "in" | "out";
  value?: Expr;
  anchor?: Point;
}
```

Avoid positional pin metadata:

```lgl
delay.Duration = pin(float, in, 1.0)
```

Pin metadata has enough optional fields that named arguments are clearer and
safer.

## Edges

Graph query results should prefer readable path sugar:

```lgl
begin.Then -> delay.Exec/Completed -> print.Exec
```

The sugar expands to canonical edge text:

```lgl
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Normalized JSON:

```ts
interface Edge {
  from: PinRef;
  to: PinRef;
}

interface PinRef {
  node: string;
  pin: string;
}
```

Implicit node chains are invalid:

```lgl
begin -> delay -> print
```

Edges always connect pins, not nodes.

## Query

Graph query is a statement list:

```lgl
query g
find nodes "Print"
where type = PrintString and not id = "A001"
with pins, defaults
order by name asc
page limit 50
```

Query syntax:

```lgl
query <graph>
find nodes ["text"]
find path from|to <pin>
find palette entry ["text"] [from|to <pin>]
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

Graph query has no `select` clause. The default result includes graph context
and matched graph objects. `with` expands additional data such as pins and
defaults.

`find palette entry` discovers creation entries for later patch use. The
Palette section defines shortcut constructor results, fallback palette results,
and pin/default expansion rules.

For search-oriented find forms, the quoted text after `find` is the primary
search text. Use `where` for structured filters:

```lgl
find nodes "Print"
where type = PrintString
```

Exact node lookup also uses `find nodes` with a structured filter:

```lgl
find nodes
where name = branch
with pins, defaults

find nodes
where id = "A001"
with pins, defaults
```

Supported first-pass `where` fields for `find nodes`:

- `type`
- `name`
- `id`
- `comment`

Supported first-pass `where` fields for `find palette entry`:

- `component`
- `contextSensitive`

Pin context for `find path` and `find palette entry` uses find-form local
arguments:

```lgl
find path from begin.Then
find path to branch.Condition
find palette entry "Branch" from begin.Then
find palette entry "Less Equal" to branch.Condition
```

`from pin` follows graph edge direction away from the pin. `to pin` follows
graph edge direction toward the pin. Do not express pin context as
`where pin = ...`.

Supported first-pass `order by` keys:

- `name`
- `type`
- `id`

Query text uses one clause per line. Keep the full condition expression on the
`where` line.

Normalized JSON:

```ts
type GraphQuery = Query<GraphFind>;

type GraphFind =
  | FindNodes
  | FindPath
  | FindPaletteEntry;

interface FindNodes {
  kind: "nodes";
  text?: string;
}

interface FindPath {
  kind: "path";
  direction: "from" | "to";
  pin: PinRef;
}

interface FindPaletteEntry {
  kind: "palette_entry";
  text?: string;
  pinContext?: {
    direction: "from" | "to";
    pin: PinRef;
  };
}
```

`where`, `with`, `orderBy`, and `page` use the shared query model from the
language core. The graph domain validates allowed fields, expansions, sort
keys, and whether a pin context is legal for the selected find form.

## Patch

Graph patch is a statement list:

```lgl
patch g dry run

delay = delay(duration: 1.0)
health = get(variable: Health)
overlap = event(component: Trigger, event: OnComponentBeginOverlap)

add delay
add health
add overlap
add delay begin.Then -> delay.Exec
insert begin.Then -> delay.Exec/Completed -> print.Exec
set print.InString = "Game Started"
move print to (640, 0)
```

Patch operations:

| Operation | Sugar | Canonical |
| --- | --- | --- |
| Set field | `set target = value` | same |
| Add binding | `add name` | same |
| Add binding inline | `add name = node(...)` | `name = node(...)` then `add name` |
| Add and connect | `add name pin -> pin` | `add(name, connect: edge)` |
| Connect pins | `connect pin -> pin` | `connect(pin, pin)` |
| Disconnect edge | `disconnect pin -> pin` | `disconnect(pin, pin)` |
| Disconnect pin | `disconnect pin` | same |
| Insert node | `insert pin -> node.input/output -> pin` | `insert(node, from: pin, to: pin, input: pin, output: pin)` |
| Remove binding | `remove name` | same |
| Move node | `move name to (x, y)` | same |
| Reconstruct node | `reconstruct name preserve links` | same |

Palette bindings used in a patch should come from `find palette entry`; see
Palette for creation-entry discovery and pin/default details.

`add name = node(...)` uses the shared patch sugar from the language core. Its
canonical form is a graph node binding followed by `add name`.

Normalized JSON:

```ts
type GraphPatch = Patch<GraphPatchOp>;

type GraphPatchOp =
  | Set
  | Add
  | Connect
  | Disconnect
  | Insert
  | Remove
  | Move
  | Reconstruct;
```

### Connect

Sugar:

```lgl
connect begin.Then -> print.Exec
begin.Then -> print.Exec
```

Canonical:

```lgl
connect(begin.Then, print.Exec)
```

Normalized JSON:

```ts
interface Connect {
  kind: "connect";
  edge: Edge;
}
```

### Add And Connect

`add name pin -> pin` creates one binding and one immediate edge. It is sugar
for `add name` plus one `connect` edge:

```lgl
add delay begin.Then -> delay.Exec
```

Canonical:

```lgl
add(delay, connect: edge(begin.Then, delay.Exec))
```

Normalized JSON:

```ts
interface Add {
  kind: "add";
  binding: string;
  connect?: Edge;
}
```

Use `add and connect` when adding a new node and attaching one edge. Use
`insert` when replacing an existing direct edge with a new node path.

### Insert

Sugar:

```lgl
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

Canonical:

```lgl
insert(delay, from: begin.Then, to: print.Exec, input: delay.Exec, output: delay.Completed)
```

Normalized JSON:

```ts
interface Insert {
  kind: "insert";
  node: string;
  from: PinRef;
  to: PinRef;
  input: PinRef;
  output: PinRef;
}
```

`from` and `to` describe the existing direct edge being replaced. `input` and
`output` describe the inserted node's pins.

### Other Ops

```lgl
set print.InString = "Ready"
add delay
disconnect(branch.Condition)
remove print
move delay to (320, 0)
move print by (240, 0)
reconstruct print preserve links
```

Normalized JSON:

```ts
interface Set {
  kind: "set";
  target: SetTarget;
  value: Expr;
}

interface SetTarget {
  object: string;
  field: string;
}

type Disconnect =
  | { kind: "disconnect"; edge: Edge; pin?: never }
  | { kind: "disconnect"; pin: PinRef; edge?: never };

interface Remove {
  kind: "remove";
  node: string;
}

type Move =
  | { kind: "move"; node: string; mode: "to"; at: Point }
  | { kind: "move"; node: string; mode: "by"; delta: Point };

interface Reconstruct {
  kind: "reconstruct";
  node: string;
  preserveLinks: boolean;
}
```

`disconnect pin` is shorthand for removing all links from that pin. The
adapter must expand and validate the affected edges against the live graph.

## Patch Preflight

Patch execution must resolve the whole patch before applying mutations. This
allows a patch to create nodes and immediately connect to their pins in one
request:

```lgl
patch g

delay1 = delay(duration: 1.0)
print1 = call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")

add delay1
add print1
connect begin.Then -> delay1.Exec
connect delay1.Completed -> print1.Exec
```

The adapter should process graph patches in phases:

1. Parse bindings and operations.
2. Resolve shortcut constructors and palette sources.
3. Build provisional nodes and pins for newly added aliases.
4. Resolve all pin references against existing graph pins plus provisional pins.
5. Validate connections and graph-state-dependent operations.
6. Apply mutations only after validation succeeds.

Static pins should be discovered through creation-entry details, not through a
mutation dry run:

```lgl
query g
find palette entry "Delay"
with pins
```

`dry run` follows the same validation path through phase 5 and stops before
mutation. Its primary job is to validate the whole patch. It should not become
the main query surface for static pin discovery.

Illustrative dry-run planned result:

```lgl
patch g dry run result

delay1 = node(graph: g, type: Delay, planned: true)
delay1.Exec = pin(type: exec, direction: in)
delay1.Duration = pin(type: float, direction: in, value: 1.0)
delay1.Completed = pin(type: exec, direction: out)
```

Dynamic pins must be determined before validation. Constructor arguments or
node-local edits should carry the necessary information:

```lgl
seq = sequence(outputs: 3)
text = format(text: "Hello {Name}")
```

If the bridge cannot derive a node's pins precisely, it should reject the
one-shot connection with an error that names the missing pin-detail requirement.
It should not silently guess pin names.

## Palette

Palette is the graph domain's creation-entry surface. It groups shortcut
constructors, UE Action Menu fallback entries, and the pin/default details an
agent needs before writing a one-shot patch.

### Shortcut Node Creation

Common UE graph nodes should have shortcut constructors when the agent intent
is stable and explicit. The constructor names describe the common creation
operation, not which editor menu item created it.

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
add print = call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
```

These constructors are not aliases for arbitrary node classes. They are compact
LGL forms for common UE graph creation operations:

| Constructor | Meaning | Example |
| --- | --- | --- |
| `get(variable: ref)` | Variable getter node | `get(variable: Health)` |
| `set(variable: ref)` | Variable setter node | `set(variable: Health)` |
| `event(component: ref, event: name)` | Component-bound event node | `event(component: Trigger, event: OnComponentBeginOverlap)` |
| `call(function: ref)` | Function call node | `call(function: "/Script/Engine.KismetSystemLibrary.PrintString")` |
| `branch()` | Branch node | `branch()` |
| `sequence()` | Execution sequence node | `sequence()` |
| `delay(duration: value)` | Delay node | `delay(duration: 1.0)` |
| `cast(to: classRef)` | Dynamic cast node | `cast(to: "/Script/Engine.Character")` |

The adapter may implement these constructors by using UE palette spawners,
schema helpers, or direct UE node creation APIs. That choice is bridge-internal.
The agent-facing text should preserve the shortcut creation request.

Shortcut constructors and palette fallback can both produce the same UE node
class. For example, `get(variable: Health)` and a palette entry for `Get Health`
both create a variable-get node. They differ only in the returned creation
binding.

Normalized JSON should keep that distinction. The complete `NodeCreation`
model is defined in Creation Object Model below; this section only establishes
that shortcut creation and palette fallback remain separate creation kinds.

The bridge owns UE-dependent resolution: member lookup, variable access
legality, delegate lookup, function overload resolution, and node placement.

### Constructor Selection

Use a shortcut constructor when all of these are true:

1. The UE concept is stable and commonly used.
2. The required arguments are explicit in LGL.
3. The adapter can validate the request against UE state.
4. The agent does not need to choose among multiple Action Menu entries.

When a shortcut constructor exists, `find palette entry` should return that
constructor binding instead of forcing palette-id creation. Palette ids remain
the fallback form for unmodeled UE Action Menu items.

Use palette fallback when any of these are true:

1. The action is plugin-defined or project-defined and has no LGL constructor.
2. The action depends on Action Menu ranking or editor-specific filtering.
3. The action has ambiguous overloads that the current LGL form cannot express.
4. The bridge has not yet modeled the UE concept as a shortcut constructor.

### Constructor Inventory

The first batch should prefer constructors that are already stable in UE and
already have direct or well-understood bridge paths.

| UE concept | Preferred LGL | Normalized kind | Creation path | First batch |
| --- | --- | --- | --- | --- |
| Variable getter | `get(variable: X)` | `variable_get` | `UK2Node_VariableGet` or UE schema helper | yes |
| Variable setter | `set(variable: X)` | `variable_set` | `UK2Node_VariableSet` or UE schema helper | yes |
| Component-bound event | `event(component: X, event: Y)` | `component_bound_event` | `UK2Node_ComponentBoundEvent` or bound-event spawner | yes |
| Custom event node | `event(name: X)` | `custom_event_node` | `UK2Node_CustomEvent` | yes |
| Native event node | `event(name: X, owner: C)` | `event_node` | `FKismetEditorUtilities::AddDefaultEventNode` | yes |
| Function call | `call(function: X)` | `function_call` | `UK2Node_CallFunction` or function spawner | yes |
| Branch | `branch()` | `branch` | `UK2Node_IfThenElse` | yes |
| Sequence | `sequence()` | `sequence` | `UK2Node_ExecutionSequence` | yes |
| Dynamic cast | `cast(to: X)` | `dynamic_cast` | `UK2Node_DynamicCast` | yes |
| Reroute | `reroute()` | `reroute` | `UK2Node_Knot` | yes |
| Comment | `comment(text: "...")` | `comment` | `UEdGraphNode_Comment` | yes |
| Self | `self()` | `self` | `UK2Node_Self` | yes |
| Delay | `delay(duration: X)` | `delay` | function call helper | yes |
| Timeline | `timeline(name: X)` | `timeline` | `UK2Node_Timeline` | maybe |
| Macro instance | `macro(library: A, graph: B)` | `macro_instance` | `UK2Node_MacroInstance` | maybe |
| Function result | `return()` | `function_result` | `UK2Node_FunctionResult` | maybe |
| Collapsed graph | `collapsed_graph(name: X)` | `composite` | `UK2Node_Composite` | maybe |
| Add component node | `add_component(class: X)` | `add_component_node` | `UK2Node_AddComponent` / `UK2Node_AddComponentByClass` | later |
| Delegate bind/assign/call | `bind(dispatcher: X)`, `assign(dispatcher: X)`, `call(dispatcher: X)` | delegate operation nodes | delegate spawners | later |
| Plugin or unmodeled action | none | `palette_node` | Action Menu palette entry | no |

`yes` means the constructor should be part of the initial LGL graph creation
surface. `maybe` means the UE concept is stable but needs a sharper text shape
before it becomes first batch. `later` means the constructor is probably useful,
but the surrounding UE semantics need a separate design pass.

### Constructor Arguments

Constructor arguments must be named. References use the lightest precise form:

| Ref kind | Preferred text | When to use |
| --- | --- | --- |
| Short member name | `Health` | Current Blueprint member or unambiguous graph context |
| Owned member name | `door.Health` | Disambiguating a Blueprint member or component |
| UE object path | `"/Script/Engine.KismetSystemLibrary.PrintString"` | Native, plugin, or cross-asset reference |

Do not use inline value objects as the normal ref form:

```lgl
get(variable: { owner: door, name: Health })
```

Normalized JSON can expand refs into owner/name/path fields. Agent-facing text
should stay compact and readable.

First-batch constructor argument forms:

```lgl
get(variable: Health)
set(variable: Health, value: 100.0)
event(component: Trigger, event: OnComponentBeginOverlap)
event(name: OnDoorOpened)
event(name: ReceiveBeginPlay, owner: "/Script/Engine.Actor")
call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
branch()
sequence()
delay(duration: 1.0)
cast(to: "/Script/Engine.Character")
reroute()
comment(text: "Initialize door state")
self()
```

Rules:

1. `get(variable: X)` creates a variable getter node.
2. `set(variable: X)` creates a variable setter node.
3. `set(variable: X, value: V)` also sets the setter value pin default.
4. `event(component: X, event: Y)` creates a component-bound event node.
5. `event(name: X)` creates a custom event node.
6. `event(name: X, owner: C)` creates a native/default event node.
7. `call(function: X, args...)` creates a function call node and applies pin
   defaults from the remaining named arguments.
8. `delay(duration: X)` is a first-class constructor even though the bridge may
   implement it as a `KismetSystemLibrary.Delay` function call.
9. `cast(to: C)` creates a dynamic cast node.
10. `comment(text: X)` creates an editor comment node.

`event` intentionally uses the agent-facing word `event` even when UE stores the
component case as a multicast delegate property. Normalized JSON should preserve
the UE term:

```ts
interface ComponentBoundEventCreation {
  kind: "component_bound_event";
  component: CreationRef;
  delegate: string;
}
```

If a short ref is ambiguous, the adapter should reject the request and suggest
an owned member name or full path. It should not guess.

### Creation Object Model

Shortcut constructors do not normalize into palette entries. Normalized JSON
must preserve the creation intent:

```txt
shortcut constructor -> constructor normalized JSON -> bridge creation strategy
palette fallback     -> palette normalized JSON   -> bridge palette execution
```

The bridge adapter may implement shortcut creation by using direct UE APIs,
UE schema helpers, or UE Action Menu spawners. That implementation choice must
not be exposed as the normalized LGL truth.

Reference values should normalize into explicit structured refs:

```ts
type CreationRef =
  | { kind: "local"; name: string }
  | { kind: "owned"; owner: string; name: string }
  | { kind: "path"; path: string };
```

Creation bindings should normalize by creation kind. There are exactly two
agent-facing creation forms:

```lgl
# Palette-id fallback.
print = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")

# Semantic shortcut.
delay = delay(duration: 1.0)
```

Existing node readback is separate and uses `type` plus graph identity:

```lgl
print = node(graph: g, type: PrintString, id: "A003", InString: "Ready")
```

`type` and `palette` are mutually exclusive. `type` describes an existing node.
`palette` requests creation through a stable UE palette/action id. Shortcut
constructors request modeled semantic creation. Creation bindings do not need a
`graph` argument because `patch g` and `add` define the graph context.

Normalized JSON preserves creation kind:

```ts
type NodeCreation =
  | { kind: "variable_get"; variable: CreationRef }
  | { kind: "variable_set"; variable: CreationRef; value?: Value }
  | { kind: "component_bound_event"; component: CreationRef; delegate: string }
  | { kind: "custom_event_node"; name: string }
  | { kind: "event_node"; name: string; owner: CreationRef }
  | { kind: "function_call"; function: CreationRef; defaults?: Record<string, Value> }
  | { kind: "branch" }
  | { kind: "sequence" }
  | { kind: "delay"; duration?: Value }
  | { kind: "dynamic_cast"; targetClass: CreationRef }
  | { kind: "reroute" }
  | { kind: "comment"; text: string; size?: Point }
  | { kind: "self" }
  | { kind: "palette_node"; palette: string; defaults?: Record<string, Expr> };
```

Examples:

```lgl
health = get(variable: Health)
setHealth = set(variable: Health, value: 100.0)
overlap = event(component: Trigger, event: OnComponentBeginOverlap)
print = call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
```

```json
[
  {
    "alias": "health",
    "creation": {
      "kind": "variable_get",
      "variable": { "kind": "local", "name": "Health" }
    }
  },
  {
    "alias": "setHealth",
    "creation": {
      "kind": "variable_set",
      "variable": { "kind": "local", "name": "Health" },
      "value": 100.0
    }
  },
  {
    "alias": "overlap",
    "creation": {
      "kind": "component_bound_event",
      "component": { "kind": "local", "name": "Trigger" },
      "delegate": "OnComponentBeginOverlap"
    }
  },
  {
    "alias": "print",
    "creation": {
      "kind": "function_call",
      "function": {
        "kind": "path",
        "path": "/Script/Engine.KismetSystemLibrary.PrintString"
      },
      "defaults": {
        "InString": "Ready"
      }
    }
  }
]
```

Palette fallback remains explicit and uses the palette id directly:

```lgl
delay = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay", Duration: 1.0)
```

```json
{
  "alias": "delay",
  "creation": {
    "kind": "palette_node",
    "palette": "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay",
    "defaults": {
      "Duration": 1.0
    }
  }
}
```

This separation keeps schema validation and error feedback specific. For
example, `variable_get` can return `VARIABLE_NOT_FOUND`, while `palette_node`
can return `PALETTE_ENTRY_NOT_FOUND` or `PALETTE_ENTRY_NOT_EXECUTABLE`.

### Palette Entry Details

`find palette entry` is the single creation-entry query. It searches both
Loomle-modeled shortcut entries and UE Action Menu fallback entries. The result
must be a binding that can be copied into a patch:

- modeled shortcut entries return their constructor binding, such as
  `Delay = delay(duration: value)`
- fallback entries return a palette-id node creation binding, such as
  `PrintString = node(palette: "...")`

Default palette results should stay lightweight, but `with pins` and
`with defaults` may request details for the node that a returned entry would
create.

```lgl
query g
find palette entry "Print String"
with pins, defaults
page limit 50
```

Pin context uses `from pin` or `to pin` on the `find` line. These are
find-form local arguments, not global query clauses. Use them only when the
context is a graph pin and direction matters.

Use `where` for structured palette-entry filters such as component or
context-sensitivity.

Supported first-pass palette-entry filters:

| Filter | Example | UE mapping |
| --- | --- | --- |
| `from pin` | `find palette entry "Branch" from begin.Then` | `FBlueprintActionContext.Pins` |
| `to pin` | `find palette entry "Less Equal" to branch.Condition` | `FBlueprintActionContext.Pins` |
| `component` | `where component = Trigger` | `FBlueprintActionContext.SelectedObjects` component property |
| `contextSensitive` | `where contextSensitive = false` | `MakeContextMenu(..., bIsContextSensitive, ...)` |

Examples:

```lgl
query g
find palette entry "Branch" from begin.Then
with pins

query g
find palette entry from begin.Then
with pins

query g
find palette entry "Less Equal" to branch.Condition
with pins

query g
find palette entry "Begin Overlap"
where component = Trigger
with pins

query g
find palette entry
where component = Trigger
with pins

query g
find palette entry "Print String"
where contextSensitive = false
with pins
```

`contextSensitive = true` is the default and should usually be omitted.
`page limit` defaults to 50 when omitted. The search text after
`find palette entry` may be omitted when `from`, `to`, or `where` provides
enough context. This is useful for context-first discovery from a pin or
component.

Recommended fallback sequence when an entry is not found:

1. Search with text and pin or component context, such as `find palette entry
   "Branch" from begin.Then` or `find palette entry "Begin Overlap"` plus
   `where component = Trigger`.
2. Remove text and keep context, such as `find palette entry` plus `where
   component = Trigger`, or `find palette entry from begin.Then`.
3. Keep text and use `where contextSensitive = false`.
4. Broaden the text, such as `"OnComponentBeginOverlap"` to `"Overlap"`.

Possible fallback text result:

```lgl
PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")

PrintString.Exec = pin(type: exec, direction: in)
PrintString.InString = pin(type: string, direction: in, value: "Ready")
PrintString.Then = pin(type: exec, direction: out)
```

Possible modeled shortcut result:

```lgl
Delay = delay(duration: value)
Delay.Exec = pin(type: exec, direction: in)
Delay.Duration = pin(type: float, direction: in)
Delay.Completed = pin(type: exec, direction: out)
```

Rules:

1. Default palette query returns entry identity and lightweight display fields.
2. `with pins` returns pins for the node the entry would create.
3. `with defaults` returns default values for that created node when UE exposes
   them.
4. Pin and default data must come from UE template nodes, spawners, schemas, or
   other UE-owned metadata, not a Loomle-maintained static node database.
5. Context-dependent entries, such as component-bound events, require the
   corresponding `where` context they would need for execution.
6. If pin/default data is incomplete, return an actionable diagnostic.

Normalized JSON:

```ts
type CreationEntry =
  | ShortcutCreationEntry
  | PaletteCreationEntry;

interface ShortcutCreationEntry {
  name: string;
  constructor: Call;
  label?: string;
  pins?: Pin[];
  defaults?: Record<string, Expr>;
}

interface PaletteCreationEntry {
  name: string;
  palette: {
    kind: "palette";
    id: string;
  };
  label?: string;
  category?: string;
  pins?: Pin[];
  defaults?: Record<string, Expr>;
}
```

Palette entry query results format these entries as copyable object text. A
modeled shortcut entry formats as its constructor binding. A fallback entry
formats as `Name = node(palette: "...")`. Pin details format as `Name.Pin =
pin(...)` lines when `with pins` is requested.

Palette entry details let fallback creation remain one-shot:

```txt
query palette with pins -> patch add + connect
```

without forcing:

```txt
query palette -> add node -> inspect node -> connect
```

### Node Creation

Node object text and node creation use different fields:

| Use | Field | Example |
| --- | --- | --- |
| Object Text | `type` | `node(graph: g, type: Delay, id: "A002")` |
| Shortcut creation | constructor | `get(variable: Health)` |
| Palette fallback creation | `palette` | `node(palette: "palette-id", Duration: 1.0)` |

Shortcut creation text:

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

Palette fallback creation text:

```lgl
delay = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay", Duration: 1.0)
add delay
```

Do not create from ambiguous positional forms:

```lgl
delay = node(g, Delay, Duration: 1.0)
```

The adapter must resolve `palette: "..."` as a stable UE palette/action id.

The adapter must resolve shortcut constructors through the relevant UE owner:
variables and dispatchers through Blueprint member/property metadata,
component-bound events through component properties and multicast delegate
properties, and function calls through UE function metadata.

`node(palette: "...", Duration: 1.0)` lowers to a `palette_node` creation with
`palette: "..."` and `defaults` containing the remaining named arguments. The
adapter executes the palette id in the patch graph context.

## Normalized JSON

Graph normalized JSON is defined beside each feature above. The summary below
shows the top-level graph-domain payloads and keeps existing public names where
they still fit. These variants are implementation details, not top-level LGL
text kinds:

```ts
interface Graph {
  kind: "graph";
  target: Target;
  nodes: Node[];
  edges: Edge[];
  pins?: Pin[];
}

type GraphQuery = Query<GraphFind>;

type GraphPatch = Patch<GraphPatchOp>;
```

Implementation migration details live in
[`../notes/graph-migration.md`](../notes/graph-migration.md). This section
states the target graph-domain object model.

## Adapter Boundary

Pure LGL normalization may:

- convert graph object text edge sugar into `edge(...)`
- convert patch edge sugar into `connect(...)`, `disconnect(...)`, or
  `insert(...)` depending on statement context

Pure LGL normalization must not:

- resolve UE assets
- resolve graph names to `UEdGraph`
- choose palette entries
- resolve shortcut graph constructors
- validate node classes
- validate field names
- validate pin names or directions
- decide whether an edge can connect
- check graph-state-dependent operations such as `insert`

The adapter or bridge owns those UE-dependent responsibilities.
