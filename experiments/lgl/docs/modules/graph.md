# Graph Module

## Scope

The graph module describes UE graph-shaped assets in LGL. It covers graph
identity, node readback, pin readback, edge/path text, graph queries, graph
patches, semantic node creation, and palette fallback creation.

Implementation migration notes live in
[`../notes/graph-migration.md`](../notes/graph-migration.md). This document
describes the target module shape.

## Basic Form

Graph readback text is a statement list:

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
| Node readback | `name = node(graph: ref, type: symbol, id: string, fields...)` | `delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)` |
| Pin readback | `node.pin = pin(type: symbol, direction: in/out, metadata...)` | `delay.Duration = pin(type: float, direction: in, value: 1.0)` |
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
```

Graph refs may be names or stable ids:

```lgl
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
gById = graph(domain: blueprint, asset: bp, graph: id("graph-id"))
```

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
  fields: Record<string, Value>;
  at?: Point;
  size?: Point;
}
```

`at` and `size` are graph editor readback metadata. LGL does not introduce a
separate layout object.

## Pins

Pin readback uses member bindings and named metadata:

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
  value?: Value;
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

## Semantic Node Creation

Common UE graph nodes should be created through semantic constructors when the
agent intent is stable and explicit. The constructor names describe what the
node means in the graph, not which editor menu item created it.

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
add print = call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
```

These constructors are not aliases for arbitrary node classes. They are stable
LGL forms for UE concepts that already have clear editor semantics:

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
The agent-facing text should preserve the semantic intent.

Semantic constructors and palette creation can both produce the same UE node
class. For example, `get(variable: Health)` and a palette entry for `Get Health`
both create a variable-get node. They differ only in how the creation request
is expressed.

Normalized JSON should keep that distinction:

```ts
type NodeCreation =
  | { kind: "variable_get"; variable: MemberRef }
  | { kind: "variable_set"; variable: MemberRef }
  | { kind: "component_bound_event"; component: MemberRef; delegate: string }
  | { kind: "function_call"; function: FunctionRef }
  | { kind: "palette_node"; source: PaletteRef };
```

The bridge owns UE-dependent resolution: member lookup, variable access
legality, delegate lookup, function overload resolution, and node placement.

### Constructor Selection

Use a semantic constructor when all of these are true:

1. The UE concept is stable and commonly used.
2. The required arguments are explicit in LGL.
3. The adapter can validate the request against UE state.
4. The agent does not need to choose among multiple Action Menu entries.

When a semantic constructor exists, agents should not be required to query
palette first. Palette may still create the same UE node, but it is not the
primary LGL path for that intent.

Use palette fallback when any of these are true:

1. The action is plugin-defined or project-defined and has no LGL constructor.
2. The action depends on Action Menu ranking or editor-specific filtering.
3. The action has ambiguous overloads that the current LGL form cannot express.
4. The bridge has not yet modeled the UE concept as a semantic constructor.

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
  component: MemberRef;
  delegate: string;
}
```

If a short ref is ambiguous, the adapter should reject the request and suggest
an owned member name or full path. It should not guess.

### Creation Object Model

Semantic constructors do not normalize into palette entries. Normalized JSON
must preserve the creation intent:

```txt
semantic constructor -> semantic normalized JSON -> bridge creation strategy
palette fallback     -> palette normalized JSON   -> bridge palette execution
```

The bridge adapter may implement a semantic creation by using direct UE APIs,
UE schema helpers, or UE Action Menu spawners. That implementation choice must
not be exposed as the normalized LGL truth.

Reference values should normalize into explicit structured refs:

```ts
type Ref =
  | { kind: "local"; name: string }
  | { kind: "owned"; owner: string; name: string }
  | { kind: "path"; path: string };
```

Creation bindings should normalize by semantic kind:

```ts
type NodeCreation =
  | { kind: "variable_get"; variable: Ref }
  | { kind: "variable_set"; variable: Ref; value?: Value }
  | { kind: "component_bound_event"; component: Ref; delegate: string }
  | { kind: "custom_event_node"; name: string }
  | { kind: "event_node"; name: string; owner: Ref }
  | { kind: "function_call"; function: Ref; defaults?: Record<string, Value> }
  | { kind: "branch" }
  | { kind: "sequence" }
  | { kind: "delay"; duration?: Value }
  | { kind: "dynamic_cast"; targetClass: Ref }
  | { kind: "reroute" }
  | { kind: "comment"; text: string; size?: Point }
  | { kind: "self" }
  | { kind: "palette_node"; source: string; defaults?: Record<string, Value> };
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

Palette fallback remains explicit:

```lgl
DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
delay = node(graph: g, source: DelaySource, Duration: 1.0)
```

```json
{
  "alias": "delay",
  "creation": {
    "kind": "palette_node",
    "source": "DelaySource",
    "defaults": {
      "Duration": 1.0
    }
  }
}
```

This separation keeps schema validation and error feedback semantic. For
example, `variable_get` can return `VARIABLE_NOT_FOUND`, while `palette_node`
can return `PALETTE_ENTRY_NOT_FOUND` or `PALETTE_ENTRY_NOT_EXECUTABLE`.

## Palette Sources

Palette is the shared LGL mechanism for discovery and exact fallback creation.
It is not graph-only, but graph patches may consume palette bindings when a
node does not have a stable semantic constructor or when the agent intentionally
wants the exact UE Action Menu choice.

```lgl
PrintStringSource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")
DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
```

Palette creation must resolve through palette ids, not display names or
inferred types. Semantic constructors do not require palette ids.

Palette results are also useful as discovery data. An agent may query palette
to learn what UE can create in the current graph context, then use a semantic
constructor if the desired action maps to one.

### Palette Preview

Palette query is also the fallback preview path. Default palette results should
stay lightweight, but `with` clauses may request preview metadata for the node
that a palette entry would create.

```lgl
query g
find palette entry "Print String"
with preview, pins, defaults
limit 5
```

Possible text result:

```lgl
PrintString = palette(
  id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString",
  label: "Print String",
  previewType: K2Node_CallFunction,
  previewComplete: true
)

PrintString.Exec = pin(type: exec, direction: in)
PrintString.InString = pin(type: string, direction: in, value: "Ready")
PrintString.Then = pin(type: exec, direction: out)
```

Preview metadata is not semantic discovery. It describes what the selected
palette entry is expected to create:

```ts
interface PalettePreview {
  nodeType?: string;
  pins?: Pin[];
  defaults?: Record<string, Value>;
  complete: boolean;
  incompleteReason?: string;
}
```

Rules:

1. Default palette query returns entry identity and lightweight display fields.
2. `with pins` returns preview pins for the created node.
3. `with defaults` returns preview default values when UE exposes them.
4. `with preview` returns creation summary such as node class/type and
   completeness.
5. Preview data must come from UE template nodes, spawners, schemas, or other
   UE-owned metadata, not a Loomle-maintained static node database.
6. Context-dependent entries, such as component-bound events, require the same
   context they would need for execution.
7. If preview is partial, return `complete: false` and an actionable
   `incompleteReason`.

Palette preview lets fallback creation remain one-shot:

```txt
query palette with pins -> patch add + connect
```

without forcing:

```txt
query palette -> add node -> inspect node -> connect
```

## Node Creation

Node readback and node creation use different fields:

| Use | Field | Example |
| --- | --- | --- |
| Readback | `type` | `node(graph: g, type: Delay, id: "A002")` |
| Semantic creation | constructor | `get(variable: Health)` |
| Palette fallback creation | `source` | `node(graph: g, source: DelaySource, Duration: 1.0)` |

Semantic creation text:

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
```

Palette fallback creation text:

```lgl
DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
delay = node(graph: g, source: DelaySource, Duration: 1.0)
add delay
```

Do not create from ambiguous positional forms:

```lgl
delay = node(g, Delay, Duration: 1.0)
```

The adapter must resolve `source: DelaySource` to a `palette(id: "...")`
binding before creating the node.

The adapter must resolve semantic constructors through the relevant UE owner:
variables and dispatchers through Blueprint member/property metadata,
component-bound events through component properties and multicast delegate
properties, and function calls through UE function metadata.

## Query

Graph query is a statement list:

```lgl
query g
find nodes
where type = PrintString and not id = "A001"
with pins, defaults
order by name asc
limit 10
```

Query clauses:

| Clause | Syntax | Example |
| --- | --- | --- |
| Target | `query graphRef` | `query g` |
| Find nodes | `find nodes` | `find nodes` |
| Find node | `find node alias` | `find node branch` |
| Find path | `find path from pin` | `find path from begin.Then` |
| Surrounding | `find surrounding around alias depth number` | `find surrounding around branch depth 2` |
| Palette search | `find palette entry "text"` | `find palette entry "Print String"` |
| Condition | `where condition` | `where type = PrintString and not id = "A001"` |
| Included data | `with item, item` | `with pins, defaults` |
| Ordering | `order by key asc/desc` | `order by name asc` |
| Result limit | `limit number` | `limit 10` |

Graph query has no `select` clause. The default result includes graph context
and matched graph objects. `with` expands additional data such as pins and
defaults.

Supported first-pass `where` fields:

- `type`
- `name`
- `id`
- `text`

Supported first-pass `order by` keys:

- `name`
- `type`
- `id`

Single-line query text may be accepted as sugar:

```lgl
query g find nodes where type = PrintString and name ~= "Print" with pins, defaults order by name asc limit 10
```

Canonical query text uses one clause per line, with the full condition
expression kept on the `where` line.

## Patch

Graph patch is a statement list:

```lgl
patch g dry run

DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
delay = node(graph: g, source: DelaySource, Duration: 1.0)
health = get(variable: Health)
overlap = event(component: Trigger, event: OnComponentBeginOverlap)

add delay
add health
add overlap
insert begin.Then -> delay.Exec/Completed -> print.Exec
set print.InString = "Game Started"
move print to (640, 0)
```

Patch operations:

| Operation | Sugar | Canonical |
| --- | --- | --- |
| Set field | `set target = value` | same |
| Add binding | `add name` | same |
| Connect pins | `connect pin -> pin` | `connect(pin, pin)` |
| Disconnect edge | `disconnect pin -> pin` | `disconnect(pin, pin)` |
| Disconnect pin | `disconnect pin` | same |
| Insert node | `insert pin -> node.input/output -> pin` | `insert(node, from: pin, to: pin, input: pin, output: pin)` |
| Remove binding | `remove name` | same |
| Move node | `move name to (x, y)` | same |
| Reconstruct node | `reconstruct name preserve links` | same |

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
2. Resolve semantic constructors and palette sources.
3. Build preview nodes and preview pins for newly added aliases.
4. Resolve all pin references against existing graph pins plus preview pins.
5. Validate connections and graph-state-dependent operations.
6. Apply mutations only after validation succeeds.

`dry run` follows the same path through phase 5 and stops before mutation. A
dry-run result should include planned nodes and pins so the agent can refine a
patch without applying partial graph edits:

```lgl
patch g dry run result

delay1 = node(graph: g, type: Delay, preview: true)
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

If the bridge cannot preview a node's pins precisely, it should reject the
one-shot connection with an error that names the missing preview requirement.
It should not silently guess pin names.

## Normalized JSON

The target graph schema keeps existing public names where they still fit:

```ts
type LglObject = Graph | Query | Patch | Palette;

interface Graph {
  kind: "graph";
  target: Target;
  nodes: Node[];
  edges: Edge[];
  pins?: Pin[];
}

interface Query {
  kind: "query";
  target: Target;
  find?: Find;
}

interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  bindings: Binding[];
  ops: Op[];
}
```

Known schema migrations:

```txt
Node.layout.at        -> Node.at
Node.layout.size      -> Node.size
Pin.layout.anchor     -> Pin.anchor
Connect.chain         -> Connect.edge
Insert.chain          -> Insert.from / Insert.to / Insert.input / Insert.output
palette node creation -> node(..., source: PaletteBindingName, ...)
semantic node creation -> get(...), set(...), event(...), call(...)
```

## Adapter Boundary

Pure LGL normalization may:

- convert graph readback edge sugar into `edge(...)`
- convert patch edge sugar into `connect(...)`, `disconnect(...)`, or
  `insert(...)` depending on statement context
- convert single-line query sugar into query clauses without splitting the
  `where` condition expression

Pure LGL normalization must not:

- resolve UE assets
- resolve graph names to `UEdGraph`
- choose palette entries
- resolve semantic graph constructors
- validate node classes
- validate field names
- validate pin names or directions
- decide whether an edge can connect
- check graph-state-dependent operations such as `insert`

The adapter or bridge owns those UE-dependent responsibilities.
