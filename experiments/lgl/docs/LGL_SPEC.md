# LGL Spec Draft

Loomle Graph Lang is the text format used by the LGL SDK for graph documents,
graph queries, and graph patches. This draft is intentionally small and exists
to test agent usability.

## Document Forms

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

begin@7A9D: EventBeginPlay()
print@C2B0: PrintString({InString: "Hello"})

begin.Then -> print.Exec
```

```txt
patch blueprint("/Game/BP_Door"/EventGraph) dry run

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

set print.InString = "Game Started"
delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find nodes where type = PrintString
```

## Document Scope

LGL documents are self-describing. The graph system and asset live in the text,
not in SDK side parameters:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

The document header selects both the operation and the graph reference:

```txt
graph blueprint("/Game/BP_Door"/EventGraph)
query blueprint("/Game/BP_Door"/EventGraph)
patch blueprint("/Game/BP_Door"/EventGraph)
```

Future graph systems should use the same pattern:

```txt
query material("/Game/M_Wood"/MaterialGraph)
query pcg("/Game/PCG_Forest"/PCGGraph)
```

When a target exposes a graph id, the id remains scoped by the asset path. It is
a Loomle graph reference id, not a global UE object id:

```txt
query blueprint("/Game/BP_Door"/id("graph-id"))
```

Patch documents may request dry-run behavior in text:

```txt
patch blueprint("/Game/BP_Door"/EventGraph) dry run
```

Dry run is part of the LGL document header, not an SDK option.

## Names

Aliases identify nodes inside one LGL document. In exported graph documents,
an alias may carry an existing target node identity:

```txt
print@C2B0: PrintString({InString: "Hello"})
```

The part before `@` is the LGL alias. The part after `@` is the target graph's
stable node identity, such as a UE `NodeGuid` or a compact display form of it.
Patch documents normally use aliases, while graph/query readback uses
`alias@id` so agents can refer back to real nodes without reading a separate
metadata block.

Patch references may use either a document alias or an id reference:

```txt
set print.InString = "Ready"
set @C2B0.InString = "Ready"
```

`alias@id` is a declaration/readback form, not a reference form inside patch
operations. When an agent receives `print@C2B0: PrintString({...})`, it should
refer to that node as either `print` or `@C2B0`.

Node types, field names, and pin names are schema-bound names. The parser
accepts text, but an adapter must validate those names against the target graph
schema before mutation.

LGL keywords are lowercase, such as `graph`, `query`, `patch`, `find`, `add`,
`set`, `connect`, and `disconnect`. Schema-bound names keep the target system's
native spelling and casing. For Unreal Blueprint, that means names such as
`InString`, `Duration`, `Class`, `SpawnTransform`, and
`CollisionHandlingOverride` should not be normalized to LGL-specific casing.

Node type names are schema or palette constructor symbols, not editor display
titles. For example, use `PrintString` and `SpawnActorFromClass`, not display
titles such as `Print String` or `Spawn Actor from Class`. Display titles may
appear as metadata in palette query results, but they are not the node type used
by graph readback or patch specs.

## Palette Bindings

Nodes that are created in a target system must be bound to that system's native
creation path. For Unreal Blueprint, this means palette/action database entries
and their node spawners.

```txt
PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"})
Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
TriggerBeginOverlap = palette({id: "palette:blueprint:component_event:/Game/BP_Door.TriggerBox.OnComponentBeginOverlap"})
```

Palette binding lines bind a short LGL constructor symbol to an already resolved
target creation source. The symbol should be stable within the document and
should not be copied from an editor display title when that title contains
spaces, localization, or contextual wording. `palette(...)` takes a named object
whose `id` field is the stable palette entry id returned by `find palette
entry`; patch documents do not perform fuzzy palette search.

Palette binding lines are only valid inside `patch` documents and `palette`
query results. Graph documents never emit palette bindings. Query documents
discover creation sources with `find palette entry`.

An `add` statement may only create a node whose type is either a schema-known
native node name or a binding name introduced by `Name = palette({id: "..."})`.

## Arguments

Node arguments must use named fields. Positional node arguments are not part of
the stable LGL form, because UE nodes do not expose a universal constructor
argument order. This keeps graph readback and patch input aligned with
schema-bound pins and properties:

```txt
EventBeginPlay()
Branch()
Delay({Duration: 1.0})
PrintString({InString: "Ready"})
SpawnActorFromClass({Class: BP_Projectile, CollisionHandlingOverride: AlwaysSpawn})
```

A node spec always keeps call syntax. Nodes without arguments use `Type()`.
Nodes with arguments use `Type({Field: value})`. Omitting `()` or using
positional arguments is not part of the stable form.

Structured metadata should be carried as an object literal argument, not as
free-form trailing fields. This keeps LGL line-oriented while giving complex
results a single parseable shape:

```txt
Name = Type({field: value, other: "text"})
```

Object literals outside pin declarations should use named fields only. UE does
not provide a universal primary-value concept for arbitrary graph metadata.
Pin declarations have one local shorthand: a leading unnamed value inside
`{...}` means the pin default.

```txt
Duration: float in {1.0, anchor: [320, 72]}
```

## Graph Inspect Form

Graph inspect should prefer OpenUI-style one-line bindings:

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

begin@7A9D: EventBeginPlay() {at: [0, 0], size: [180, 80]}
delay@81EF: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
print@C2B0: PrintString({InString: "Ready"}) {at: [640, 0], size: [220, 120]}

begin.Then -> delay.Exec/Completed -> print.Exec
```

This is the stable readback form for existing graphs. It avoids separate
metadata lines such as `node "7A9D" as begin`.

Compact graph inspect does not emit palette bindings. Existing nodes have already
been created, so readback should prioritize the current graph shape:

`alias@id: Type()` or `alias@id: Type({fields...})` is the required semantic
part. A trailing object may carry optional readback metadata such as
`{at: [x, y], size: [w, h]}`. Metadata does not change the node's type or pins.
Snapshot writers may use shortened display ids in the LGL text when the JSON
envelope or side metadata carries the full stable id.

`at` is the node's graph-editor canvas position. `size` is the current visual
bounds when the target editor can provide it. For UE Blueprint, ordinary node
size is readback metadata, not a first-version mutation target.

## Edges

Edges must name pins explicitly:

```txt
begin.Then -> delay.Exec
delay.Completed -> print.Exec
```

Linear paths may be written as explicit pin chains. Each middle segment uses
`input/output` on the same node and expands to two adjacent edges:

```txt
begin.Then -> delay.Exec/Completed -> print.Exec
```

This is equivalent to:

```txt
begin.Then -> delay.Exec
delay.Completed -> print.Exec
```

Chains may include multiple middle nodes:

```txt
tick.Then -> trace.Exec/Then -> branch.Exec/True -> print.Exec
```

This expands to:

```txt
tick.Then -> trace.Exec
trace.Then -> branch.Exec
branch.True -> print.Exec
```

The first segment must be an output pin. The last segment must be an input pin.
Every middle `input/output` segment must refer to pins on one node. The adapter
validates every expanded edge against the target graph schema.

Data flow uses the same explicit edge form:

```txt
health.Value -> branch.Condition
```

Branching or multi-output execution nodes name the selected output pin:

```txt
branch.True -> openDoor.Exec
branch.False -> printLocked.Exec
```

Readback and patch documents should not emit implicit node chains such as
`begin -> delay -> print`. A later display-only compact mode may add them back,
but the stable agent-facing form must name pins.

## Query Form

Query documents navigate an existing graph. They do not create nodes and do not
need palette bindings in the compact form. A query document contains exactly one
`find` statement; callers issue multiple queries as multiple LGL documents.
When the query body is empty, the result is a compact full graph snapshot.

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find node branch with pins, defaults
```

The first query set should stay small:

- `find nodes where <condition>` returns matching node lines.
- `find nodes where <condition> with pins, defaults, layout` returns matching
  node lines plus requested detail lines.
- `find node <node> with pins, defaults, layout` is a shortcut for one node.
- `find path from <node.pin>` returns a path from one pin. Output pins walk
  downstream; input pins walk upstream.
- `find surrounding around <node> depth <number>` returns a compact LGL graph
  containing nearby nodes and links around one node.
- `find palette entry <text>` returns matching creation entries for the current
  graph context.
- `find palette entry <text> where <condition>` returns matching creation
  entries with more explicit constraints.
- `find palette entry where <condition>` returns matching creation entries using
  only structured constraints.

Examples:

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find palette entry "Print String"
```

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find palette entry "Print" where function = "/Script/Engine.KismetSystemLibrary.PrintString"
```

Palette query results are LGL documents:

```txt
palette blueprint("/Game/BP_Door"/EventGraph)

PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", title: "Print String", category: "Utilities/String"})
```

Initial node conditions:

```txt
where type = PrintString
where title contains "Print"
where alias = print
where id = C2B0
where has pin InString
where pin InString default contains "Ready"
where pin Condition linked
where event component = Trigger
where function = "/Script/Engine.KismetSystemLibrary.PrintString"
where variable = Health
```

Query results should prefer LGL text. Structured JSON may wrap the result for
transport, but the graph content should remain readable as LGL.

## Pin Details

Compact graph inspect should not include every pin. Detailed pin readback is
returned by `find node ... with pins` or `find nodes where ... with pins`.

Pin declaration syntax omits a keyword and is part of the graph document:

```txt
<node>.<pin>: <type> <direction>
<node>.<pin>: <type> <direction> {anchor: [<x>, <y>]}
<node>.<pin>: <type> <direction> {<default>}
<node>.<pin>: <type> <direction> {<default>, anchor: [<x>, <y>]}
```

`in` and `out` are the initial directions. Types are normalized readable LGL
types, not raw UE reflection dumps:

```txt
exec
bool
int
float
string
name
text
vector
rotator
transform
object<Actor>
class<Actor>
array<vector>
enum<SpawnActorCollisionHandlingMethod>
```

Examples:

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

branch@B004: Branch()

branch.Exec: exec in {anchor: [400, 24]}
branch.Condition: bool in {anchor: [400, 72]}
branch.True: exec out {anchor: [560, 24]}
branch.False: exec out {anchor: [560, 56]}

begin.Then -> branch.Exec
threshold.ReturnValue -> branch.Condition
branch.True -> dead.Exec
branch.False -> alive.Exec
```

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

spawn@G004: SpawnActorFromClass({Class: BP_Projectile, CollisionHandlingOverride: AlwaysSpawn})

spawn.Exec: exec in {anchor: [640, 24]}
spawn.Class: class<Actor> in {BP_Projectile, anchor: [640, 72]}
spawn.SpawnTransform: transform in {anchor: [640, 112]}
spawn.CollisionHandlingOverride: enum<SpawnActorCollisionHandlingMethod> in {AlwaysSpawn, anchor: [640, 152]}
spawn.Owner: object<Actor> in {anchor: [640, 192]}
spawn.Then: exec out {anchor: [860, 24]}
spawn.ReturnValue: object<BP_Projectile> out {anchor: [860, 72]}

fire.Pressed -> spawn.Exec/Then -> print.Exec
socketTransform.ReturnValue -> spawn.SpawnTransform
owner.ReturnValue -> spawn.Owner
```

Links are still expressed with edge lines, not pin declarations. For output pins
with multiple links, repeat edge lines rather than hiding multiple links in a
dense list:

```txt
sequence.Then0 -> printA.Exec
sequence.Then0 -> printB.Exec
```

For pin declarations, the leading unnamed value inside `{...}` is the pin
default. `{anchor: [x, y]}` is optional pin layout readback. It is the visible
pin connection anchor in absolute graph-editor canvas coordinates. Pin anchors
are readback only in the first version; patches should not directly move pins.

## Patch Operations

Patch documents edit one graph. They may bind palette entries, define local node
specs, create nodes, change defaults, edit links, move nodes, and apply small
refactors. `=` is local assignment. `add` is graph mutation. `->` always means a
real graph link direction. `/` inside an edge segment pairs an input pin and an
output pin on the same node, as in `delay.Exec/Completed`.

Core operations:

- `set node.property = value`: set a node property or editable pin default.
- `name = Type()` or `name = Type({fields...})`: assign a local node spec.
- `add alias`: create one unlinked node from a local node spec.
- `add <output-pin> -> alias.<input-pin>`: create the local node spec referenced
  by `alias` and connect into one of its input pins.
- `add alias.<output-pin> -> <input-pin>`: create the local node spec referenced
  by `alias` and connect from one of its output pins.
- `insert <output-pin> -> alias.<input-pin>/<output-pin> -> <input-pin>`:
  create the local node spec referenced by `alias`, replace an existing direct
  link, and connect the expanded chain through the new node.
- `connect <output-pin> -> <input-pin>`: create one link.
- `connect <explicit-pin-chain>`: expand the chain and create each link.
- `disconnect <output-pin> -> <input-pin>`: remove one link.
- `disconnect <pin>`: remove all links attached to one pin.
- `remove <node>`: remove one node and its attached links.
- `move node to (x, y)`: move one node to an absolute canvas position.
- `move node by (dx, dy)`: move one node by a relative canvas delta.
- `reconstruct node preserve links`: ask the target adapter to refresh one
  node's target-owned pins and metadata.

Link rules:

- `connect A -> B` requires `A` to be an output pin and `B` to be a compatible
  input pin.
- `disconnect A -> B` removes exactly that link. Missing links are errors.
- `disconnect P` removes every link attached to `P`; results should report the
  removed link count.
- A bare explicit edge line in a patch is shorthand for `connect`.
- Explicit pin chains such as `A -> N.In/Out -> B` expand to multiple `connect`
  operations.
- `add` may connect only one side of a newly created node. Use `connect` for
  ordinary links and `insert` for two-sided replacement.
- `insert A -> N.In/Out -> B` creates `N`, replaces an existing direct `A -> B`
  link, and connects the expanded chain.
- Implicit node chains such as `begin -> delay -> print` are not part of the
  stable patch form.

Refactor rules:

- `insert A -> N.In/Out -> B` requires an existing direct link
  `A -> B` and a local node spec `N = Type()` or `N = Type({fields...})`. The
  adapter removes the old link, creates `N`, then creates `A -> N.In` and
  `N.Out -> B`.
- `remove N` does not reconnect or repair surrounding structure. Preserve graph
  flow with explicit `disconnect`, `connect`, and `insert` operations.
- `reconstruct` is a target-specific maintenance escape hatch. Adapters should
  perform reconstruction automatically when a supported `add` or `set` needs it.

Examples:

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
```

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
```


```txt
patch blueprint("/Game/BP_Door"/EventGraph)

disconnect health.Value -> branch.Condition
```

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

remove print
```

Patch layout mutation is intentionally narrow in the first version. It supports
node movement only. It does not resize ordinary nodes, move pin anchors, or
define comment/reroute behavior yet. Comment boxes and reroute nodes need their
own design before they become patch targets.
