# LGL Bridge Architecture

## Intent

The UE bridge executes normalized LGL Object JSON against live Unreal Editor
state. It does not parse LGL text. Text parsing, pure normalization, source
spans, and formatting remain in the TypeScript SDK.

The bridge exists to keep UE as the source of truth for assets, graphs, pins,
palette entries, target-state validation, mutation legality, transactions,
reconstruction, dirtying, and compile-related feedback. Structural schema
validation remains tied to the shared LGL object schema.

## Public RPC

The target bridge-facing LGL RPC surface is:

```txt
lgl.query
lgl.patch
```

The current milestone registers `lgl.query` and `lgl.patch`. `lgl.query`
performs live readback. `lgl.patch` starts with the shared patch boundary and
mutation result envelope; domain edit operations must return clear
`not_implemented` diagnostics until they share parse, resolve, validate, plan,
and apply paths.

`lgl.query` and `lgl.patch` receive an object envelope:

```json
{
  "object": {
    "kind": "query",
    "target": {}
  }
}
```

Responses are `ObjectResult` JSON:

```json
{
  "object": {},
  "diagnostics": []
}
```

`ObjectResult.object` is a normalized LGL object such as `graph`,
`asset_result`, `blueprint_result`, `widget_result`, or `palette_result`.
`graph` is not a separate response envelope.

The TypeScript SDK owns `lgl.schema()`. For now it is a minimal contract
inspection interface for the active LGL object schema. Domain capability
metadata needs separate design and does not require a bridge RPC or live UE
graph state.

## Flow

```txt
TypeScript SDK
  -> LGL text parse
  -> pure normalization
  -> LGL Object JSON
  -> lgl.query / lgl.patch
  -> UE bridge core
  -> target-domain adapter
  -> shared UE services
  -> ObjectResult JSON
  -> TypeScript formatter
```

## Layers

Bridge core owns:

- RPC method registration
- request envelope decoding
- structural validation against the LGL object contract
- adapter dispatch by `target.domain`
- result and diagnostic encoding
- response validation before returning across the RPC boundary

Domain adapters own target semantics:

- `asset` adapter: Asset Registry search and asset references
- `blueprint` adapter: Blueprint class, graph, member, component, and palette
  semantics
- `widget` adapter: WidgetBlueprint tree and palette semantics
- future adapters: Material, PCG, Niagara, Control Rig, and other UE graph
  systems

Shared UE services own reusable editor operations:

- asset resolution
- graph resolution
- graph readback
- graph patch planning and application
- palette search and palette entry resolution
- value and pin conversion
- transaction, dirtying, reconstruction, and compile feedback helpers

## Core Contracts

The bridge core should keep a narrow internal contract:

```txt
FLglObjectRequest
  object: normalized Query or Patch JSON

FLglObjectResult
  object?: normalized LGL object JSON
  diagnostics: diagnostics
  page?: pagination cursor
```

The first implementation may keep decoded objects as `FJsonObject` plus typed
accessors. It should still use the same contract boundaries that generated or
hand-written structs would use later:

```txt
RPC arguments
  -> FLglJsonCodec decode envelope
  -> FLglSchemaValidator validate language-level request object
  -> FLglAdapterRegistry dispatch
  -> shared domain capability validation
  -> FLglDomainAdapter
  -> FLglObjectResult
  -> FLglSchemaValidator validate language-level result
  -> FLglJsonCodec encode response
```

`FLglJsonCodec` owns JSON field extraction, required-field diagnostics, and
normalizing error paths such as `object.target.graph`. Domain adapters should
not hand-parse the RPC envelope.

`FLglSchemaValidator` owns structural contract validation. If a full JSON Schema
engine is not available in the first pass, the validator should still exist as
the single validation boundary and perform focused checks for supported objects.
The exit condition is to replace or extend it with validation against
`schema/lgl-object.schema.json`, not to scatter schema checks through adapters.

`FLglDiagnostics` owns stable diagnostic shape and helper constructors. Domain
code should return diagnostics through this helper so source paths,
suggestions, severity, and codes remain consistent. The bridge diagnostic shape
and code layers are defined in [`DIAGNOSTICS.md`](DIAGNOSTICS.md).

## Validation Boundaries

Validation has three layers. They must stay separate so domain adapters do not
repeat language checks and the SDK does not need UE state.

### SDK Language Validation

The SDK is the first validation boundary because it owns LGL text parsing and
pure normalization. It should reject text and normalized JSON that violate
language-level rules before any adapter or bridge call:

- top-level text kind is object, query, or patch
- query clauses have valid shape
- patch operations have valid shape
- constructor calls use named arguments
- references, values, arrays, and inline objects are syntactically valid
- `where` lowers to a valid `Condition` tree
- `with` lowers to a string detail list
- `order by` lowers to `{ key, direction }` entries
- `page` lowers to `limit` and/or `after`

SDK language validation must not decide whether a domain supports a `find`
kind, `where` field, detail expansion, order key, palette entry, graph node,
widget class, Blueprint member, or UE asset path. Those are domain or UE-state
questions.

### Bridge Core Language Validation

The bridge core repeats language-level object validation at the RPC boundary
because callers may bypass the SDK or send stale JSON. This is not domain
validation. It protects C++ code from malformed normalized objects:

- request envelope contains an `object`
- `object.kind` matches the RPC method
- `target.domain` exists
- `find`, when present, is an object with `kind`
- `where`, when present, is a structurally valid `Condition` tree
- `with`, when present, is an array of non-empty strings
- `orderBy`, when present, is an array of order entries
- `page`, when present, has valid `limit` and/or `after` fields
- result diagnostics and result envelopes have valid shape

Bridge core validation should be implemented once in `FLglSchemaValidator` or
a generated JSON Schema-backed validator. Domain adapters should not duplicate
recursive condition validation, order entry validation, page shape validation,
or RPC-envelope validation.

### Domain Capability Validation

After language validation succeeds, the target domain validates whether the
language-valid request is supported for that domain and current implementation
milestone.

Examples:

- `asset` may support `find assets`, `where root/type/class/name/path`,
  `with registryTags`, `order by score/name/path/type/class`, and cursor
  pagination.
- `blueprint` may support `find class`, `find members`, `find components`, and
  graph targets with graph-specific `find` forms.
- graph-owning domains may support `find nodes`, `find path`, and `find
  palette entry` with `with pins/defaults`.

The shared domain validator should consume a capability declaration rather than
forcing every adapter to hand-code the same rejections:

```cpp
struct FLglQueryCapabilities
{
    TSet<FString> FindKinds;
    TSet<FString> WhereFields;
    TSet<FString> WithDetails;
    TSet<FString> OrderKeys;
    bool bSupportsPageAfter = false;
    bool bSupportsCompare = false;
};
```

Adapters should supply capabilities and then execute already validated
requests:

```txt
decode
  -> bridge language validate
  -> dispatch domain
  -> shared capability validate
  -> domain execution
```

Capability validation returns diagnostics such as `unsupported_find`,
`unsupported_where_field`, `unsupported_detail`, `unsupported_order_key`, and
`unsupported_pagination`, but it should not report malformed-language errors.
Malformed-language errors belong to SDK and bridge core validation.
Concrete diagnostic objects should follow [`DIAGNOSTICS.md`](DIAGNOSTICS.md),
including the `language.*`, `capability.*`, `resolution.*`, and `validation.*`
code prefixes.

## Adapter Contract

Adapters are registered by domain:

```cpp
class ILglDomainAdapter
{
public:
    virtual FString GetDomain() const = 0;
    virtual FLglObjectResult Query(const FLglQueryRequest& Request) = 0;
    virtual FLglObjectResult Patch(const FLglPatchRequest& Request) = 0;
};
```

The concrete C++ signatures may differ, but the semantic contract should not:

- adapter dispatch is based only on `target.domain`
- adapters receive already decoded and structurally validated request objects
- adapters may reject unsupported domain target shapes, query forms, or patch
  ops through shared capability validation where possible
- adapters return normalized LGL result objects, not formatted text
- adapters do not call public legacy tool handlers

Graph targets are handled by the owning domain adapter. For example,
`target.domain = "blueprint"` with a graph reference dispatches to the
Blueprint adapter, which may delegate graph read and patch mechanics to shared
services.

## Dispatch Rule

Adapters are dispatched by `target.domain`.

Graph behavior is not a standalone dispatch target in the bridge. A graph always
belongs to a UE domain such as Blueprint, Material, PCG, or WidgetBlueprint. The
domain adapter may delegate common graph read and patch work to shared graph
services.

## Code Shape

Recommended C++ layout:

```txt
Private/Lgl/
  LglModule.*
  LglDomainAdapter.*
  LglAdapterRegistry.*
  LglObjectModel.*
  LglJsonCodec.*
  LglSchemaValidator.*
  LglDiagnostics.*
  LglResult.*

Private/Lgl/Services/
  LglAssetRegistry.*
  LglAssetResolve.*
  LglGraphResolve.*
  LglGraphRead.*
  LglGraphPatch.*
  LglPalette.*

Private/Lgl/Blueprint/
  LglBlueprintAdapter.*
  LglBlueprintQuery.*
  LglBlueprintPatch.*
  LglBlueprintDiagnostics.*
```

## Query Pipeline

`lgl.query` should use the same bridge core for every domain:

```txt
lgl.query
  -> decode ObjectRequest
  -> validate query language shape
  -> dispatch target.domain
  -> validate domain query capabilities
  -> adapter query
  -> validate ObjectResult
```

The Blueprint graph query adapter then owns:

```txt
GraphTarget
  -> resolve Blueprint asset
  -> resolve UEdGraph by name or id
  -> interpret find/where/with/page
  -> read UE graph state
  -> assemble normalized graph or palette_result object
```

An empty graph query returns a compact graph snippet. A constrained node query
returns a smaller graph snippet around matched nodes. Palette queries return
`palette_result`. All result objects must remain schema-valid and formatter
friendly.

## Patch Pipeline

`lgl.patch` should be designed before implementation even if it is not the
first milestone:

```txt
lgl.patch
  -> decode ObjectRequest
  -> validate patch language shape
  -> dispatch target.domain
  -> validate domain patch capabilities
  -> adapter patch
  -> validate ObjectResult
```

Domain patch adapters should follow Loomle's mutation dry-run contract:

1. Decode patch target, bindings, and ordered ops.
2. Resolve assets, graphs, members, palette ids, and shortcut constructors.
3. Build provisional created objects and their pins/properties when needed.
4. Resolve all references against existing state plus provisional state.
5. Validate legality through UE schemas and target state.
6. Build a patch plan.
7. If `dryRun`, return diagnostics and planned result without mutation.
8. Otherwise apply inside a UE transaction, mark assets dirty, reconstruct as
   needed, and return a normalized result.

Dry run is not a separate adapter path. It stops the same patch pipeline before
mutation.

## Milestones

Milestone 1 builds the complete bridge skeleton:

- keep `lgl.query` registered
- add the bridge core boundaries above
- add adapter registry and Blueprint adapter registration
- add request/result codec
- add schema validator boundary, even if initially focused on supported shapes
- add shared query capability validation before adding more domain query
  features
- keep `lgl.patch` as a planned target, not a registered working tool unless it
  returns a clear `not_implemented`

Milestone 2 confirms SDK language validation before expanding bridge domain
features:

- audit parser and normalizer rejection of malformed query, patch, condition,
  detail, order, and page shapes
- ensure in-memory adapters reject capability issues separately from language
  issues
- add missing SDK tests for language-level rejection and normalized object
  schema validation
- keep domain capability support out of `lgl.schema()` until separately
  designed

Milestone 3 implements UE-backed asset query:

- register an `asset` domain adapter
- query Asset Registry without loading assets by default
- support `find assets`, primary search text, supported `where` fields,
  `with registryTags`, ordering, and pagination
- return schema-valid `asset_result` objects
- expose shared asset registry and asset resolution services for later domains

Milestone 4 implements Blueprint graph query readback:

- accept normalized query object JSON
- support `GraphTarget` objects where `target.domain = "blueprint"`
- resolve Blueprint asset and graph references
- return a compact graph snippet for an empty query
- support `find nodes where name = <name> with pins, defaults`
- return actionable diagnostics for malformed objects, unsupported domains,
  missing assets, missing graphs, unknown nodes, and ambiguous nodes

Milestone 5 adds Blueprint palette discovery:

- support `find palette entry`
- use UE Action Menu and node spawners as the source of truth
- return `palette_result`
- support `with pins` and `with defaults` when UE can provide template details

Milestone 6 adds Blueprint graph patch dry run and mutation:

- support graph patch ops through one patch planning pipeline
- resolve shortcut constructors and palette ids
- validate one-shot add/connect and insert operations before mutation
- apply through UE transactions and reconstruction paths

Asset, widget, Material, PCG, and other domains should plug into the same core
after the Blueprint path proves the boundary.
