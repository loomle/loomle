---
name: manage-unreal-assets-with-python
description: Create, edit, duplicate, rename, move, or cross-project migrate persistent Unreal Engine assets through Loomle Python after applying use-unreal-python. Use when no structured Loomle interface owns the required asset lifecycle operation; excludes Level assets, deletion, bulk import, and Blueprint Graph internals.
---

# Manage Unreal Assets with Python

Apply the resident `use-unreal-python` Skill first. This Skill adds persistent
Asset identity, package, reference, source-control, and migration semantics. It
does not make Python transactional or provide a dry run.

## Read only the matching workflow

- For create, reuse, edit, or duplicate, read
  [create-and-edit.md](references/create-and-edit.md).
- For rename or movement inside the current project, read
  [rename-and-move.md](references/rename-and-move.md).
- For migration to another project's Content directory, read
  [cross-project-migration.md](references/cross-project-migration.md).

## Establish exact asset scope

1. Use `sal_schema` for the `asset` Domain and `sal_query` to resolve every
   existing source and destination before Python. Prefer exact Asset Paths and
   verified native Class Paths over names or filesystem paths.
2. Distinguish create, edit, duplicate, in-project rename/move, and
   cross-project migration before choosing an API. In UE, changing an asset's
   package path is a rename/move; migration copies packages to another project.
3. Require an Editor session outside PIE. Editor Asset Library operations are
   slow, exclude Level assets, and are not supported as a PIE workflow.
4. Probe the required reflected Python API in the bound UE version. UE 5.7 and
   5.8 differ in some Asset Tools signatures; never infer overwrite support
   from another version.

Do not move or rename `.uasset` files with filesystem APIs. Do not use this
Skill for maps, Level packages, deletion, consolidation, broad import, or
ordinary Blueprint Graph edits.

## Make mutations restartable

- Treat an existing destination as `reused` only when its exact Class and
  task-defining state match. Otherwise report a conflict; never silently
  overwrite it.
- Split preflight, mutation, exact-path rediscovery, persistence, and
  verification into separate calls when a later failure could leave partial
  state.
- For a batch, preflight the complete source-to-destination map first and
  return one result entry per requested asset. A failed batch may be partial.
- Return ordinary JSON facts for paths, Classes, before/after values,
  referencers, redirectors, save state, and conflicts. When a published SAL
  projector supports the UObject, `sal.object(asset)` may additionally request
  canonical read-only projection; it does not replace ordinary evidence.

Use the base Skill's `applied` / `notApplied` / `unknown` recovery procedure
after any failed, lost, or uncertain call. Never replay a create, rename, move,
or migration script without rediscovering both source and destination.

## Persist and verify through structured surfaces

Prefer the exact Asset Domain terminal `save` after Python when it owns the
target package. Do not call Save All or save unrelated dirty packages. After a
mutation, query Asset Registry state again and verify the exact path, Class,
and task-specific values. A Python return value, an in-memory UObject, or a
copied file alone is not durable Asset proof.

## Report asset-specific outcomes

Report the requested operation, exact old and new Asset Paths, created versus
reused or changed versus already satisfied, native Class, affected referencer
packages, redirectors or conflicts, persistence evidence, and any partial or
unknown effects. For migration, separately report the requested package set,
dependency closure, conflict policy, destination verification level, and any
packages skipped or not independently proven.
