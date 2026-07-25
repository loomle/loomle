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
assets = target { domain: asset }

query assets
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint"
order by score desc, path asc
page limit 10
```

Results use native `FAssetData::AssetClassPath` values and may include `domains`
hints such as `blueprint`. Those hints do not select another Domain. UE Assets
do not share a Registry-resolvable contained-object Guid, so an exact Asset
Target uses Object Path plus verified native Class. It has no synthetic
StableRef.

```text
door = target {
  domain: asset,
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
}
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

## Domain Discovery

The `domains` list helps choose a Domain; it is not itself a handoff. Bind the
discovered path in a new Domain discovery request:

```text
door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}

query door
summary
```

The first Blueprint Query may discover its non-zero Blueprint Guid. A
canonical exact Target and every Blueprint Patch then retain both Asset Path
and that lowercase, hyphenated Guid. Asset defines Registry search and exact
package save. An exact Asset result may instead supply a canonical related
Target and explicit handoff; when present, copy that returned Target rather
than reconstructing it. Asset creation, rename, move, duplicate, delete,
metadata mutation, and bulk package operations are not part of the current
0.7 interface.
