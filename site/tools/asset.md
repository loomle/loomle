---
layout: default
title: Asset
parent: Interfaces
nav_order: 1
---

# Asset

The Asset interface searches UE Asset Registry state without loading every
asset and returns exact Object Paths for handoff into a more specific module.

```text
query asset
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint"
order by score desc, path asc
page limit 10
```

Results use native `FAssetData::AssetClassPath` values and may include `domains`
hints such as `blueprint`. UE Assets do not share a Registry-resolvable stable
Guid, so the global locator is the exact path. The interface has no selector of
this form:

```text
asset@id
```

## Registry Tags

Add Registry Tags only when Registry metadata is required:

```text
with registryTags
```

Loomle never materializes UE's opaque `FiBData` or legacy `FiB` search indexes,
and protects other oversized values. Adjacent comments identify omitted keys
and sizes. Large results fail atomically with guidance to narrow the query
instead of being silently truncated.

## Handoff

Bind the discovered path in the relevant module:

```text
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

Asset defines Registry search and exact package save. Asset creation, rename,
move, duplicate, delete, metadata mutation, and bulk package operations are
not part of the current 0.7 interface.
