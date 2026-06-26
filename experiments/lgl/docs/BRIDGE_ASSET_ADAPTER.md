# Asset Adapter

## Intent

The asset adapter executes LGL asset queries against Unreal's Asset Registry.
It is the first UE-backed read adapter because every other domain needs stable
asset resolution before it can work against Blueprint, WidgetBlueprint,
Material, PCG, or other UE objects.

The adapter should not load assets during default search. It should return
canonical asset identities and lightweight metadata that agents can feed into
later LGL queries and patches.

## Boundary

The TypeScript SDK accepts LGL text. The UE bridge accepts normalized query
object JSON:

```txt
query asset
find assets "Door"
where root = "/Game" and type = blueprint
with registryTags
page limit 50
```

Bridge object shape:

```json
{
  "kind": "query",
  "target": { "domain": "asset" },
  "find": { "kind": "assets", "text": "Door" }
}
```

The adapter returns `ObjectResult.object.kind = "asset_result"`.

## Responsibilities

The asset adapter owns:

- Asset Registry access through UE APIs
- package path and object path search
- class path filtering
- registry tag filtering
- deterministic text matching and ranking
- pagination over deterministic results
- conversion from `FAssetData` to normalized LGL `Asset`

It does not own:

- loading assets by default
- asset mutation
- graph, widget, material, or PCG semantics
- Content Browser UI search language compatibility

## First Scope

First supported query shape:

```txt
query asset
find assets ["text"]
where root = "/Game"
where type = blueprint
where class = "/Script/Engine.Blueprint"
with registryTags
order by score desc, path asc
page limit 50
page after "cursor"
```

The first implementation should support:

- empty `find assets`
- primary search text from `find assets "text"`
- `where root = "..."`
- `where type = <symbol>`
- `where class = "..."`
- `where name = "..."`
- `where path = "..."`
- `where registryTag.<key> = value`
- `where loaded = true|false`
- `and`, `or`, and `not` over supported fields
- `with registryTags`
- `order by score|name|path|type|class`
- cursor pagination with `page limit` and `page after`

Unsupported fields or expansions should return `unsupported_query`, not silently
ignore the clause.

## Result Mapping

Each `FAssetData` should map to normalized `Asset`:

```ts
interface Asset {
  kind: "asset";
  alias: string;
  path: string;
  type?: string;
  class?: string;
  domains?: string[];
  loaded?: boolean;
  registryTags?: Record<string, Value>;
  score?: number;
}
```

Mapping rules:

- `path` is the canonical object path.
- `class` is the UE asset class path.
- `type` is Loomle's stable domain-facing type such as `blueprint`,
  `widget`, `material`, or `pcg`.
- `domains` includes `asset` and any domain that can consume the asset.
- `loaded` reflects whether the asset object is currently loaded.
- `registryTags` is present only when requested with `with registryTags`.
- `score` is deterministic query ranking metadata.

Aliases should be deterministic, readable, and unique within the result page.

## Shared Service

Asset Registry work should live behind a shared service:

```txt
Private/Lgl/Services/
  LglAssetRegistry.*
```

The asset adapter uses this service directly. Blueprint, widget, material, and
PCG adapters should use the same service for asset resolution instead of
reimplementing path normalization, object lookup, loaded-state checks, or
registry tag access.

## Milestone Acceptance

The asset bridge milestone is complete when:

- `target.domain = "asset"` dispatches to the asset adapter.
- supported asset queries return schema-valid `asset_result` objects.
- default search does not load assets.
- unsupported find kinds, fields, expansions, or sort keys return diagnostics.
- pagination is deterministic and returns opaque cursors.
- Blueprint graph resolve can reuse the shared asset registry service.
