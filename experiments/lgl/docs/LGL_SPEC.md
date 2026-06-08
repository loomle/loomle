# LGL Spec Draft

Loomle Graph Lang is a compact text format for graph documents, graph queries,
and graph patches. This draft is intentionally small and exists to test agent
usability.

## Document Forms

```txt
use PrintString from palette "Print String"
use Delay from palette "Delay"

graph EventGraph

begin@7A9D = EventBeginPlay()
print@C2B0 = PrintString("Hello")

begin -> print
```

```txt
use Delay from palette "Delay"

patch EventGraph

set print.Text = "Game Started"
add delay = Delay(1.0)
rewire begin.Then -> delay.Exec
delay.Then -> print.Exec
```

```txt
query EventGraph

find nodes where type = PrintString
find subgraph around print depth 2
find node details print with pins, defaults
```

## Names

Aliases identify nodes inside one LGL document. In exported graph documents,
an alias may carry an existing target node identity:

```txt
print@C2B0 = PrintString("Hello")
```

The part before `@` is the LGL alias. The part after `@` is the target graph's
stable node identity, such as a UE `NodeGuid` or a compact display form of it.
Patch documents normally use aliases, while export/inspect documents use
`alias@id` so agents can refer back to real nodes without reading a separate
metadata block.

Node types and pin names are schema-bound names. The parser accepts text, but an
adapter must validate those names against the target graph schema before
mutation.

## Palette Bindings

Nodes that are created in a target system must be bound to that system's native
creation path. For Unreal Blueprint, this means palette/action database entries
and their node spawners.

```txt
use PrintString from palette "Print String"
use Delay from palette "Delay" where function = "/Script/Engine.KismetSystemLibrary.Delay"
use TriggerBeginOverlap from palette "On Component Begin Overlapped" context component "TriggerBox"
use GetDisplayName from palette "Get Display Name" context from breakHit.HitActor
use StablePrint from palette entry "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"
```

`use` binds a short LGL symbol to a target creation source. `palette` sources
must be resolved by the adapter in the current graph context. `palette entry`
sources are already resolved palette handles and are useful as a fallback when
readability is less important than exact replay.

An `add` statement may only create a node whose type is either a schema-known
native symbol or a symbol introduced by `use`.

## Arguments

Positional arguments are allowed only where the schema defines a stable order.
Until schema binding exists, arguments are preserved as untyped literals.

## Graph Inspect Form

Graph inspect should prefer OpenUI-style one-line bindings:

```txt
graph EventGraph

begin@7A9D = EventBeginPlay() at (0, 0) size (180, 80)
delay@81EF = Delay(1.0) at (320, 0) size (200, 100)
print@C2B0 = PrintString("Ready") at (640, 0) size (220, 120)

begin -> delay -> print
```

This is the compact readback form for existing graphs. It avoids separate
metadata lines such as `node "7A9D" as begin`.

Compact graph inspect does not emit `use` bindings. Existing nodes have already
been created, so readback should prioritize the current graph shape:

`alias@id = Type(args...)` is the required semantic part. `at (x, y)` and
`size (w, h)` are optional layout readback fields. They do not change the node's
type or pins. Exporters may use shortened display ids in the LGL text when the
JSON envelope or side metadata carries the full stable id.

`at` is the node's graph-editor canvas position. `size` is the current visual
bounds when the target editor can provide it. For UE Blueprint, ordinary node
size is readback metadata, not a first-version mutation target.

## Edges And Chains

Edges can be explicit:

```txt
begin.Then -> delay.Exec
delay.Completed -> print.Exec
```

When schema binding can prove the default execution input and output pins, graph
inspect may compress an execution chain:

```txt
begin -> delay -> print
```

This shorthand is for execution flow only. Data flow remains explicit:

```txt
health.Value -> branch.Condition
```

Branching or multi-output execution nodes must name the selected output pin:

```txt
branch.True -> openDoor
branch.False -> printLocked
```

Exporters should only emit chain shorthand when it does not lose UE semantics.
When in doubt, they should fall back to explicit pin edges.

## Query Form

Query documents navigate an existing graph. They do not create nodes and do not
need `use` bindings in the compact form.

```txt
query EventGraph

find nodes where type = PrintString
find edges where node = branch
find exec path from begin to print
find subgraph around branch depth 2
find node details branch with pins, defaults
```

The first query set should stay small:

- `find nodes where <condition>` returns matching node lines.
- `find edges where <condition>` returns explicit pin-to-pin edge lines.
- `find path from <nodeOrPin> to <nodeOrPin>` returns a graph path.
- `find exec path from <nodeOrPin> to <nodeOrPin>` restricts path search to
  execution flow.
- `find data path from <nodeOrPin> to <nodeOrPin>` restricts path search to
  data flow.
- `find subgraph around <nodeOrPin> depth <number>` returns a compact LGL graph
  containing nearby nodes and edges.
- `find node details <node> with pins, defaults, ue` returns an LGL node details
  document.

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

Initial edge conditions:

```txt
where node = branch
where from = begin
where to = branch
where from.pin = True
where to.pin = Condition
where flow = exec
where flow = data
```

Query results should prefer LGL text. Structured JSON may wrap the result for
transport, but the graph content should remain readable as LGL.

## Pin Details

Compact graph inspect should not include every pin. Detailed pin readback is
returned by `find node details`.

Pin declaration syntax:

```txt
pin <node>.<pin>: <type> <direction>
pin <node>.<pin>: <type> <direction> = <default>
pin <node>.<pin>: <type> <direction> <- <source.pin>
pin <node>.<pin>: <type> <direction> -> <target.pin>
pin <node>.<pin>: <type> <direction> anchor (<x>, <y>)
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
node details EventGraph

branch@B004 = Branch()

pin branch.Exec: exec in <- begin.Then anchor (400, 24)
pin branch.Condition: bool in <- threshold.ReturnValue anchor (400, 72)
pin branch.True: exec out -> dead.Exec anchor (560, 24)
pin branch.False: exec out -> alive.Exec anchor (560, 56)
```

```txt
node details EventGraph

spawn@G004 = SpawnActorFromClass(BP_Projectile, AlwaysSpawn)

pin spawn.Exec: exec in <- fire.Pressed anchor (640, 24)
pin spawn.Class: class<Actor> in = BP_Projectile anchor (640, 72)
pin spawn.SpawnTransform: transform in <- socketTransform.ReturnValue anchor (640, 112)
pin spawn.CollisionHandlingOverride: enum<SpawnActorCollisionHandlingMethod> in = AlwaysSpawn anchor (640, 152)
pin spawn.Owner: object<Actor> in <- owner.ReturnValue anchor (640, 192)
pin spawn.Then: exec out -> print.Exec anchor (860, 24)
pin spawn.ReturnValue: object<BP_Projectile> out anchor (860, 72)
```

For output pins with multiple links, repeat the pin line once per target rather
than hiding multiple links in a dense list:

```txt
pin sequence.Then0: exec out -> printA.Exec
pin sequence.Then0: exec out -> printB.Exec
```

`anchor (x, y)` is optional pin layout readback. It is the visible pin connection
anchor in absolute graph-editor canvas coordinates. Pin anchors are readback
only in the first version; patches should not directly move pins.

## Patch Operations

- `set node.property = value` updates a node property or editable pin default.
- `add alias = Type(args...)` creates a node.
- `rewire from.node.pin -> to.node.pin` redirects links from a source pin to a
  new target pin according to adapter rules.
- A bare edge line inside a patch creates a connection. Patch documents may use
  chain shorthand when the adapter can validate default exec pins.
- `move node to (x, y)` moves a graph node to an absolute canvas position.
- `move node by (dx, dy)` moves a graph node by a relative canvas delta.

`rewire` is deliberately underspecified in this draft. Blueprint exec pins,
data pins, material inputs, and PCG pins have different multiplicity rules, so
the adapter must own final validation.

Patch documents use `use` bindings when they create nodes:

```txt
use Delay from palette "Delay"

patch EventGraph

add delay = Delay(1.0)
rewire begin.Then -> delay.Exec
delay -> print
move delay to (320, 0)
```

Patch layout mutation is intentionally narrow in the first version. It supports
node movement only. It does not resize ordinary nodes, move pin anchors, or
define comment/reroute behavior yet. Comment boxes and reroute nodes need their
own design before they become patch targets.
