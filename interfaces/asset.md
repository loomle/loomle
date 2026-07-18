# asset

Search UE Asset Registry and obtain exact Asset Paths without loading assets.
This interface assumes the resident SAL Core guide.

## Query

The Asset collection root has one primary operation:

```sal
query asset
assets ["text"]
[where condition]
[with registryTags]
[order by key asc|desc, ...]
[page limit N]
[page after "cursor"]
```

The optional search text matches Asset name, object path, native Asset Class
Path, and selected Registry Tags. Structured filters are:

| Field | Operators |
| --- | --- |
| `root` | `=`, `!=` |
| `type` | `=`, `!=` |
| `name` | `=`, `!=`, `~=` |
| `path` | `=`, `!=`, `~=` |
| `registryTag.<key>` | `=`, `!=`, `~=`; `<key>` must be expressible as a SAL field path |
| `loaded` | `=`, `!=`, `loaded`, `not loaded` |

Conditions may use `not`, `and`, `or`, and parentheses. Ordered comparisons are
unsupported. Ordering keys are `score`, `name`, `path`, and `type`. Cursor
pagination defaults to 50 results and is capped at 200. `with registryTags`
adds Registry Tags whose keys can be represented as SAL inline fields. Any
remaining safe native key/value pairs follow that Asset binding in a lossless
Comment instead of being renamed or dropped.

UE's opaque `FiBData` and legacy `FiB` Blueprint-search indexes are never
materialized. Other Registry Tag values whose UE resource size exceeds 8 KiB
are protected the same way. They are absent from inline tags, fallback JSON,
and free-text search. Explicit `where registryTag.FiBData` and legacy `FiB`
conditions are rejected; other explicitly named Tag conditions stay exact,
including values above the output threshold. A bounded Comment adjacent to the
Asset reports the omitted native key, `ue_internal_index` or `value_too_large`
reason, and `resourceSizeBytes`; it never substitutes a fake tag value. Query
results over 128 KiB of condensed UTF-8 JSON fail atomically
with `validation.result_too_large` instead of returning truncated data. Narrow
the Query with search, filters, pagination, depth, or an exact reference. A
serialization failure during measurement fails closed with
`language.invalid_result_shape`.

Example:

```sal
query asset
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint" and not loaded
with registryTags
order by score desc, path asc
page limit 10
```

Results are ordered Asset bindings:

```sal
door = asset(
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint",
  domains: [asset, blueprint],
  loaded: false,
  score: 98,
  registryTags: {ParentClass: "/Script/Engine.Actor"}
)
###
registryTags omitted
FiBData: reason=ue_internal_index, resourceSizeBytes=842391
###
###
registryTags not representable as SAL inline fields; exact native key/value JSON:
{"Display Name":"Door"}
###
```

`path` is the global locator and `type` is the exact native
`FAssetData::AssetClassPath`. `domains` contains interface-discovery hints; it
does not route the target or override the loaded UE Class. Search does not load
assets by default.

## Exact Asset And Handoff

UE Assets do not share a Registry-resolvable persistent Guid, so there is no
`asset@id`. When the exact Path is known, bind it directly:

```sal
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door")
```

Enter a more specific interface with the same Path. The first Blueprint Query
may omit `BlueprintGuid` and returns it for later exact access:

```sal
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

## Save

Asset defines no domain-specific Patch operations. An exact Asset binding may
use the Core terminal `save` operation:

```sal
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door")

patch door
save
```

`patch asset` is invalid because the collection root does not identify one
Package. Asset create, rename, move, duplicate, delete, metadata editing,
redirector cleanup, and bulk Package operations are outside the current
interface.

Asset has no `summary`, Palette, singular Asset query, or Asset-object
`with schema`. Use `with registryTags` only when Registry metadata is needed.
SAL does not invent aliases for native Registry Tag names: a key that cannot be
written as a SAL field path is not available to `where registryTag.<key>` yet.
