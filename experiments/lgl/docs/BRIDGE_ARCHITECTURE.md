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

The current milestone registers and implements only `lgl.query`. `lgl.patch`
is the next mutation milestone after query readback is working against live UE
state.

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

## First Milestone

The first bridge milestone should be small:

- register `lgl.query`
- accept normalized query object JSON
- support `GraphTarget` objects where `target.domain = "blueprint"`
- resolve Blueprint asset and graph references
- return a compact graph snippet for an empty query
- support `find nodes where name = <name> with pins, defaults`
- return actionable diagnostics for malformed objects, unsupported domains,
  missing assets, missing graphs, unknown nodes, and ambiguous nodes

Patch, palette mutation, path queries, surrounding queries, and non-Blueprint
domains should build on the same core/adapter/service boundaries after this
path works.
