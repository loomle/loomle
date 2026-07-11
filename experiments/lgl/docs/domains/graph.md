# Graph Domain

## Scope

The graph domain describes UE graph-shaped assets in LGL. It covers graph
identity, node text, pin text, edge/path text, graph queries, graph patches,
shortcut node creation, and palette fallback creation.

## Basic Form

Graph object text is a statement list:

```lgl
bp = asset(path: "/Game/BP_LGLExample.BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)

begin = node(graph: g, type: EventBeginPlay, id: "A001")
print = node(graph: g, type: PrintString, id: "A003", InString: "Ready")
print.InString = pin(id: "pin-guid", type: string, direction: in, value: "Ready")

begin.Then -> print.Exec
```

Returned graph text should be standalone and copyable by default. Query
results repeat required asset and graph bindings so snippets stay
self-contained.

## Graph Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Asset binding | `name = asset(path: "...", type: symbol)` | `bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Graph binding | `name = graph(domain: symbol, asset: ref, id: string, name: symbol, type: symbol)` | `g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)` |
| Node object text | `name = node(graph: ref, type: symbol, id: string, fields...)` | `delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)` |
| Pin object text | `node.pin = pin(id: string, type: symbol, direction: in/out, metadata...)` | `delay.Duration = pin(id: "pin-guid", type: float, direction: in, value: 1.0)` |
| Edge sugar | `pin -> pin` | `begin.Then -> print.Exec` |
| Edge canonical | `edge(pin, pin)` | `edge(begin.Then, print.Exec)` |

Graph ownership is explicit. Nodes use `graph: g`; ownership is not inferred
from source position.

## Graph Identity

Canonical graph identity uses an asset binding plus one ordinary Graph object:

```lgl
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)
```

Graph follows the same object rule used across LGL: `id` is stable identity,
`name` is current readable/searchable name, and `type` is exact domain-owned
semantics. For Blueprint graphs, `id` maps to `UEdGraph::GraphGuid` and `type`
distinguishes event, function, macro, delegate-signature, interface-function,
construction-script, and other UE graph roles.

Normalized object shape:

```ts
interface Graph {
  kind: "graph";
  alias: string;
  domain: string;
  asset: Ref;
  id: string;
  name: string;
  type: Name;
}
```

The earlier `graph: EventGraph` / `graph: id(id: "...")` alternative identity
is removed. It forced readers to choose either name or id and introduced a
needless nested ref object. `query g` and `patch g` still use the local Graph
binding; cross-query identity uses its returned `@id`.

The current schema and adapters still use the earlier Graph target shape. Their
normalized replacement must be reviewed in the schema phase rather than
inferred from this documentation.

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
  id: string;
  type: string;
  fields: Record<string, Expr>;
  at?: Point;
  size?: Point;
}
```

Following the core alias/id rule, returned existing nodes require `id`, mapped
to `UEdGraphNode::NodeGuid`. Creation bindings use aliases before UE creates the
node; the adapter returns the actual `id` after creation.

`at` and `size` are returned only when the query asks for `with layout`.
`at` maps to UE `NodePosX/NodePosY`. `size` maps to UE
`NodeWidth/NodeHeight` and should be omitted when UE has not stored a non-zero
size. LGL does not introduce a separate layout object.

## Pins

Pin object text uses member bindings and named metadata:

```lgl
delay.Exec = pin(id: "exec-pin-guid", type: exec, direction: in)
delay.Duration = pin(id: "duration-pin-guid", type: float, direction: in, value: 1.0)
delay.Completed = pin(id: "completed-pin-guid", type: exec, direction: out)
```

Normalized JSON:

```ts
interface Pin {
  node: string;
  id: string;
  name: string;
  type: string;
  direction: "in" | "out";
  value?: Expr;
  anchor?: Point;
}
```

For an existing Graph Pin, `id` maps to `UEdGraphPin::PinId`. `PinName` remains
readable local syntax, not cross-query identity. UE's optional `PersistentGuid`
is not part of the first public model: it is not present on every pin, and LGL
must not use it or a name to guess after reconstruction. If reconstruction
changes PinId, the old `@id` becomes stale and the mutation result must return
the new Pin objects.

Following the same rule, creation and palette results may describe future pins
without `id`, because no `UEdGraphPin::PinId` exists yet. After UE creates the
node, returned Pin objects must include their actual `id`.

`anchor` is reserved for live editor layout data. Plain asset readback should
not estimate pin anchors because UE stores exact pin positions in Slate widget
geometry, not on `UEdGraphPin`.

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

Those paths use aliases and Pin names from the same LGL document. Cross-query
reads and mutations use the Pin's own stable id:

```lgl
edge(@exec-output-pin-guid, @exec-input-pin-guid)
```

Normalized JSON:

```ts
interface Edge {
  from: PinRef;
  to: PinRef;
}

type PinRef = IdRef | MemberRef;
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
and matched graph objects. `with` expands additional data such as pins,
defaults, and layout.

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

Supported `where` fields for `find nodes`:

- `type`
- `name`
- `id`
- `comment`

Supported `where` fields for `find palette entry`:

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

Supported `order by` keys:

- `name`
- `type`
- `id`

Query text uses one clause per line. Keep the full condition expression on the
`where` line.

Normalized JSON:

```ts
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

Graph query text uses the shared `Query` envelope with `target.domain =
"graph"` and `find = GraphFind`. `where`, `with`, `orderBy`, and `page` use
the shared query model from the language core. The graph domain validates
allowed fields, expansions, sort keys, and whether a pin context is legal for
the selected find form.

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
| Add and connect | `add name pin -> pin` | `add name` then `connect pin -> pin` |
| Connect pins | `connect pin -> pin` | `connect(pin, pin)` |
| Disconnect edge | `disconnect pin -> pin` | `disconnect(pin, pin)` |
| Disconnect pin | `disconnect pin` | same |
| Insert node | `insert pin -> node.input/output -> pin` | `insert(node, from: pin, to: pin, input: pin, output: pin)` |
| Remove node | `remove name` | same |
| Move node | `move name to (x, y)` | same |
| Reconstruct node | `reconstruct name preserve links` | same |

Palette-id creation templates used in a patch should come from
`find palette entry`; see Palette for palette entry discovery and pin/default
details.

`add name = node(...)` uses the shared patch sugar from the language core. Its
canonical form is a graph node binding followed by `add name`.

Normalized JSON:

```ts
interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  bindings: Binding[];
  ops: GraphPatchOp[];
}

interface Binding {
  target: BindingTarget;
  value: BindingValue;
}

type BindingValue = Expr | NodeCreation;

type GraphPatchOp =
  | Set
  | Add
  | Connect
  | Disconnect
  | Insert
  | Remove
  | Move
  | Reconstruct;

interface Set {
  kind: "set";
  target: SetTarget;
  value: Expr;
}

interface SetTarget {
  object: string;
  field: string;
}

interface Add {
  kind: "add";
  binding: string;
}

interface Connect {
  kind: "connect";
  edge: Edge;
}

interface Insert {
  kind: "insert";
  node: string;
  from: PinRef;
  to: PinRef;
  input: PinRef;
  output: PinRef;
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

Operation notes:

- `connect pin -> pin` and bare `pin -> pin` both normalize to `Connect`.
- `add name pin -> pin` is sugar for `add name` followed by `connect pin -> pin`.
  Longer edge chains expand into one `add` op followed by multiple `connect`
  ops. Normalized JSON never stores this as a nested `Add.connect` field.
- `insert from -> node.input/output -> to` replaces an existing direct edge.
  `from` and `to` describe the old edge; `input` and `output` describe the
  inserted node pins.
- `disconnect pin` removes all links from that pin. The adapter expands and
  validates the affected edges against the live graph.

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

Static pins should be discovered through palette entry details, not through a
mutation dry run:

```lgl
query g
find palette entry "Delay"
with pins
```

`dry run` follows the same validation path through phase 5 and stops before
mutation. Its primary job is to validate the whole patch. It should not become
the main query surface for static pin discovery.

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

Palette is the graph domain's global creation-discovery surface. It returns
copyable creation entries for patch text: modeled shortcut constructors when
the intent is stable, and palette-id fallback entries when UE Action Menu state
is the safest creation identity.

```lgl
query g
find palette entry "Print String"
with pins, defaults
page limit 50
```

Default palette results stay lightweight. `with pins` returns pins for the node
an entry would create, and `with defaults` returns defaults when UE exposes
them. Pin/default details must come from UE template nodes, spawners, schemas,
or other UE-owned metadata.

Pin context uses `from pin` or `to pin` on the `find` line. Use `where` for
structured filters such as component or context sensitivity.

| Filter | Example | UE mapping |
| --- | --- | --- |
| `from pin` | `find palette entry "Branch" from begin.Then` | `FBlueprintActionContext.Pins` |
| `to pin` | `find palette entry "Less Equal" to branch.Condition` | `FBlueprintActionContext.Pins` |
| `component` | `where component = Trigger` | `FBlueprintActionContext.SelectedObjects` component property |
| `contextSensitive` | `where contextSensitive = false` | `MakeContextMenu(..., bIsContextSensitive, ...)` |

`contextSensitive = true` is the default and should usually be omitted.
`page limit` defaults to 50. The search text after `find palette entry` may be
omitted when `from`, `to`, or `where` provides enough context.

Palette result examples:

```lgl
Delay = delay(duration: value)
Delay.Exec = pin(type: exec, direction: in)
Delay.Duration = pin(type: float, direction: in)
Delay.Completed = pin(type: exec, direction: out)

PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")
PrintString.Exec = pin(type: exec, direction: in)
PrintString.InString = pin(type: string, direction: in)
PrintString.Then = pin(type: exec, direction: out)
```

Normalized JSON:

```ts
type CreationEntry =
  | ShortcutEntry
  | PaletteEntry;

interface ShortcutEntry {
  name: string;
  constructor: Call;
  label?: string;
  pins?: Pin[];
  defaults?: Record<string, Expr>;
}

interface PaletteEntry {
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

## Node Creation

Node object text and node creation use different fields:

| Use | Field | Example |
| --- | --- | --- |
| Existing node object text | `type` | `node(graph: g, type: Delay, id: "A002")` |
| Shortcut creation | constructor | `delay(duration: 1.0)` |
| Palette fallback creation | `palette` | `node(palette: "palette-id", Duration: 1.0)` |

Shortcut constructors describe common UE graph creation intents. They are not
aliases for arbitrary node classes, and they do not normalize into palette
entries:

```lgl
add health = get(variable: Health)
add setHealth = set(variable: Health, value: 100.0)
add overlap = event(component: Trigger, event: OnComponentBeginOverlap)
add print = call(function: "/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
add delay = delay(duration: 1.0)
```

Use a shortcut constructor when the UE concept is stable, commonly used,
explicit in LGL, and does not require the agent to choose among multiple Action
Menu entries. Use palette fallback for plugin/project actions, editor-ranked
actions, ambiguous overloads, or unmodeled UE concepts:

```lgl
delay = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay", Duration: 1.0)
add delay
```

Stable shortcut constructors:

| Constructor | Meaning |
| --- | --- |
| `get(variable: X)` | Variable getter |
| `set(variable: X, value?: V)` | Variable setter, optionally with value default |
| `event(component: X, event: Y)` | Component-bound event |
| `event(name: X)` | Custom event |
| `event(name: X, owner: C)` | Native/default event |
| `call(function: X, args...)` | Function call with pin defaults |
| `branch()` | Branch |
| `sequence()` | Execution sequence |
| `delay(duration: X)` | Delay |
| `cast(to: C)` | Dynamic cast |
| `reroute()` | Reroute |
| `comment(text: X)` | Editor comment |
| `self()` | Self |

Unmodeled or ambiguous concepts use `node(palette: "...")` until they have a
stable shortcut constructor.

Constructor arguments must be named. References use aliases or binding paths
only inside the current document, stable `@id` for existing concrete objects
across queries, and UE object paths for native, plugin, or cross-asset
references. Do not use positional forms:

```lgl
delay = node(g, Delay, Duration: 1.0)
```

Normalized JSON preserves creation kind:

```ts
type NodeCreation =
  | ShortcutNodeCreation
  | PaletteNodeCreation;

interface ShortcutNodeCreation {
  kind: "shortcut_node";
  constructor: Call;
}

interface PaletteNodeCreation {
  kind: "palette_node";
  palette: string;
  defaults?: Record<string, Expr>;
}
```

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
