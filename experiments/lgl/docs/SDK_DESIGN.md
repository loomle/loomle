# LGL SDK Design

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
interface Lgl {
  query(text: LglText): Promise<TextResult>;
  patch(text: LglText): Promise<TextResult>;
}

declare function createLgl(options?: CreateLglOptions): Lgl;
```

The SDK facade accepts self-describing LGL text. Domain, asset, graph, query
shape, patch target, palette bindings, and dry-run intent live in LGL text, not
in side parameters.

Call sites should normally hold this facade as `lgl`:

```ts
const lgl = createLgl({ adapters: [blueprintAdapter] });
await lgl.query(text);
await lgl.patch(text);
```

## Text And Object Model

The SDK public contract is LGL text. Agent-facing text has three top-level
kinds:

- object text
- query text
- patch text

`Palette` is not a top-level text kind. It is discovery data or a patch binding
source inside domains that expose creation entries.

The normalized JSON model is internal to the SDK, adapters, schemas, and RPC
boundary. Graph, asset, blueprint, widget, and future domains define their own
normalized object shapes in domain documents. Those domain sections are the
target design source. The current [`OBJECT_MODEL.md`](OBJECT_MODEL.md) remains
a graph-first implementation reference until the schema migrates to the current
domain design.

## LGL Scope

The target text forms are documented by the overview, language core, and domain
docs:

- [`OVERVIEW.md`](OVERVIEW.md): top-level mental model, text kinds, domains,
  and palette positioning.
- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared statement, constructor, value,
  reference, query, patch, and normalization syntax.
- [`DOMAINS.md`](DOMAINS.md): domain document contract and normalization
  boundary.
- [`domains/graph.md`](domains/graph.md): graph text, pins, edges, queries,
  patches, palette creation entries, and normalized JSON.
- [`domains/asset.md`](domains/asset.md): asset discovery and reusable asset
  references.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint class, member, and
  component structure.
- [`domains/widget.md`](domains/widget.md): widget tree constructors, slots,
  queries, and patches.
- [`notes/graph-migration.md`](notes/graph-migration.md): migration notes from
  the current graph-first implementation.

## Pipeline

The SDK pipeline is:

```txt
LGL text
  -> parser
  -> parsed text document
  -> pure normalizer
  -> normalized domain JSON
  -> adapter or RPC
  -> normalized result JSON
  -> formatter
  -> LGL object text plus diagnostics
```

Parser responsibilities:

- parse LGL text into a structured text document
- parse bindings, constructor calls, references, values, domain statements,
  query clauses, patch operations, and sugar statements
- attach source spans for diagnostics when possible
- report syntax errors

Parser must not inspect target domains, palette entries, graph schemas, widget
classes, Blueprint members, pins, or native UE state.

Normalizer responsibilities:

- convert syntax sugar that does not require target-domain knowledge
- reject structurally invalid LGL that parsed but violates stable language rules
- lower canonical text into normalized domain JSON
- preserve source mapping for diagnostics where practical

Normalizer must not consult palette databases, graph schemas, default exec pin
rules, UMG class metadata, Blueprint member tables, or native UE state. Compact
forms may be normalized here only when the rewrite is pure LGL syntax. Any
rewrite that needs UE or domain-state knowledge belongs in an adapter or should
remain unsupported in the stable form.

Adapter/resolver responsibilities:

- route by normalized domain target
- resolve domain references, asset references, palette bindings, and creation
  entries
- validate domain objects and operations against real domain state
- validate graph-state-dependent operations such as graph `insert`
- compute dry-run changes through the same path used by real mutation
- apply mutations when dry run is not requested

The first implementation checkpoint is a minimal TypeScript-only loop:
parse representative LGL documents into normalized graph-domain JSON values,
validate them against `schema/lgl-object.schema.json`, format them back to LGL
text, and parse the formatted text again. This checkpoint intentionally does
not invoke UE adapters.

The in-memory graph adapter is a test fixture for this checkpoint. It exercises
the adapter contract and basic query/patch result flow using `Graph` objects,
but it must not become a replacement model for Blueprint, Material, PCG, or UE
graph semantics.

Graph adapter examples include resolving palette bindings, validating node
types, validating pins and pin directions, checking existing edges for
`insert`, and applying graph schema connect/disconnect legality.

## RPC Boundary

For Unreal Engine, the SDK and UE editor communicate through an RPC boundary:

```txt
Agent / TypeScript SDK
  -> normalized domain JSON over RPC
  -> UE Bridge C++
  -> UE Editor APIs
```

The RPC boundary carries normalized domain JSON values in both directions, not
raw LGL text. C++ bridge code should not implement the LGL parser or
formatter. Parsing, source mapping, pure LGL normalization, and LGL text
formatting stay in TypeScript.

The TypeScript side owns:

- LGL text parsing
- pure LGL normalization
- structural validation that does not require target state
- adapter routing by normalized domain target
- converting bridge responses into agent-facing LGL results and diagnostics

The UE Bridge side owns everything that depends on real UE editor state or UE
APIs:

- asset, Blueprint, graph, widget, and other domain resolution
- palette query and creation-entry resolution
- graph node spawner execution
- graph node type, field, pin, and direction validation
- current graph edge lookup
- graph `insert` old-edge validation
- graph connect and disconnect legality through UE graph schema
- transactions, undo, asset dirtying, reconstruction, and editor position
  mutation
- dry-run planning through the same path used by real mutation
- applying graph edits

This keeps LGL parsing portable while preserving UE as the source of truth for
Blueprint semantics.

The proposed bridge architecture is documented in
[`LGL_NATIVE_BRIDGE.md`](LGL_NATIVE_BRIDGE.md).

## JSON Schema Contract

The normalized domain JSON model is maintained as JSON Schema. The schema is
the cross-language contract between TypeScript and C++:

```txt
LGL text
  -> TypeScript parser/normalizer
  -> normalized domain JSON
  -> JSON Schema validation
  -> RPC
  -> C++ JSON Schema validation/deserialization
  -> UE adapter work
  -> normalized domain JSON response
  -> JSON Schema validation
  -> TypeScript formatter
  -> LGL text
```

TypeScript owns parser, formatter, developer ergonomics, and generated
object-model types. C++ owns lightweight object-model codecs and UE adapter
behavior. Both sides must conform to the same JSON Schema.

The machine contract is:

```txt
schema/lgl-object.schema.json
```

The current schema covers the graph-first implementation contract. The target
schema should cover normalized domain JSON, result envelopes, and diagnostics
for graph, asset, blueprint, widget, and future domains. Accepted fixtures
should validate representative object, query, patch, palette/creation-entry,
edge, and diagnostic objects. Rejected fixtures should cover contract
boundaries such as required fields, closed object shapes, enum values, mutually
exclusive fields, and reserved discriminators.

TypeScript object-model types are generated from this schema into
`src/generated/lgl-object-schema.ts`. Public SDK facade types such as `Lgl`,
`Adapter`, and `CreateLglOptions` remain hand-written.

C++ may use generated code later, but the first C++ bridge can use lightweight
hand-written structs and codecs as long as boundary JSON is validated against
the same schema.

Suggested validation assets:

```txt
fixtures/object/graph-begin-delay-print.json
fixtures/object/patch-insert-delay.json
fixtures/object/palette-print-string.json
fixtures/object-invalid/patch-missing-dry-run.json
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
interface ObjectResult { object?: unknown; diagnostics: Diagnostic[]; }
```

`ObjectResult` is the adapter/RPC result shape. `TextResult` is the public SDK
result after formatting `ObjectResult.object` back to LGL text. The concrete
normalized object type is domain-owned and schema-validated.

A patch response can return an updated compact `graph` snippet around the
changed nodes. Created aliases, resolved ids, changed links, and editor
position changes should be visible in that LGL text when they matter to the
next agent action. The public SDK result shape remains the same.

## Diagnostics

Diagnostics must be teachable. They should point at LGL source spans and tell
the caller what to do next:

The shared `Diagnostic` model is defined in
[`LANGUAGE_CORE.md`](LANGUAGE_CORE.md).

Examples:

- `unbound_palette_binding`: add a `Name = palette(id: "entry-id")`
  binding from a palette query result.
- `ambiguous_palette_query`: refine the `find palette entry` query before patching.
- `unknown_pin`: query the graph node or creation entry with pins:

```lgl
query g
find nodes
where name = print
with pins

query g
find palette entry "Print String"
with pins
```

- `unsafe_chain`: write explicit pin edges.

## Adapter Contract

```ts
interface Adapter {
  domain: string;
  query(object: unknown): Promise<ObjectResult>;
  patch(object: unknown): Promise<ObjectResult>;
}
```

Adapters own domain semantics:

- palette binding resolution
- palette/action lookup where the domain exposes creation entries
- creation paths
- reference and field-name normalization
- domain defaults that require real domain metadata
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
