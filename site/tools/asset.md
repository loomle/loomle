---
layout: default
title: Assets
parent: Tools
nav_order: 2
---

# Asset Tools

Asset tools operate at the Unreal asset boundary: creating assets, inspecting
asset-level metadata, and editing metadata. After creation, switch to the
domain-specific tools for graph, member, parameter, or WidgetTree edits.

## Recommended Flow

1. Use `asset_create` when the target asset does not exist.
2. Use `asset_inspect` to confirm the asset kind and asset-level state.
3. Use `asset_edit` only for metadata or enum-entry compatibility edits.
4. Continue with Blueprint, Material, PCG, or Widget tools for domain edits.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `asset_create` | Create supported Unreal assets. |
| `asset_inspect` | Inspect an asset through the matching public domain surface. |
| `asset_edit` | Edit asset-level metadata; enum entries are a compatibility case. |

## `asset_create`

Creates a supported Unreal asset.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `kind` | yes | `blueprint`, `enum`, `material`, `materialFunction`, `pcgGraph`, or `widgetBlueprint`. |
| `assetPath` | yes | Destination Unreal asset path. |
| `parentClassPath` | no | Blueprint parent class path; Blueprint defaults to Actor, WidgetBlueprint defaults to UserWidget. |
| `parentClass` | no | Compatibility alias for parent class. |
| `entries` | no | Enum entries for `kind: enum`. |
| `displayNames` | no | Enum display names keyed by entry name. |
| `args` | no | Optional kind-specific argument envelope. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

### Next Step

Use `asset_inspect`, then switch to the domain tool: `blueprint.*`,
`material.*`, `pcg.*`, or `widget.*`.

## `asset_inspect`

Inspects an asset through its public domain surface.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `kind` | yes | Same kind set as `asset_create`. |
| `assetPath` | yes | Unreal asset path. |
| `view` | no | PCG or WidgetTree view: `overview`, `pins`, `links`, `defaults`, `full`, `outline`, `layout`, or `details`. |
| `filter` | no | `nodeIds`, `names`, or fuzzy `text`, depending on asset kind. |
| `page` | no | `limit` and `cursor` for paged graph results. |
| `includeConnections` | no | Material connection detail. |
| `nodeIds` | no | Material expression ids. |

### Use When

Use this when the asset kind is known but the next step depends on asset-level
metadata or routing to the correct domain tool.

## `asset_edit`

Edits asset metadata. `updateEntries` remains for enum entry compatibility, but
general asset editing should be metadata-only.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Unreal asset path. |
| `operation` | yes | `updateMetadata` or `updateEntries`. |
| `kind` | no | Required only for enum compatibility `updateEntries`. |
| `metadata` | no | String metadata key/value pairs to set. |
| `removeKeys` | no | Metadata keys to remove. |
| `clearMetadata` | no | Remove all current metadata before applying new values. |
| `entries` | no | Enum entries for `updateEntries`. |
| `displayNames` | no | Enum display names keyed by entry name. |
| `args` | no | Optional operation envelope. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

### Boundary

Do not use `asset_edit` for graph nodes, Blueprint members, PCG parameters, or
WidgetTree hierarchy. Use the matching domain tool for those changes.
