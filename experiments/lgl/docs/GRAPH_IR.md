# Graph IR Draft

Graph IR is Loomle's internal structured representation after parsing LGL text.
It is not the user-facing language and it is not a mirror of Unreal's native
objects.

## Graph Document

```ts
interface GraphDocument {
  kind: "graph";
  name: string;
  bindings: UseBinding[];
  statements: GraphStatement[];
}
```

Graph statements currently include node declarations and edges. Exported graph
documents may attach a target node id to each node declaration. In text form this
is written as `alias@id = Type(args...)`; in IR it should remain a field on the
node declaration rather than a separate metadata statement.

## Patch Document

```ts
interface PatchDocument {
  kind: "patch";
  name: string;
  bindings: UseBinding[];
  statements: PatchStatement[];
}
```

Patch statements include `set`, `add`, `rewire`, and `connect`.

## Query Document

```ts
interface QueryDocument {
  kind: "query";
  name: string;
  statements: QueryStatement[];
}
```

Query statements are read operations over an existing graph. The initial
statement set is:

- `find nodes where ...`
- `find edges where ...`
- `find path from ... to ...`
- `find exec path from ... to ...`
- `find data path from ... to ...`
- `find subgraph around ... depth ...`
- `find node details ... with ...`

Query results may carry structured transport metadata, but graph content should
round-trip as LGL result documents such as compact `graph` snippets, explicit
edge lists, or `node details` documents.

## Compact Inspect Syntax

LGL graph inspect should be line-oriented and compact:

```txt
begin@7A9D = EventBeginPlay()
delay@81EF = Delay(1.0)
print@C2B0 = PrintString("Ready")

begin -> delay -> print
```

This text form maps to ordinary graph IR:

- node alias: `begin`
- target node id: `7A9D`
- node type: `EventBeginPlay`
- exec edges: `begin.Then -> delay.Exec`, then `delay.Completed -> print.Exec`

The parser/exporter should expand chain shorthand into explicit edge IR after
schema binding identifies default exec pins.

Node declarations may also carry layout readback:

```txt
delay@81EF = Delay(1.0) at (320, 0) size (200, 100)
```

In IR this should preserve:

- graph-editor canvas position from `at`
- optional visual bounds from `size`

For UE Blueprint, ordinary node size is readback metadata. The first patch
layout mutation only moves nodes; it does not resize them.

## Pin Detail IR

`find node details` returns pin declarations. Text form:

```txt
pin branch.Condition: bool in <- threshold.ReturnValue anchor (400, 72)
pin print.InString: string in = "Ready" anchor (640, 72)
pin print.Then: exec out anchor (820, 24)
```

In IR this should preserve:

- node alias
- pin name
- normalized type
- direction
- optional default value
- zero or more incoming links
- zero or more outgoing links
- optional absolute graph-editor anchor position

UE-specific pin metadata can be included in an optional detail bag when the
query asks for `ue`, but compact pin declarations should stay readable.

Pin anchors are readback metadata. They are useful for visual context and wire
debugging, but they are not first-version patch mutation targets.

## Use Binding

```ts
interface UseBinding {
  kind: "use";
  symbol: string;
  source: { kind: "palette"; query: string } | { kind: "palette_entry"; id: string };
  context?: { kind: "component"; name: string } | { kind: "fromPin"; pin: PinRef };
  where?: BindingWhere[];
}
```

Bindings are adapter inputs, not parser guarantees. A Blueprint adapter should
resolve `palette` bindings through UE's palette/action database in the current
asset, graph, and context, then create nodes through the resulting spawner.

## Source Mapping

Every statement carries a source span with line and column numbers. This allows
parser, compiler, adapter, and Unreal errors to point back to the original LGL
text.
