# LGL Object Model

> Status: current graph-first schema reference. This document describes the
> current `schema/lgl-object.schema.json` contract, not the target
> agent-facing language design. The target domain-oriented design is documented
> in [`OVERVIEW.md`](OVERVIEW.md), [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md), and
> [`domains/graph.md`](domains/graph.md). After schema migration, this document
> should be deleted or renamed as a legacy schema reference.

## Intent

This document explains the normalized LGL object model. It describes
`LglObject` values after text parsing and before adapter-specific validation or
bridge operation generation.

The model should stay graph-domain neutral. Unreal Blueprint details belong in
the Blueprint adapter, not in these core types.

The machine contract is `schema/lgl-object.schema.json`. TypeScript object-model
types are generated from that schema into
`src/generated/lgl-object-schema.ts`. This document is the human-readable guide;
the schema is the source of truth for compatibility.

## Top-Level Object

LGL refers to the text language. The parsed JSON-safe representation is called
`LglObject`. It has four forms:

```ts
type LglObject = Graph | Query | Patch | Palette;
```

The package is an independent LGL package, so variant types do not need an
`Lgl` prefix. Each variant carries a `kind` discriminator:

```ts
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

interface Palette {
  kind: "palette";
  target: Target;
  entries: PaletteBinding[];
}
```

The confirmed graph, query, patch, palette, and value models are below.

## Target

Every LGL form targets one graph:

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

`domain` is the graph domain, such as `blueprint`, `material`, or `pcg`. It is
not called `system` or `adapter`: the SDK may route a domain to an adapter, but
the LGL target describes the graph domain itself.

Examples:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
query blueprint("/Game/BP_Door"/id("graph-id"))
```

## Graph

`Graph` is the readback model for graph snapshots and graph snippets:

```ts
interface Graph {
  kind: "graph";
  target: Target;
  nodes: Node[];
  edges: Edge[];
  pins?: Pin[];
}
```

`pins` is optional because compact graph readback does not always include pin
details. Queries such as `find node branch with pins, defaults, layout` may
return pins.

## Node

```ts
interface Node {
  alias: string;
  id?: string;
  type: string;
  fields: Record<string, Value>;
  layout?: NodeLayout;
}

interface NodeLayout {
  at?: [number, number];
  size?: [number, number];
}
```

`alias` is the LGL-local node name. `id` is the target graph's stable node
identity when the target can provide one. `type` is a schema or palette
constructor symbol, not an editor display title.

`fields` are named schema fields. Positional node arguments are not part of the
stable model.

Example:

```txt
delay@A002: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
```

## Edge

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

Pin chains in LGL text are normalized into edges before adapter validation:

```txt
begin.Then -> delay.Exec/Completed -> print.Exec
```

normalizes to:

```ts
[
  { from: { node: "begin", pin: "Then" }, to: { node: "delay", pin: "Exec" } },
  { from: { node: "delay", pin: "Completed" }, to: { node: "print", pin: "Exec" } },
]
```

## Pin

```ts
interface Pin {
  node: string;
  name: string;
  type: string;
  direction: "in" | "out";
  value?: Value;
  layout?: PinLayout;
}

interface PinLayout {
  anchor?: [number, number];
}
```

`Pin.value` is the current pin value in LGL terms. The model does not expose a
UE-specific `defaultValue` field name.

Example:

```txt
delay.Duration: float in {1.0, anchor: [320, 72]}
```

## Query

`Query` is the model for read LGL:

```ts
interface Query {
  kind: "query";
  target: Target;
  find?: Find;
}
```

`find` is optional. An empty query body requests a compact full graph snapshot:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

The first query set stays intentionally small:

```ts
type Find =
  | FindNodes
  | FindNode
  | FindPath
  | FindSurrounding
  | FindPaletteEntry;

interface FindNodes {
  kind: "nodes";
  where?: Condition;
  with?: Detail[];
}

interface FindNode {
  kind: "node";
  node: string;
  with?: Detail[];
}

interface FindPath {
  kind: "path";
  from: PinRef;
}

interface FindSurrounding {
  kind: "surrounding";
  around: string;
  depth: number;
}

interface FindPaletteEntry {
  kind: "palette_entry";
  text?: string;
  where?: Condition;
}

type Detail = "pins" | "defaults" | "layout";
```

Examples:

```txt
find nodes where type = PrintString
find nodes where type = PrintString with pins, defaults, layout
find node branch with pins, defaults, layout
find path from begin.Then
find surrounding around branch depth 2
find palette entry "Print String"
find palette entry "Print" where function = "/Script/Engine.KismetSystemLibrary.PrintString"
```

Conditions are deliberately limited:

```ts
type Condition =
  | { kind: "eq"; field: string; value: Value }
  | { kind: "contains"; field: string; value: Value }
  | { kind: "and"; conditions: Condition[] };
```

This covers common query constraints such as:

```txt
where type = PrintString
where title contains "Print"
where function = "/Script/Engine.KismetSystemLibrary.PrintString"
```

The first version does not include `or`, grouping, arithmetic, or target-specific
query functions. Adapters may reject unsupported fields with diagnostics.

## Value

`Value` is the shared literal model used by node fields, pin values, palette
metadata, patch `set` values, and query conditions.

The first version keeps values small and domain neutral:

```ts
type Value =
  | null
  | boolean
  | number
  | string
  | Name
  | Value[]
  | { [key: string]: Value };

interface Name {
  kind: "name";
  name: string;
}
```

Strings and names are intentionally separate. A quoted value is a string
literal:

```txt
PrintString({InString: "Ready"})
```

```ts
{ InString: "Ready" }
```

An unquoted symbol is a `Name`:

```txt
SpawnActorFromClass({Class: BP_Projectile, CollisionHandlingOverride: AlwaysSpawn})
```

```ts
{
  Class: { kind: "name", name: "BP_Projectile" },
  CollisionHandlingOverride: { kind: "name", name: "AlwaysSpawn" },
}
```

The core model does not decide whether a name is a class, asset, enum value,
variable name, or graph-domain symbol. Adapters resolve names against the target
schema and graph domain.

## Palette

`Palette` is the readback model for palette query results. Patch bindings reuse
the same `PaletteBinding` type.

```ts
interface Palette {
  kind: "palette";
  target: Target;
  entries: PaletteBinding[];
}

interface PaletteBinding {
  name: string;
  entry: PaletteEntryRef;
  meta?: Record<string, Value>;
}

interface PaletteEntryRef {
  kind: "palette";
  id: string;
}
```

`name` is the constructor symbol used inside LGL. It should be stable within one
LGL value and should not be copied from editor display titles with spaces or
localization.

`entry.id` is the stable palette entry id returned by a palette query. Palette
calls use named object arguments:

```txt
palette({id: "entry-id"})
```

Palette query result:

```txt
palette blueprint("/Game/BP_Door"/EventGraph)

PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", title: "Print String", category: "Utilities/String"})
```

Patch binding:

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
```

Both parse to `PaletteBinding`. Palette query results commonly include `meta`;
patch bindings usually need only `name` and `entry`.

## Patch

`Patch` is the model for mutation LGL:

```ts
interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  bindings: Binding[];
  ops: Op[];
}
```

`bindings` are local `=` bindings. The object model does not split them into
palette bindings, node specs, and value aliases. Resolver phases classify them
later.

Examples:

```txt
Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
delay = Delay({Duration: 1.0})
message = "Game Started"
```

```ts
interface Binding {
  name: string;
  value: Expr;
}

type Expr =
  | Value
  | Call;

interface Call {
  kind: "call";
  callee: string;
  args: Record<string, Value>;
}
```

`palette({id: "entry-id"})` is represented as a call:

```ts
{
  name: "Delay",
  value: {
    kind: "call",
    callee: "palette",
    args: {
      id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay",
    },
  },
}
```

Node specs use the same call model:

```ts
{
  name: "delay",
  value: {
    kind: "call",
    callee: "Delay",
    args: { Duration: 1.0 },
  },
}
```

The first version of patch operations is:

```ts
type Op =
  | Set
  | Add
  | Insert
  | Connect
  | Disconnect
  | Remove
  | Move
  | Reconstruct;
```

Operation variant names do not carry an `Op` suffix. They are already scoped by
`Patch.ops: Op[]`.

```ts
interface Set {
  kind: "set";
  target: FieldRef;
  value: Expr;
}

interface FieldRef {
  node: string;
  field: string;
}

interface Add {
  kind: "add";
  node: string;
  connect?: Edge;
}

interface Insert {
  kind: "insert";
  node: string;
  chain: PinChain;
}

interface Connect {
  kind: "connect";
  chain: PinChain;
}

type Disconnect =
  | { kind: "disconnect"; edge: Edge; pin?: never }
  | { kind: "disconnect"; pin: PinRef; edge?: never };

interface Remove {
  kind: "remove";
  node: string;
}

type Move =
  | { kind: "move"; node: string; mode: "to"; at: [number, number] }
  | { kind: "move"; node: string; mode: "by"; delta: [number, number] };

interface Reconstruct {
  kind: "reconstruct";
  node: string;
  preserveLinks: boolean;
}
```

`add` may include at most one edge, because it can create a node and connect one
side. Two-sided replacement uses `insert`.

## Pin Chain

Pin chains preserve the compact text form while still naming every pin:

```ts
interface PinChain {
  segments: PinChainSegment[];
}

type PinChainSegment =
  | { kind: "pin"; pin: PinRef }
  | { kind: "through"; input: PinRef; output: PinRef };
```

Example:

```txt
begin.Then -> delay.Exec/Completed -> print.Exec
```

```ts
{
  segments: [
    { kind: "pin", pin: { node: "begin", pin: "Then" } },
    {
      kind: "through",
      input: { node: "delay", pin: "Exec" },
      output: { node: "delay", pin: "Completed" },
    },
    { kind: "pin", pin: { node: "print", pin: "Exec" } },
  ],
}
```

Adapters validate every expanded edge. `insert` additionally requires an
existing direct edge from the first pin to the last pin.

## Results

`ObjectResult` is the object-form result used at adapter and RPC boundaries:

```ts
interface ObjectResult {
  object?: LglObject;
  diagnostics: Diagnostic[];
}
```

`object` is optional so parse, validation, adapter, or bridge failures can
return diagnostics without fabricating an LGL object.

`TextResult` is the public TypeScript SDK result after formatting an
`ObjectResult` back to LGL text:

```ts
type LglText = string;

interface TextResult {
  text?: LglText;
  diagnostics: Diagnostic[];
}
```

`ObjectResult` and `Diagnostic` belong in the JSON Schema contract.
`TextResult` is a TypeScript-facing SDK convenience shape.

## Diagnostics

Diagnostics must be teachable and safe to send across the TypeScript/C++ RPC
boundary:

```ts
interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  span?: SourceSpan;
  suggestion?: string;
}

interface SourceSpan {
  line: number;
  column: number;
  length?: number;
}
```

`span` is optional. TypeScript parser diagnostics should provide spans when
possible. C++ adapter diagnostics may omit spans when the error comes from UE
state rather than a specific text location.
