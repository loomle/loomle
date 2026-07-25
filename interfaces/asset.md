# asset

Search UE Asset Registry data and obtain exact Asset Targets without loading
assets by default.

## Target

The collection root is Query-only:

```sal
assets = target { domain: asset }
```

An exact Asset uses Path and verified native Class:

```sal
door = target {
  domain: asset,
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
}
```

Asset Path is Target identity. Asset Domain has no contained StableRef for the
Target itself.

## Query

```sal
query assets
assets ["text"]
[where condition]
[with registryTags]
[order by key asc|desc, ...]
[page limit N]
[page after "cursor"]
```

Search covers Asset name, object path, native Asset Class Path, and selected
Registry Tags. Structured filters are:

| Field | Operators |
| --- | --- |
| `root` | `=`, `!=` |
| `type` | `=`, `!=` |
| `name` | `=`, `!=`, `~=` |
| `path` | `=`, `!=`, `~=` |
| `registryTag.<key>` | `=`, `!=`, `~=` |
| `loaded` | `=`, `!=`, `loaded`, `not loaded` |

Ordering keys are `score`, `name`, `path`, and `type`. Cursor pagination
defaults to 50 and is capped at 200.

```sal
query assets
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint" and not loaded
with registryTags
order by score desc, path asc
page limit 10
```

Each result object carries ordinary data:

```sal
{
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint",
  domains: ["asset", "blueprint"],
  loaded: false,
  score: 98,
  registryTags: { ParentClass: "/Script/Engine.Actor" }
}
```

`domains` is a list of discovery hints, not routing data. Opening another
Domain requires an explicit independent Target.

Opaque `FiBData` and legacy `FiB` indexes are never materialized. Registry Tag
values larger than 8 KiB are likewise omitted and reported adjacently.
Unrepresentable native keys are preserved as exact JSON in comments. Results
larger than 128 KiB fail atomically with `validation.result_too_large`.

## Exact Read And Handoff

An exact Asset Query uses the canonical Target and the structural Target
operation:

```sal
query door
target
```

Asset discovery may return related canonical Targets such as Blueprint,
StateTree, or Widget, with explicit handoffs. Those Targets are flat and
independent; the Asset object never embeds them.

## Save

Asset defines no authored field or lifecycle Patch. An exact Asset Target may
use the Core terminal `save`:

```sal
patch door
save
```

The collection root cannot be patched. Asset create, rename, move, duplicate,
delete, metadata editing, redirector cleanup, and bulk Package operations are
outside the current interface.
