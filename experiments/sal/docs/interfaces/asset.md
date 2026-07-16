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
| `registryTag.<key>` | `=`, `!=`, `~=` |
| `loaded` | `=`, `!=`, `loaded`, `not loaded` |

Conditions may use `not`, `and`, `or`, and parentheses. Ordered comparisons are
unsupported. Ordering keys are `score`, `name`, `path`, and `type`. Cursor
pagination defaults to 50 results. `with registryTags` adds the complete
Registry Tag map returned by `FAssetData`.

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
