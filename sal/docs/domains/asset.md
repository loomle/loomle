# Asset Domain

## Scope

The asset domain is the entry point for finding and resolving UE assets before
entering graph, widget, material, PCG, or other domains.

The domain should be backed by UE Asset Registry when running inside an
initialized UE process. It should not load assets by default. Search should
return canonical asset identities and lightweight registry metadata that agents
can feed into later SAL queries or patches.

The TypeScript library implements the shared `assets` Query operation, ordered
Object Text result, schema contract, and formatter. Its test suite exercises
the contract through a test-only generic memory executor. The UE Bridge
executes the same contract against the live Asset Registry.

## Basic Form

Asset query text is a statement list:

```sal
query asset
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint"
with registryTags
order by score desc, path asc
page limit 10
```

Asset results are SAL object statements:

```sal
door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
doorFrame = asset(path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame", type: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false)
```

The returned binding supplies the global address for a more specific target.
For example, the first Blueprint read uses the Asset result without guessing
its native state:

```sal
doorBlueprint = blueprint(asset: door)

query doorBlueprint
summary
```

## Asset Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Asset binding | `name = asset(path: "...", type: nativeClassPath, metadata...)` | `door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")` |
| Registry tags | `registryTags: {key: value}` plus Comment fallback | `registryTags: {ParentClass: "/Script/Engine.Actor"}` |
| Domain list | `domains: [symbol, symbol]` | `domains: [asset, blueprint]` |
| Loaded flag | `loaded: boolean` | `loaded: false` |

`path` is the canonical object path. `type` is exactly
`FAssetData::AssetClassPath`; SAL does not translate it into an asset category.
Other named arguments are lightweight Registry metadata or deterministic SAL
capability hints derived from the registered native Asset Class. A `domains`
list helps the agent discover relevant interface schema; it does not select an
adapter or override the actual UE Class after load.

Asset Path is the Asset domain's exact locator. UE assets do not share one
universal persistent Guid that the Asset Registry can resolve, so Asset
targets do not invent `asset@id`. `type`, `domains`, loaded state, registry
tags, and score describe or rank the result but do not replace `path` as
identity. Capability hints are not target-routing input. A missing path,
redirector, or type mismatch is resolved and reported by the adapter rather
than repaired through display-name search.

Asset identity should stay explicit. Avoid positional asset constructors:

```sal
asset("/Game/BP_Door.BP_Door", "/Script/Engine.Blueprint", false)
```

Use named arguments:

```sal
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", loaded: false)
```

The normalized representation is the shared `Binding` and `Call` model. Asset
does not introduce a second Asset-only object shape.

## Query

Asset query syntax:

```sal
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

The filter surface is closed:

| Field | Meaning | Operators |
| --- | --- | --- |
| `root` | package path root such as `/Game` | `=`, `!=` |
| `type` | exact native `FAssetData::AssetClassPath` | `=`, `!=` |
| `name` | Asset name | `=`, `!=`, `~=` |
| `path` | canonical object path | `=`, `!=`, `~=` |
| `registryTag.<key>` | one Asset Registry tag value; key must be a SAL field path | `=`, `!=`, `~=` |
| `loaded` | whether the Asset is currently loaded | `=`, `!=`, bare Boolean predicate |

`loaded` and `not loaded` are the compact Boolean predicate forms. Ordered
comparison operators are not supported by Asset fields. Conditions may still
combine supported predicates through `not`, `and`, `or`, and parentheses.

Supported `order by` keys:

- `score`
- `name`
- `path`
- `type`

Asset query has no `select` clause. The default result includes identity,
path, type, domains, and loaded state. `with registryTags` expands Asset
Registry tags. Cursor pagination defaults to 50 results when `page limit` is
omitted and the Asset adapter clamps every requested page to at most 200
results.

`assets` is the Asset collection root's only primary operation. The domain
defines no `summary`, Palette, `asset@id`, singular Asset operation, or
Asset-object `with schema`. When an exact Asset Path is already known, the
agent binds `asset(path: ...)` directly rather than querying it again.

The primary operation normalizes as:

```json
{"kind": "assets", "text": "door"}
```

The common Query envelope carries it in `operation`. The asset interface still
owns allowed fields, expansions, sort keys, and pagination defaults.

## Results

Asset search returns canonical asset bindings:

```sal
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

The shared `Result.object` contains one `ObjectText.statements` array. Asset
bindings and comments stay interleaved; there is no parallel `assets` array or
Asset-specific result wrapper.

## Registry Tags

`registryTags` means UE Asset Registry tags, not user-facing content labels.
They are lightweight key/value metadata exposed through `FAssetData`.

Example:

```sal
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", registryTags: {ParentClass: "/Script/Engine.Actor"})
```

`{}` is allowed here because it is an inline value object. It is not a
structural block. Inline object keys follow SAL identifier rules, and `kind` is
reserved because it can collide with the normalized reference discriminator.
The Bridge leaves those native names unchanged and places every key/value pair
that cannot be represented inline in an adjacent lossless Comment:

```sal
door = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint", registryTags: {ParentClass: "/Script/Engine.Actor"})
###
registryTags not representable as SAL inline fields; exact native key/value JSON:
{"Display Name":"Door"}
###
```

The JSON inside the Comment is data carried by a Comment, not a second request
or result syntax. A native Registry Tag key that cannot be written as a SAL
field path is also unavailable to `where registryTag.<key>` until SAL gains a
general quoted field-path design.

Registry metadata is not assumed to be small or human-readable. In particular,
UE's `FiBData` and legacy `FiB` tags contain the opaque Find in Blueprints
index. The adapter must identify those keys before calling
`FAssetTagValueRef::AsString()` and never expose, search, or filter their raw
values. The same output rule applies to any other tag whose
`FAssetTagValueRef::GetResourceSize()` exceeds 8 KiB. Ordinary values at or
below that bound remain exact and lossless.

Omitted values are not replaced by sentinel strings inside `registryTags`.
Instead, an immediately following ordinary Comment reports bounded metadata in
stable native-key order:

```sal
menu = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: "/Script/UMGEditor.WidgetBlueprint", registryTags: {ParentClass: "/Script/UMG.UserWidget"})
###
registryTags omitted
FiBData: reason=ue_internal_index, resourceSizeBytes=842391
LargeCustomTag: reason=value_too_large, resourceSizeBytes=32776
###
```

The Comment reports at most 64 omitted keys and then reports the remaining
count, so disclosure cannot itself become an unbounded result. The byte count
is UE's resource-size measurement for that native value; it is useful for
scale, but it is not a promise about the exact UTF-8 length of a localized
display string.

Opaque indexes and over-limit values are excluded from the free-text
`assets "text"` scan. An explicit `where registryTag.FiBData` or
`where registryTag.FiB` condition is rejected because those native values are
not queryable metadata. Other explicitly named Registry Tag conditions remain
exact even when their values exceed the output threshold: the adapter may read
that one requested value for comparison, but `with registryTags` still omits it
from the result. This keeps deliberate structured filtering available without
letting broad search materialize every large Tag.

As a final bridge-wide safety boundary, every read-only Query result is
serialized as condensed JSON and measured as UTF-8 before it leaves the SAL
module. A result larger than 128 KiB is replaced in full by a small
`validation.result_too_large` diagnostic that asks the agent to narrow the
query with pagination, depth, search, or an exact object reference. Results are
never byte-truncated, so the returned JSON and Object Text remain valid. This
post-query fuse does not run on Patch results because a mutation may already
have been applied before its readback is built. If the normalized Query result
cannot itself be serialized for measurement, the boundary fails closed with a
small `language.invalid_result_shape` diagnostic rather than treating its size
as zero.

## UE Capability Boundary

UE Asset Registry can search and enumerate `FAssetData` by:

- package path
- package name
- object path
- class path
- recursive path and class filters
- tags and values exposed through Asset Registry

Content Browser text search adds UI-oriented text filtering on top of Asset
Registry data. SAL should use Asset Registry as the primary structured search
source and expose agent-friendly text filtering and deterministic ranking
without copying Content Browser's UI search language as the main interface.

## Relationship To Other Domains

Asset bindings are reusable global-address references. An asset-backed domain
first binds its own concrete object, then contained objects refer to that
specific target:

```sal
doorAsset = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
door = blueprint(asset: doorAsset, id: "blueprint-guid")
g = graph(asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
```

Graph, widget, material, and PCG domains should not reimplement asset path
normalization, loading, or Asset Registry lookup. They should consume
canonical asset references resolved by the asset domain or by shared bridge
asset resolution utilities.

## Patch

The asset domain defines no Asset-specific Patch operations yet. An exact Asset
binding may still use the Core terminal `save` statement:

```sal
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

Pure SAL normalization may:

- normalize asset query clauses into a structured query object
- preserve asset bindings as references for graph, widget, material, and PCG
  domains
- normalize `registryTag.<key>` conditions into structured field references
- preserve Core `save` terminal statement text for an exact Asset target

Pure SAL normalization must not:

- scan the Asset Registry
- load assets
- resolve redirectors
- decide whether an asset can be opened as a graph or widget
- compute search ranking
- validate UE class paths
- resolve Package ownership, Source Control state, external packages, or
  execute `save`

The adapter or bridge owns those UE-dependent responsibilities.

## Implementation Audit — 2026-07-18

The Registry Tag admission policy, bounded omission comments, Asset page cap,
and read-only Query result fuse compile against UE 5.7. SAL diagnostic and
schema tests, the generated interface catalog check, interface validation, and
Client tests pass. Patch results do not enter the Query fuse.

This audit did not replace the installed plugin or launch Unreal Editor while
the packaged build was under user testing. Live verification still needs to
cover real `FiBData`, an over-limit ordinary Tag, a requested page above 200,
and a deterministic result above 128 KiB in the next integration cycle.
