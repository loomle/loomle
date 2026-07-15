# Asset Domain

## Scope

The asset domain is the entry point for finding and resolving UE assets before
entering graph, widget, material, PCG, or other domains.

The domain should be backed by UE Asset Registry when running inside an
initialized UE process. It should not load assets by default. Search should
return canonical asset identities and lightweight registry metadata that agents
can feed into later LGL queries or patches.

The TypeScript experiment still implements the earlier `find assets` text and
JSON shape. The language design below uses `assets`; parser, schema, formatter,
and in-memory adapter migration remains implementation work. UE Asset Registry
integration belongs to the UE-backed adapter.

## Basic Form

Asset query text is a statement list:

```lgl
query asset
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint"
with registryTags
order by score desc, path asc
page limit 10
```

Asset results are LGL object statements:

```lgl
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
doorFrame = asset(path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
```

The returned bindings should be directly usable by other domains:

```lgl
g = graph(domain: blueprint, asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
```

## Asset Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Asset binding | `name = asset(path: "...", type: nativeClassPath, metadata...)` | `door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")` |
| Registry tags | `registryTags: {key: value}` | `registryTags: {ParentClass: "/Script/Engine.Actor"}` |
| Domain list | `domains: [symbol, symbol]` | `domains: [asset, blueprint]` |
| Loaded flag | `loaded: boolean` | `loaded: false` |

`path` is the canonical object path. `type` is exactly
`FAssetData::AssetClassPath`; LGL does not translate it into an asset category.
Other named arguments are lightweight Registry metadata or deterministic LGL
adapter routing information.

Asset identity should stay explicit. Avoid positional asset constructors:

```lgl
asset("/Game/BP_Door.BP_Door", "/Script/Engine.Blueprint", false)
```

Use named arguments:

```lgl
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", loaded: false)
```

Normalized JSON:

```ts
interface Asset {
  kind: "asset";
  alias: string;
  path: string;
  type?: string;
  domains?: string[];
  loaded?: boolean;
  registryTags?: Record<string, Value>;
  score?: number;
}
```

## Query

Asset query syntax:

```lgl
query asset
assets ["text"]
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

The quoted text after `assets` is the primary asset search text. It should
perform deterministic contains-style search over asset name, object path,
native Asset Class Path, and selected registry tags. Exact structured filtering
belongs in `where`.

Field-level `~=` may still be used for advanced structured filters, but it is
not the normal way to express the main asset search text.

Supported `where` fields:

- `root`: package path root such as `/Game`
- `type`: exact native `FAssetData::AssetClassPath`
- `name`: asset name
- `path`: object path
- `registryTag.<key>`: Asset Registry tag filtering
- `loaded`: whether the asset is currently loaded

Supported `order by` keys:

- `score`
- `name`
- `path`
- `type`

Asset query has no `select` clause. The default result includes identity,
path, type, domains, and loaded state. `with registryTags` expands Asset
Registry tags.

The normalized JSON representation of the `assets` primary operation is not
specified yet. It must be reviewed with the shared query contract rather than
silently reusing the experiment's earlier `find = FindAssets` field. The asset
domain still owns allowed fields, expansions, sort keys, and pagination
defaults.

## Results

Asset search returns canonical asset bindings:

```lgl
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], score: 98)
doorFrame = asset(path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], score: 81)
```

Results should be deterministic. A default ranking policy can prefer:

1. exact object path or package name match
2. exact asset name match
3. asset name prefix
4. asset name contains
5. path segment contains
6. native Asset Class Path or registry tag match

Results should not load assets by default. Loading belongs to explicit adapter
behavior outside default search.

Normalized JSON:

```ts
interface AssetResult {
  kind: "asset_result";
  assets: Asset[];
}
```

The formatter turns asset result objects into canonical asset bindings.

## Registry Tags

`registryTags` means UE Asset Registry tags, not user-facing content labels.
They are lightweight key/value metadata exposed through `FAssetData`.

Example:

```lgl
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", registryTags: {ParentClass: "/Script/Engine.Actor"})
```

`{}` is allowed here because it is an inline value object. It is not a
structural block.

## UE Capability Boundary

UE Asset Registry can search and enumerate `FAssetData` by:

- package path
- package name
- object path
- class path
- recursive path and class filters
- tags and values exposed through Asset Registry

Content Browser text search adds UI-oriented text filtering on top of Asset
Registry data. LGL should use Asset Registry as the primary structured search
source and expose agent-friendly text filtering and deterministic ranking
without copying Content Browser's UI search language as the main interface.

## Relationship To Other Domains

Asset bindings are reusable references:

```lgl
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
g = graph(domain: blueprint, asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
```

Graph, widget, material, and PCG domains should not reimplement asset path
normalization, loading, or Asset Registry lookup. They should consume
canonical asset references resolved by the asset domain or by shared bridge
asset resolution utilities.

## Patch

The asset domain defines no Asset-specific Patch operations yet. An exact Asset
binding may still use the Core terminal `save` statement:

```lgl
patch door
save
```

Core resolves the Asset's owning Package and applies the same dirty-only,
non-interactive, source-control-aware behavior used when a Blueprint, Graph,
Widget, or another owned target requests `save`. The collection-level
`patch asset` target does not identify one Package and therefore cannot save;
the caller must use an exact Asset binding or another exact target with
resolvable persistent ownership.

Asset mutation such as create, rename, move, duplicate, delete, metadata edits,
redirector cleanup, and bulk package operations remains a separate Asset Tools
design. Core `save` does not introduce an `AssetPatch`, `save all`, directory
save, or any of those additional lifecycle operations.

## Adapter Boundary

Pure LGL normalization may:

- normalize asset query clauses into a structured query object
- preserve asset bindings as references for graph, widget, material, and PCG
  domains
- normalize `registryTag.<key>` conditions into structured field references
- preserve Core `save` terminal statement text for an exact Asset target

Pure LGL normalization must not:

- scan the Asset Registry
- load assets
- resolve redirectors
- decide whether an asset can be opened as a graph or widget
- compute search ranking
- validate UE class paths
- resolve Package ownership, Source Control state, external packages, or
  execute `save`

The adapter or bridge owns those UE-dependent responsibilities.
