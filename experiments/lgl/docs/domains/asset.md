# Asset Domain

## Scope

The asset domain is the entry point for finding and resolving UE assets before
entering graph, widget, material, PCG, or other domains.

The domain should be backed by UE Asset Registry when running inside an
initialized UE process. It should not load assets by default. Search should
return canonical asset identities and lightweight registry metadata that agents
can feed into later LGL queries or patches.

The TypeScript experiment implements the first asset query/readback loop:
`query asset`, `find assets`, canonical asset result bindings, schema
validation, formatter roundtrip, and an in-memory asset adapter. UE Asset
Registry integration remains adapter work.

## Basic Form

Asset query text is a statement list:

```lgl
query asset
find assets "door"
where root = "/Game" and type = blueprint
with registryTags
order by score desc, path asc
page limit 10
```

Asset results are LGL object statements:

```lgl
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: blueprint, class: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
doorFrame = asset(path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame", type: blueprint, class: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
```

The returned bindings should be directly usable by other domains:

```lgl
g = graph(domain: blueprint, asset: door, graph: EventGraph)
```

## Asset Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Asset binding | `name = asset(path: "...", type: symbol, metadata...)` | `door = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)` |
| Registry tags | `registryTags: {key: value}` | `registryTags: {ParentClass: "/Script/Engine.Actor"}` |
| Domain list | `domains: [symbol, symbol]` | `domains: [asset, blueprint]` |
| Loaded flag | `loaded: boolean` | `loaded: false` |

`path` is the canonical object path. Other named arguments are lightweight
metadata from Asset Registry or deterministic Loomle classification.

Asset identity should stay explicit. Avoid positional asset constructors:

```lgl
asset("/Game/BP_Door.BP_Door", blueprint, false)
```

Use named arguments:

```lgl
door = asset(path: "/Game/BP_Door.BP_Door", type: blueprint, loaded: false)
```

Normalized JSON:

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

## Query

Asset query syntax:

```lgl
query asset
find assets ["text"]
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

The quoted text after `find assets` is the primary asset search text. It should
perform deterministic contains-style search over asset name, object path, class,
type, and selected registry tags. Exact structured filtering belongs in `where`.

Field-level `~=` may still be used for advanced structured filters, but it is
not the normal way to express the main asset search text.

Supported first-pass `where` fields:

- `root`: package path root such as `/Game`
- `type`: LGL asset type such as `blueprint`, `material`, `widget`, or `pcg`
- `class`: UE class path when the caller needs exact UE class filtering
- `name`: asset name
- `path`: object path
- `registryTag.<key>`: Asset Registry tag filtering
- `loaded`: whether the asset is currently loaded

Supported first-pass `order by` keys:

- `score`
- `name`
- `path`
- `type`
- `class`

Asset query has no `select` clause. The default result includes identity,
path, type, domains, and loaded state. `with registryTags` expands Asset
Registry tags.

Normalized JSON:

```ts
type AssetQuery = Query<FindAssets>;

interface FindAssets {
  kind: "assets";
  text?: string;
}
```

`where`, `with`, `orderBy`, and `page` use the shared query model from the
language core. The asset domain validates allowed fields, expansions, sort
keys, and pagination defaults.

## Results

Asset search returns canonical asset bindings:

```lgl
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: blueprint, domains: [asset, blueprint], score: 98)
doorFrame = asset(path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame", type: blueprint, domains: [asset, blueprint], score: 81)
```

Results should be deterministic. A first ranking policy can prefer:

1. exact object path or package name match
2. exact asset name match
3. asset name prefix
4. asset name contains
5. path segment contains
6. class, type, or registry tag match

Results should not load assets by default. Loading belongs to explicit adapter
behavior outside default search.

Normalized JSON:

```ts
type AssetResult = Result<Asset[]>;
```

The formatter turns asset result objects into canonical asset bindings.

## Registry Tags

`registryTags` means UE Asset Registry tags, not user-facing content labels.
They are lightweight key/value metadata exposed through `FAssetData`.

Example:

```lgl
door = asset(path: "/Game/BP_Door.BP_Door", type: blueprint, registryTags: {ParentClass: "/Script/Engine.Actor"})
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
door = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: door, graph: EventGraph)
```

Graph, widget, material, and PCG domains should not reimplement asset path
normalization, loading, or Asset Registry lookup. They should consume
canonical asset references resolved by the asset domain or by shared bridge
asset resolution utilities.

## Patch

The asset domain currently has no patch text. Asset mutation, such as create,
rename, move, duplicate, delete, save, metadata edits, redirector cleanup, and
source-control-aware package operations, belongs to a later asset-tools design.

There is no `AssetPatch` in the current asset domain target model.

## Normalized JSON

Asset normalized JSON is defined beside each feature above. The summary below
shows the top-level asset-domain payloads:

```ts
type AssetQuery = Query<FindAssets>;
type AssetResult = Result<Asset[]>;
```

Text is for agents. Normalized JSON is for schema validation, RPC, generated
types, and bridge adapters.

## Adapter Boundary

Pure LGL normalization may:

- normalize asset query clauses into a structured query object
- preserve asset bindings as references for graph, widget, material, and PCG
  domains
- normalize `registryTag.<key>` conditions into structured field references

Pure LGL normalization must not:

- scan the Asset Registry
- load assets
- resolve redirectors
- decide whether an asset can be opened as a graph or widget
- compute search ranking
- validate UE class paths

The adapter or bridge owns those UE-dependent responsibilities.
