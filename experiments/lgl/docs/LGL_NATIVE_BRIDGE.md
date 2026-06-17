# LGL-Native Bridge Architecture

## Intent

LGL should become the agent-facing graph operation layer, not just a thin text
wrapper around existing low-level graph edit commands.

The existing Loomle UE bridge remains the main source of practical knowledge:
it proves which UE APIs are needed, which graph operations are difficult, and
where agent workflows currently become verbose. The new design should use that
experience as reference material, not as a runtime dependency.

The target architecture is:

```txt
Agent / MCP / CLI
  -> LGL text
  -> TypeScript parser, formatter, diagnostics
  -> normalized LglObject JSON
  -> UE bridge LGL RPC
  -> UE graph-domain adapter
  -> ObjectResult JSON
  -> TypeScript formatter
  -> LGL text + diagnostics
```

The UE bridge receives normalized, schema-valid `LglObject` JSON. It does not
parse raw LGL text.

## Why Not Only Wrap Existing Tools

Wrapping existing tools with LGL would improve syntax, but it would not fully
fix the underlying complexity:

- Agent-facing operations would still be shaped by backend edit command batches.
- The adapter would need to translate compact LGL into many legacy public tool
  calls instead of one coherent graph operation contract.
- Dry-run, diagnostics, snippets, and cache behavior would remain scattered
  across tools.
- The public surface would keep growing around implementation details rather
  than graph intent.

The LGL-native bridge should make query and patch the primary graph operations.
Existing lower-level bridge capabilities can remain as internal mechanisms,
debug tools, or migration aids.

The LGL-native path must not call existing public graph tool implementations as
its backend. If old code contains useful UE API logic, move the relevant logic
into LGL-native modules with LGL-native object boundaries.

## Non-Goals

- Do not make the UE bridge parse LGL text.
- Do not replace UE graph schemas, palette/action databases, or node spawners.
- Do not invent a Blueprint graph model separate from UE.
- Do not implement LGL by translating into existing public graph edit tools.
- Do not use old bridge command structs as the LGL bridge's internal IR.
- Do not remove existing Loomle graph tools before the LGL path proves itself.
- Do not make full graph snapshots the normal agent read path.

## Public MCP Surface

The normal agent-facing surface should be small:

```txt
lgl.query
lgl.patch
lgl.schema_inspect
```

Domain aliases may be useful when the target domain is obvious:

```txt
blueprint.lgl.query
blueprint.lgl.patch
```

The public inputs are LGL text documents:

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find node branch with pins, defaults
```

```txt
patch blueprint("/Game/BP_Door"/EventGraph) dry run

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

The TypeScript side parses these documents and sends normalized JSON to the UE
bridge.

## UE Bridge RPC Surface

The bridge-facing RPC should be object based:

```txt
lgl.object.query
lgl.object.patch
lgl.object.schema
```

Request shape:

```ts
interface LglObjectRequest {
  object: Query | Patch;
}
```

Response shape:

```ts
interface ObjectResult {
  object?: LglObject;
  diagnostics: Diagnostic[];
}
```

The request and response objects must validate against
`schema/lgl-object.schema.json`. TypeScript generated types and C++ codecs
should both follow that schema.

## Bridge Module Boundaries

The LGL-native bridge should separate these layers:

```txt
RPC endpoint
  -> schema validation / object decoding
  -> adapter dispatch by Target.domain
  -> domain adapter query/patch
  -> schema validation / object encoding
  -> RPC response
```

The RPC endpoint owns:

- request envelope validation
- schema validation at the boundary
- target domain dispatch
- top-level diagnostic conversion for malformed requests
- response envelope assembly

The domain adapter owns graph semantics.

## Adapter Interface

Conceptual C++ interface:

```cpp
class ILglGraphAdapter
{
public:
    virtual FString Domain() const = 0;
    virtual FLglObjectResult Query(const FLglQuery& Query) = 0;
    virtual FLglObjectResult Patch(const FLglPatch& Patch) = 0;
};
```

Adapters should not receive raw LGL text. They receive decoded object-model
values such as `FLglQuery` and `FLglPatch`.

Adapters own:

- asset and graph resolution
- graph snapshot/snippet readback
- palette/action lookup
- palette entry id resolution
- node creation path
- node, field, pin, direction, and link validation
- graph-state-dependent validation such as `insert`
- transaction, undo, dirtying, reconstruction, and compile-related diagnostics
- mutation planning and application

## Blueprint Adapter Responsibilities

The Blueprint adapter should use UE as the source of truth:

- Resolve Blueprint assets and graph references.
- Read graph nodes, pins, links, layout, and stable node identities from UE.
- Query Blueprint action/palette databases.
- Resolve stable palette ids into native creation actions.
- Execute UE node spawners for node creation.
- Validate links through UE graph schema rules.
- Apply mutations inside UE transactions.
- Mark assets dirty and trigger reconstruction when required.
- Return diagnostics that tell the agent what to query or change next.

Existing Loomle bridge code for palette lookup, graph inspection, graph edits,
node reconstruction, and diagnostics should be read as a UE behavior reference.
The new Blueprint adapter should be implemented in LGL-native modules. Small
pieces of UE API logic may be moved across when useful, but the new path should
not directly call old tool implementations or preserve old command payloads.

## Query Execution

Query execution should return small LGL object snippets by default.

Supported first targets:

- Empty query: compact graph snapshot.
- `find nodes`: node snippets, optionally with pins/defaults/layout.
- `find node`: one-node graph snippet.
- `find path`: path graph snippet.
- `find surrounding`: local context graph snippet.
- `find palette entry`: palette result document.

Large graph snapshots should use cache/workspace file references rather than
large inline responses when they would be inefficient for agent context.

## Patch Execution

Patch execution should follow a single path for dry-run and apply:

```txt
decode object
resolve target graph
resolve bindings and palette ids
validate node specs, fields, pins, links, and graph state
plan changes
if dryRun:
  return plan/snippet diagnostics without applying
apply UE transaction
reconstruct/dirty as required
return changed graph snippet + diagnostics
```

The `dryRun` flag lives inside the `Patch` object. It should not be duplicated
as a side option.

Patch results should show what matters for the next agent action:

- created aliases and stable ids
- changed links
- moved nodes
- updated field/default values
- diagnostics and suggested follow-up queries

The result should still fit the `ObjectResult` shape.

## Diagnostics

Diagnostics must be actionable and safe across the RPC boundary:

```ts
interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  span?: SourceSpan;
  suggestion?: string;
}
```

TypeScript parser diagnostics can include source spans. UE diagnostics may omit
spans when the error comes from graph state rather than text position.

Good diagnostics should tell the agent the next useful action:

- `unknown_pin`: run `find node <name> with pins`.
- `unbound_palette_binding`: run `find palette entry ...` and bind the returned
  id.
- `missing_insert_edge`: query the path or surrounding graph before inserting.
- `ambiguous_palette_query`: refine the palette query with structured
  constraints.

## Schema And C++ Object Model

`schema/lgl-object.schema.json` is the machine contract.

TypeScript already generates object-model types from the schema. The C++ bridge
has two viable first steps:

1. Hand-written lightweight structs/codecs validated against the schema at the
   RPC boundary.
2. Generated or semi-generated C++ codecs once the object model stabilizes.

The first implementation can use hand-written structs if boundary validation
prevents drift.

## Migration Strategy

Do not rewrite the entire bridge at once, and do not implement the new path as a
wrapper over the old public tools.

Recommended sequence:

1. Add object-model decode/encode and schema validation utilities in new
   LGL-native files.
2. Add `lgl.object.query` with a small Blueprint adapter path implemented
   directly against UE APIs for:
   - empty query
   - `find node`
   - `find palette entry`
3. Add `lgl.object.patch` dry-run for one operation:
   - `insert begin.Then -> delay.Exec/Completed -> print.Exec`
4. Use old bridge behavior as an oracle for comparison tests, not as a runtime
   dependency.
5. Add apply mode for the same patch path.
6. Add compile/diagnostic feedback integration.
7. Keep existing public graph tools available until the LGL path handles common
   agent workflows better.

## Open Questions

- Which JSON Schema validation library or strategy should the C++ bridge use?
- Should MCP expose generic `lgl.query`/`lgl.patch`, domain-specific aliases, or
  both?
- How should large graph snapshots be cached and referenced?
- What is the stable palette id format for context-sensitive Blueprint actions?
- Which patch result snippets are most useful after apply?
- Which old bridge behaviors should become comparison tests for the new adapter?
- Which small UE API helper logic is worth moving into LGL-native modules?

## Acceptance Criteria For The First Spike

The first UE-backed spike should prove:

- A TypeScript `query blueprint(...)` document can become normalized JSON and
  reach the UE bridge.
- The UE bridge can validate/decode the object and dispatch to a Blueprint
  adapter.
- The adapter can return a schema-valid `ObjectResult`.
- TypeScript can format the result back to LGL text.
- A dry-run patch can resolve palette/node/pin state through UE and return
  useful diagnostics or a changed snippet without mutating the asset.
