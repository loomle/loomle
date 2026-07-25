# Asset Domain

## Scope

Asset Domain exposes UE Asset Registry discovery, exact Asset identity, native
Registry Tags, load state, and generic Package save. It does not create a
parallel Asset model or infer a more specific Domain from native Class.

Asset create, rename, move, duplicate, delete, metadata editing, redirector
cleanup, and bulk Package operations remain outside the current Domain.

Collection search is zero-load by default. It enumerates `FAssetData` from the
Asset Registry and must not load matching Assets merely to rank, filter, or
format them. Any later exact Domain Target that requires a UObject performs
its own explicit resolution.

## Target

The Query-only collection root is:

```sal
assets = target { domain: asset }
```

An exact Asset Target is:

```sal
door = target {
  domain: asset,
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
}
```

Query discovery may omit `type` as an assertion. Canonical exact readback and
Patch always include the resolved native `FTopLevelAssetPath`.

Asset Path is identity. UE Assets do not share a Registry-resolvable persistent
Guid, so Asset Domain exposes no contained StableRef for the Target.

## Object Data

Registry results are ordinary ObjectExpr:

```sal
{
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint",
  domains: ["asset", "blueprint"],
  loaded: false,
  score: 98,
  registryTags: {
    ParentClass: "/Script/Engine.Actor"
  }
}
```

`path` is the Registry object path and `type` is the exact native Asset Class
Path. `domains` contains string discovery hints. It is not Target routing,
adapter composition, or proof that the Asset supports another Domain.

No semantic tag is needed. `asset` is a reserved Domain keyword and cannot be
used as a tag.

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

Search text covers Asset name, object path, native Class Path, and selected
Registry Tag values. Filters are:

| Field | Operators |
| --- | --- |
| `root` | `=`, `!=` |
| `type` | `=`, `!=` |
| `name` | `=`, `!=`, `~=` |
| `path` | `=`, `!=`, `~=` |
| `registryTag.<key>` | `=`, `!=`, `~=` |
| `loaded` | `=`, `!=`, `loaded`, `not loaded` |

Conditions support `not`, `and`, `or`, and parentheses. Ordered comparisons
are unsupported. Ordering keys are `score`, `name`, `path`, and `type`.
Pagination defaults to 50 and is capped at 200.

```sal
query assets
assets "door"
where root = "/Game" and
  type = "/Script/Engine.Blueprint" and
  not loaded
with registryTags
order by score desc, path asc
page limit 10
```

The root Target result uses:

```sal
result domain_root
target assets = target { domain: asset }
objects
...
```

Every Asset object remains discovery data. When enough facts are available,
the result may also include a canonical related Target for a more specific
Domain.

## Registry Tag Fidelity

`with registryTags` adds native Registry Tags whose keys are valid SAL
identifier fields, except the collision-prone key `kind`. The current Bridge
does not place non-identifier keys or `kind` inside `registryTags`, even though
ObjectExpr can represent quoted keys. It preserves those exact native
key/value pairs in an immediately adjacent lossless Comment:

```sal
door = {
  path: "/Game/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint",
  registryTags: { ParentClass: "/Script/Engine.Actor" }
}
###
registryTags not representable as SAL inline fields; exact native key/value JSON:
{"Display Name":"Door","kind":"native-value"}
###
```

The condensed JSON is Comment data, not executable SAL. These keys are also
unavailable to `where registryTag.<key>` because member-path syntax cannot
address them losslessly.

UE's opaque `FiBData` and legacy `FiB` Blueprint-search indexes are never
materialized. Other Registry Tag values whose UE resource size exceeds 8 KiB
are protected the same way. They are absent from inline fields, fallback JSON,
and free-text search.

Explicit conditions on `FiBData` or `FiB` are rejected. Other explicitly named
Tag conditions remain exact even when the value exceeds the output threshold.
An adjacent bounded Comment reports protected omissions in stable native-key
order:

- native key;
- `ue_internal_index` or `value_too_large`;
- `resourceSizeBytes`.

At most 64 omitted keys are listed; if more exist, one final line reports the
remaining count. Omitted values are not replaced by sentinel strings. The
resource size is UE's native measurement and is not claimed to equal the UTF-8
display length.

As a Bridge-wide read safety fuse, the complete normalized result of every
read-only Query is serialized as condensed UTF-8 JSON before it leaves SAL. A
result above 128 KiB fails atomically with `validation.result_too_large` and
asks the caller to narrow search, pagination, depth, or identity. A
serialization failure fails closed with `language.invalid_result_shape`.
Results are never byte-truncated.

The 128 KiB fuse is Query-only. It does not run on Patch results because live
mutation may already have occurred before readback is built; Patch readback
must instead follow its mutation contract honestly.

## Exact Read

```sal
query door
target
```

The Bridge resolves the Asset Registry entry, canonicalizes Path and native
Class, and emits `result exact_target`. `target with schema` describes the
small exact Asset surface and terminal save availability.

Asset has no `summary`, singular contained-object operation, Palette, or
Asset-object StableRef.

## Cross-Domain Handoff

Native Class and Registry metadata may suggest another Domain, but the result
must construct an independent canonical Target only when every required field
is verified.

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related doorBlueprint = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff edit_blueprint to doorBlueprint
```

If zero-load Registry data lacks `BlueprintGuid`, GraphGuid, or another
verification field, it remains ordinary evidence:

```sal
{
  assetPath: "/Game/Blueprints/BP_Door.BP_Door",
  suggestedDomain: "blueprint",
  exactTargetAvailable: false
}
```

The formatter never invents a related Target.

## Save

An exact canonical Asset Target may use Core `save`:

```sal
patch door
save
```

This is a terminal request. It resolves the exact Package, uses the shared
non-interactive Source Control-aware save path, and never implies compile.

The collection root cannot be patched. A failed external save does not create
an in-memory rollback claim.

## Adapter Boundary

Pure SAL handles object fields, Target structure, Query clauses, ordering, and
result formatting. The Asset adapter owns:

- Registry queries and native filter semantics;
- Path and Class canonicalization;
- loaded-state observation;
- protected Registry Tag policy;
- zero-load evidence and exact Target verification;
- Package ownership and save behavior.

Native Class is always validation inside already selected Asset Domain. It
never activates Blueprint, StateTree, Widget, or another adapter.
