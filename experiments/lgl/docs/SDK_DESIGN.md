# LGL SDK Design Draft

## Intent

LGL should be a TypeScript SDK for graph operations, not a public Graph IR
project. The SDK owns parsing, validation, adapter dispatch, diagnostics, and
result formatting. Agent-facing callers should think in terms of query, patch,
and the LGL text they send. Palette discovery and palette binding are LGL query
and patch language features, not separate SDK methods. Full graph snapshots are
internal cache/offline primitives, not a public SDK method or normal agent read
path.

Internal data structures may exist, but they are implementation details. The
contract is the SDK API plus the LGL text formats it accepts and returns.

## Facade

```ts
interface Sdk {
  query(text: LglText): Promise<TextResult>;
  patch(text: LglText): Promise<TextResult>;
}
```

The SDK facade accepts self-describing LGL text. Domain, asset, graph, query
shape, patch target, palette bindings, and dry-run intent live in LGL text, not
in side parameters.

## Object Model

The parsed object model is documented separately in
[`OBJECT_MODEL.md`](OBJECT_MODEL.md). The top-level type is:

```ts
type LglObject = Graph | Query | Patch | Palette;
```

The confirmed model currently covers `Target`, `Graph`, `Node`, `Edge`, `Pin`,
layout readback, query, patch, palette, and values.

## LGL Scope

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find nodes where type = PrintString
```

An empty query body requests a compact full graph snapshot:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

Large full-graph results may be written to cache files by tools or adapters,
with the SDK result returning a small LGL reference and diagnostics.

The LGL header selects the operation and the full graph reference. The reference
maps to `Target.domain`, `Target.asset`, and `Target.graph`. Future domains can
use the same pattern:

```txt
query material("/Game/M_Wood"/MaterialGraph)
```

A graph id is scoped by the asset path. It is a Loomle graph reference id, not a
global UE object id:

```txt
query blueprint("/Game/BP_Door"/id("graph-id"))
```

## Pipeline

The SDK pipeline is:

```txt
LGL text
  -> parser
  -> parsed LglObject
  -> normalizer
  -> normalized LglObject
  -> adapter/resolver
  -> ObjectResult
  -> formatter
  -> TextResult
```

Parser responsibilities:

- parse text into the JSON-safe `LglObject` model
- parse target headers, bindings, graph lines, pin declarations, edges, query
  statements, patch operations, values, and calls
- attach source spans for diagnostics when possible
- report syntax errors

Parser must not inspect target domains, palette entries, graph schemas, pins, or
native graph state.

Normalizer responsibilities:

- convert syntax sugar that does not require target-domain knowledge
- reject structurally invalid LGL that parsed but violates stable language rules
- normalize bare patch edge lines into `Connect`
- validate pin-chain shape, such as requiring middle segments to be
  `node.input/output`
- validate first-version patch structure, such as `add` connecting at most one
  side and `insert` using a two-sided chain

Normalizer must not consult palette databases, graph schemas, default exec pin
rules, or native graph state. Future compact forms such as positional arguments,
palette positional calls, or omitted fields may be normalized here only when the
rewrite is pure LGL syntax. Any rewrite that needs schema knowledge belongs in
an adapter or should remain unsupported in the stable form.

Adapter/resolver responsibilities:

- route by `Target.domain`
- resolve palette bindings and graph-domain names
- validate node types, fields, pins, and pin directions
- validate graph-state-dependent operations such as `insert`
- compute dry-run changes through the same path used by real mutation
- apply mutations when dry run is not requested

## RPC Boundary

For Unreal Engine, the SDK and UE editor communicate through an RPC boundary:

```txt
Agent / TypeScript SDK
  -> normalized LglObject JSON over RPC
  -> UE Bridge C++
  -> UE Editor APIs
```

The RPC boundary carries normalized `LglObject` JSON values in both directions,
not raw LGL text. C++ bridge code should not implement the LGL parser or
formatter. Parsing, source mapping, pure LGL normalization, and LGL text
formatting stay in TypeScript.

The TypeScript side owns:

- LGL text parsing
- pure LGL normalization
- structural validation that does not require target state
- adapter routing by `Target.domain`
- converting bridge responses into agent-facing LGL results and diagnostics

The UE Bridge side owns everything that depends on real UE editor state or UE
APIs:

- asset and graph resolution
- palette query and palette entry resolution
- node spawner execution
- node type, field, pin, and direction validation
- current graph edge lookup
- `insert` old-edge validation
- connect and disconnect legality through UE graph schema
- transactions, undo, asset dirtying, reconstruction, and layout mutation
- dry-run planning through the same path used by real mutation
- applying graph edits

This keeps LGL parsing portable while preserving UE as the source of truth for
Blueprint semantics.

## JSON Schema Contract

The normalized `LglObject` model should be maintained as JSON Schema. The
schema is the cross-language contract between TypeScript and C++:

```txt
LGL text
  -> TypeScript parser/normalizer
  -> LglObject JSON
  -> JSON Schema validation
  -> RPC
  -> C++ JSON Schema validation/deserialization
  -> UE adapter work
  -> LglObject JSON response
  -> JSON Schema validation
  -> TypeScript formatter
  -> LGL text
```

TypeScript owns parser, normalizer, formatter, and developer ergonomics. C++ owns
lightweight object-model structs/codecs and UE adapter behavior. Both sides
conform to the same JSON Schema.

The first schema should cover:

- `LglObject`
- `Target` and `GraphRef`
- `Graph`, `Node`, `Pin`, `Edge`, and layout
- `Value`, `Name`, `Binding`, and `Call`
- `Palette` and palette bindings
- `Query`, `Find`, `Condition`, and details
- `Patch`, `Op`, and pin chains
- `Diagnostic` and result envelopes

Human-readable design remains in [`OBJECT_MODEL.md`](OBJECT_MODEL.md). The JSON
Schema is the machine-verifiable source for RPC compatibility once added.

Schema maintenance should be schema-first for the RPC contract. The planned
machine contract is:

```txt
schema/lgl-object.schema.json
```

It should cover `LglObject`, `ObjectResult`, and `Diagnostic` once complete.
The first checked-in draft may grow incrementally from foundation types such as
`Target`, `GraphRef`, `Value`, `PinRef`, and `Edge`. TypeScript types may be
generated from the schema or checked against it. C++ may use generated code
later, but the first C++ bridge can use lightweight hand-written structs and
codecs as long as boundary JSON is validated against the same schema.

Suggested validation assets:

```txt
fixtures/object/graph-begin-delay-print.json
fixtures/object/patch-insert-delay.json
fixtures/object/palette-print-string.json
```

The important invariant is that TypeScript normalized objects and C++ response
objects both validate against the same JSON Schema. Human-readable docs explain
the model; the schema enforces cross-language compatibility.

## Results

SDK methods return agent-facing LGL text and diagnostics after formatting bridge
responses. Counts, summaries, alias mappings, patch changes, and debug details
should be expressed inside returned LGL text when needed, not as parallel public
SDK fields.

```ts
type LglText = string;
interface TextResult { text?: LglText; diagnostics: Diagnostic[]; }
interface ObjectResult { object?: LglObject; diagnostics: Diagnostic[]; }
```

`ObjectResult` is the adapter/RPC result shape. `TextResult` is the public SDK
result after formatting `ObjectResult.object` back to LGL text. These result
types and diagnostics are defined in [`OBJECT_MODEL.md`](OBJECT_MODEL.md).

A patch response can return an updated compact `graph` snippet around the
changed nodes. Created aliases, resolved ids, changed links, and layout moves
should be visible in that LGL text when they matter to the next agent action.
The public SDK result shape remains the same.

## Diagnostics

Diagnostics must be teachable. They should point at LGL source spans and tell
the caller what to do next:

The `Diagnostic` type is defined in [`OBJECT_MODEL.md`](OBJECT_MODEL.md).

Examples:

- `unbound_palette_binding`: add a `Name = palette({id: "entry-id"})` binding from a palette query result.
- `ambiguous_palette_query`: refine the `find palette entry` query before patching.
- `unknown_pin`: run `find node <name> with pins`.
- `unsafe_chain`: write explicit pin edges.

## Adapter Contract

```ts
interface Adapter {
  domain: string;
  query(object: Query): Promise<ObjectResult>;
  patch(object: Patch): Promise<ObjectResult>;
}
```

Adapters own domain semantics:

- palette binding resolution
- palette/action lookup
- node creation path
- pin name normalization
- default exec pin rules
- query execution
- mutation validation, change computation, dry-run, and apply
- mapping native errors into LGL diagnostics

## Dry Run

Dry-run intent should be expressed in LGL, not in SDK options. `patch` must
follow Loomle's mutation dry-run contract when the document requests dry run:
parse, resolve, validate, and compute changes through the same path used by real
mutation, then stop before applying changes.

## Tool Wrapping

MCP and CLI tools should be thin wrappers around the SDK. They should not
reimplement parsing, query behavior, palette resolution, or patch execution.
