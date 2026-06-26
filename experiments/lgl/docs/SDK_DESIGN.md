# LGL SDK Design

## Intent

LGL is a TypeScript SDK for agent-facing UE text operations, not a public Graph
IR project. The SDK owns parsing, validation, adapter dispatch, diagnostics,
and result formatting. Callers use `query` and `patch` with LGL text. Palette
discovery and creation binding are language features, not separate SDK methods.

Internal data structures are implementation details. The contract is the SDK
API plus the LGL text it accepts and returns.

## Facade

```ts
interface Lgl {
  query(text: LglText): Promise<TextResult>;
  patch(text: LglText): Promise<TextResult>;
  schema(): Promise<SchemaResult>;
}

declare function createLgl(options?: CreateLglOptions): Lgl;
```

The SDK facade accepts self-describing LGL text. Domain, asset, graph, query
shape, patch target, creation entries, and dry-run intent live in LGL text, not
in side parameters.

Call sites should normally hold this facade as `lgl`:

```ts
const lgl = createLgl({ adapters: [blueprintAdapter] });
await lgl.query(text);
await lgl.patch(text);
await lgl.schema();
```

`schema` exposes the active LGL object contract and domain capabilities for
tooling. It is not a graph query, does not read UE assets, and does not require
a UE bridge RPC. The SDK should serve it from the local schema package and SDK
capability metadata.

## Text And Object Model

The SDK public contract is LGL text. Agent-facing text has three top-level
kinds:

- object text
- query text
- patch text

`Palette` is not a top-level text kind. It is discovery data or a patch binding
source inside domains that expose creation entries.

Normalized JSON is internal to the SDK, adapters, schemas, and RPC boundary.
Domain documents define the target object shapes.

## LGL Scope

The target text forms are documented here:

- [`OVERVIEW.md`](OVERVIEW.md): top-level mental model, text kinds, domains,
  and palette positioning.
- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared statement, constructor, value,
  reference, query, patch, and normalization syntax.
- [`DOMAINS.md`](DOMAINS.md): domain document contract and normalization
  boundary.
- [`domains/graph.md`](domains/graph.md): graph objects, queries, patches, and
  creation entries.
- [`domains/asset.md`](domains/asset.md): asset discovery and references.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint class, member, and
  component structure.
- [`domains/widget.md`](domains/widget.md): widget trees, slots, queries, and
  patches.

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

Parser:

- parse LGL text into a structured text document
- parse bindings, constructor calls, references, values, domain statements,
  query clauses, patch operations, and sugar statements
- attach source spans for diagnostics when possible
- report syntax errors

Parser must not inspect domains, palette entries, schemas, widget classes,
Blueprint members, pins, or UE state.

Normalizer:

- convert syntax sugar that does not require target-domain knowledge
- reject structurally invalid LGL that parsed but violates stable language rules
- lower canonical text into normalized domain JSON
- preserve source mapping for diagnostics where practical

Normalizer must not consult palette databases, graph schemas, UMG metadata,
Blueprint member tables, or UE state. It may rewrite only pure LGL syntax.

Adapter/resolver:

- route by normalized domain target
- resolve domain references, asset references, palette entries, and creation
  entries
- validate domain objects and operations against real domain state
- validate graph-state-dependent operations such as graph `insert`
- compute dry-run changes through the same path used by real mutation
- apply mutations when dry run is not requested

The TypeScript experiment exercises this pipeline with schema validation,
roundtrip tests, examples, and in-memory adapters for graph, asset, Blueprint,
and widget. These adapters are contract fixtures, not replacement models for
UE semantics.

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

TypeScript owns:

- LGL text parsing
- pure LGL normalization
- structural validation that does not require target state
- adapter routing by normalized domain target
- converting bridge responses into agent-facing LGL results and diagnostics

The UE Bridge owns everything that depends on real UE editor state or APIs:

- asset, Blueprint, graph, widget, and other domain resolution
- palette query and palette entry resolution
- graph node spawner execution
- graph node type, field, pin, and direction validation
- current graph edge lookup
- graph `insert` old-edge validation
- graph connect and disconnect legality through UE graph schema
- transactions, undo, asset dirtying, reconstruction, and editor position
  mutation
- dry-run planning through the same path used by real mutation
- applying graph edits

This keeps parsing portable while preserving UE as the source of truth.

Bridge architecture is tracked separately in
[`BRIDGE_ARCHITECTURE.md`](BRIDGE_ARCHITECTURE.md).

## JSON Schema Contract

Normalized domain JSON is maintained as JSON Schema. The schema is the
cross-language contract between TypeScript and C++:

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
object-model types. C++ owns codecs and UE adapter behavior. The machine
contract is:

```txt
schema/lgl-object.schema.json
```

The schema covers the shared envelope plus graph, asset, Blueprint, widget,
query, patch, palette-result, result, and diagnostic payloads. Fixtures should
cover representative accepted objects and rejected contract boundaries such as
required fields, closed object shapes, enum values, mutually exclusive fields,
and reserved discriminators.

TypeScript object-model types are generated from this schema into
`src/generated/lgl-object-schema.ts`. Public SDK facade types such as `Lgl`,
`Adapter`, and `CreateLglOptions` remain hand-written.

C++ may use generated code later, but the first bridge can use hand-written
structs and codecs as long as boundary JSON validates against the same schema.
Docs explain the model; the schema enforces cross-language compatibility.

## Results

SDK methods return LGL text and diagnostics after formatting bridge responses.
Counts, summaries, alias mappings, patch changes, and debug details should be
expressed inside returned LGL text when useful. Pagination cursors are separate
because they control the next query.

```ts
type LglText = string;
interface TextResult { text?: LglText; diagnostics: Diagnostic[]; page?: Page; }
interface ObjectResult { object?: LglObject; diagnostics: Diagnostic[]; page?: Page; }
interface SchemaResult { schema: unknown; diagnostics: Diagnostic[]; }
```

`ObjectResult` is the adapter/RPC shape. `TextResult` is the public SDK shape.
Concrete normalized object types are domain-owned and schema-validated.
`Graph` is one possible `ObjectResult.object`, alongside result objects such as
`AssetResult`, `BlueprintResult`, `WidgetResult`, and `PaletteResult`; it is
not a separate response envelope.

A patch response can return a compact updated snippet around changed objects.
Created aliases, resolved ids, changed links, and position changes should be
visible when they matter to the next agent action.

## Diagnostics

Diagnostics must be teachable. They should point at LGL source spans and tell
the caller what to do next:

The shared `Diagnostic` model is defined in
[`LANGUAGE_CORE.md`](LANGUAGE_CORE.md).

Examples:

- `unknown_palette_id`: use a `Name = node(palette: "entry-id")`
  creation template from a palette query result.
- `ambiguous_palette_query`: refine the `find palette entry` query before patching.
- `unknown_pin`: query the graph node or palette entry with pins:

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
  query(object: Query): Promise<ObjectResult>;
  patch?(object: Patch): Promise<ObjectResult>;
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
