# LOOMLE 0.4.0 Structure Refactor

## Summary

`LOOMLE 0.4.0` should be treated as a structural upgrade, not a small feature
release.

This upgrade changes four things together:

- the project-facing directory model
- the skill installation and packaging model
- the way `LOOMLE Studio` project outputs are stored
- the runtime entrypoint and connection model

The main goal is to separate:

- global LOOMLE capabilities installed on the user's machine
- project-local human-readable outputs
- project-local hidden system state
- project-attached runtime authority

The current repository shape still reflects an earlier model where the
installed project workspace under `workspace/` carries too many roles at once.

For `0.4.0`, that model should be split.

## Why This Refactor Is Needed

The current direction has exposed a real structural problem:

- skills and shared studio capabilities are global assets
- design, architecture, and worklog outputs are project assets
- hidden state, indexes, and machine-facing metadata are system assets

These should not live in the same place.

If they do, several problems appear:

- project outputs get mixed with installation assets
- human-facing artifacts end up in tool-owned directories
- multi-project usage becomes awkward
- future host integration becomes harder
- role skills such as `Dora` do not have a clean place to write their outputs

`LOOMLE 0.4.0` should fix this by making the layering explicit.

## New Layer Model

`LOOMLE 0.4.0` should use three layers.

### 1. Global LOOMLE Install Layer

This is the machine-level LOOMLE home.

It contains reusable LOOMLE capabilities that are not tied to one Unreal
project.

Examples:

- global `loomle` executable
- installed role skills
- workflow skills
- shared references
- templates
- host integration assets
- installation metadata
- machine-level helper scripts

This layer should be installed once and reused across multiple Unreal projects.

This is the layer that should evolve toward the gstack-style model: a reusable
global capability base that can be attached to many projects.

### 2. Project Visible Layer

This is the user-facing collaboration layer inside a specific Unreal project.

Its purpose is to hold project outputs that humans and agents should read,
maintain, and discuss.

This layer should live under a visible project-root directory named:

- `loomle/`

This directory is part of the project, not part of the global install.

Its contents are project artifacts, not tool internals.

Recommended subdirectories:

- `loomle/concept/`
- `loomle/design/`
- `loomle/architecture/`
- `loomle/worklog/`

These are the main `LOOMLE Studio` output surfaces.

### 3. Project Hidden Core Layer

This is the project-local internal system layer.

Its purpose is to hold LOOMLE-managed metadata that should not clutter the
user-facing project root.

This layer should live under a hidden project-root directory named:

- `.loomle-core/`

Recommended contents:

- config
- state
- indexes
- cache
- installation records
- host-facing integration state
- other LOOMLE-managed internal metadata

This directory is for system concerns, not for human-readable design output.

## Naming Decision

The project-local naming model for `0.4.0` should be:

- visible layer: `loomle/`
- hidden internal layer: `.loomle-core/`

This gives `LOOMLE Studio` a clear split:

- `loomle/` is what humans collaborate around
- `.loomle-core/` is what LOOMLE uses internally

This is intentionally a mixed model:

- visible where people should read and maintain the artifacts
- hidden where the files are internal and operational

## Repository Meaning After Refactor

After this refactor, repository directories should be understood like this.

### `skills/`

Development source for role skills and workflow skills.

This is the authoring layer inside the repository.

Examples:

- `skills/dora/`
- `skills/price/`
- `skills/aeris/`

These are not project outputs.

### `workspace/`

This should no longer be interpreted as the final home of project outputs.

Instead, it should be treated as installation-oriented material and packaged
workspace assets that LOOMLE can deploy, derive from, or use during project
setup.

`workspace/` is part of the LOOMLE product installation model, not the long-term
home of project `Design`, `Architecture`, or `Worklog`.

That means directories such as:

- `workspace/design/`
- `workspace/skills/` as a final project artifact surface

should be treated as transitional and should move toward the proper split
defined in this document.

## Project Artifact Ownership

`LOOMLE Studio` outputs should map to project directories like this.

### `Dora`

`Dora` should maintain:

- `loomle/design/`

Her job is to keep the project's design layer complete, effective, and clean.

She should not treat a global workspace as her primary output target.

### `Aeris`

`Aeris` should maintain:

- `loomle/architecture/`

### `Worklog`

Cross-role task progression should be written into:

- `loomle/worklog/`

The worklog is part of the visible collaboration surface, not part of hidden
tool state.

### Hidden Internal State

Machine-facing or system-facing artifacts that users generally should not edit
by hand belong under:

- `.loomle-core/`

## Installation Model Changes

This refactor implies an installation change.

The current model emphasizes a project-local installed entrypoint. For `0.4.0`,
LOOMLE should move toward a global install model plus project attach/init.

The intended direction is:

1. Install or update LOOMLE globally on the machine.
2. Use LOOMLE to attach to or initialize a specific Unreal project.
3. Materialize project-visible directories under `loomle/`.
4. Materialize project-hidden internal state under `.loomle-core/`.
5. Install or update Unreal project integration assets such as
   `Plugins/LoomleBridge`.

This also implies a runtime entrypoint shift:

- the globally installed `loomle` CLI becomes the stable agent-facing
  client/launcher
- `loomle mcp` becomes the primary runtime protocol entrypoint
- Unreal project integration remains the runtime authority
- project runtime access should no longer depend on Codex-managed MCP
  configuration as the primary path

This means installation is no longer just "copy a project workspace and use it
in place."

Instead, installation becomes:

- global capability install
- project integration
- project artifact structure initialization

## Upgrade Implications

This is a breaking structural upgrade relative to the current `0.3.x` model.

That is why the target version should move to:

- `0.4.0`

The version bump is justified because this upgrade changes:

- where project outputs live
- how LOOMLE installs
- how skills are packaged and consumed
- how future multi-project studio workflows will operate

## Migration Direction

The `0.4.0` migration should follow these principles.

### Principle 1: Global capabilities out of project output space

Skills and reusable studio capabilities should move toward the global LOOMLE
install layer.

### Principle 2: Human-readable outputs into `loomle/`

Artifacts that humans should read or maintain should live under:

- `loomle/`

### Principle 3: Internal state into `.loomle-core/`

System-owned metadata should live under:

- `.loomle-core/`

### Principle 4: Do not hide collaborative artifacts

Design, architecture, and worklog should stay visible.

### Principle 5: Do not scatter visible outputs at project root

The visible output layer should be grouped under one top-level project
directory:

- `loomle/`

This avoids cluttering the project root with many parallel top-level folders.

## 0.3.x to 0.4.0 Migration Plan

This section defines the practical migration from the current `0.3.x` shape to
the `0.4.0` target shape.

The purpose is not only to describe the destination. It is to describe how
existing installs and repository assumptions should move there safely.

### Current 0.3.x Assumption

The current structure still centers the installed project shape around:

- `Plugins/LoomleBridge/`
- `Loomle/`

And the repository still treats `workspace/` as the source of that installed
project-local workspace.

That model worked when LOOMLE's installed project surface was primarily:

- a project-local client
- graph guides and examples
- bridge-coupled runtime material

It becomes less correct once `LOOMLE Studio` adds:

- reusable role skills
- reusable workflow skills
- long-lived project collaboration artifacts
- multi-project machine-level LOOMLE behavior

### Target 0.4.0 Assumption

For `0.4.0`, the install model should assume:

1. LOOMLE has a global machine-level home.
2. Unreal projects have a visible LOOMLE collaboration layer at `loomle/`.
3. Unreal projects have a hidden LOOMLE internal layer at `.loomle-core/`.
4. Unreal bridge integration still installs into `Plugins/LoomleBridge/`.

### Runtime Role Split For 0.4.0

`0.4.0` should also assume this runtime role split:

- global `loomle` is a client/launcher
- `loomle mcp` is the primary agent-facing runtime protocol surface
- the runtime server belongs to project integration

That means the global CLI is not the runtime server itself.

### Migration Mapping

The current and target shapes should map like this.

#### Globalized from the old installed workspace model

These concerns should move toward the global LOOMLE install layer:

- global `loomle` executable
- role skills
- workflow skills
- shared templates
- shared references
- host-facing skill integration
- machine-level LOOMLE install metadata

These should no longer be conceptualized as project artifacts.

#### Kept project-local and visible

These concerns should live in `loomle/`:

- concept output
- design output
- architecture output
- worklog output

#### Kept project-local and hidden

These concerns should live in `.loomle-core/`:

- project config
- project/runtime metadata
- project-local indexes
- project-local caches
- internal state
- machine-written operational metadata

#### Kept bridge-local

These concerns should remain in `Plugins/LoomleBridge/`:

- Unreal plugin binaries
- Unreal plugin resources
- runtime server payloads that version-lock with the plugin
- Unreal-hosted runtime server implementation assets

## Directory Migration Rules

### Rule 1: Do not treat `workspace/` as the final home of project artifacts

`workspace/` remains useful as a repository source area and packaging assembly
surface.

It should not remain the conceptual home of final project outputs such as
design or worklog artifacts.

### Rule 2: `workspace/Loomle/` becomes installation material, not the artifact model

The current `workspace/Loomle/` repository content should be reinterpreted as:

- installable project integration material
- project bootstrap/readme material
- compatibility-era local entrypoint material

It should not continue to absorb the long-term `LOOMLE Studio` artifact model.

### Rule 3: New role outputs must target project artifacts, not workspace internals

Role skills such as `Dora` should write to the project's visible `loomle/`
layer, not to repository `workspace/` directories and not to hidden internal
directories.

### Rule 4: Hidden directories are for LOOMLE-owned system state only

If a human should regularly read or maintain it, it probably does not belong in
`.loomle-core/`.

### Rule 5: Visible directories are for durable collaboration artifacts

If it is a design, architecture, concept, or worklog artifact, it should be
part of the visible collaboration layer.

## Install Flow Migration

The old install model is effectively:

1. bootstrap installer
2. install bridge into project
3. install `Loomle/` project-local workspace
4. use project-local `Loomle/loomle`

The new install model for `0.4.0` should become:

1. ensure global LOOMLE home exists on the machine
2. ensure the global `loomle` executable exists and is upgradeable
3. ensure LOOMLE global skills/workflows are available
4. attach LOOMLE to a specific Unreal project
5. install or update `Plugins/LoomleBridge/`
6. initialize or repair `loomle/`
7. initialize or repair `.loomle-core/`
8. maintain compatibility entrypoints only as needed during the migration window

This is a substantial install-model change and should be described explicitly in
install docs rather than smuggled in through implementation alone.

## Runtime Connectivity Implication

The structure refactor should assume this runtime flow:

1. the agent executes global `loomle`
2. `loomle mcp` discovers the current project
3. `loomle mcp` resolves a project-derived local runtime endpoint
4. `loomle mcp` handshakes with the project runtime server

First `0.4.0` scope should assume:

- no active-project registry
- one active project per agent workflow
- project-derived local pipe/socket endpoints
- Unreal-hosted Python runtime server as the first in-process server direction

## Compatibility Window

`0.4.0` should support a migration window rather than a hard cliff.

During the migration window:

- existing `0.3.x` projects with `Loomle/` should remain repairable
- installer/update logic may detect old shapes and migrate them
- documentation should explain both the old detected shape and the new target
  shape

But the compatibility window should still be directional:

- preserve operability for old projects
- migrate toward `loomle/` and `.loomle-core/`
- avoid inventing new long-term features on top of the legacy project layout

## Project Migration Behavior

When LOOMLE encounters an existing `0.3.x` project, the upgrade behavior should
roughly be:

1. Detect old project-local LOOMLE footprint.
2. Create `loomle/` if missing.
3. Create `.loomle-core/` if missing.
4. Move or regenerate project-readable artifacts into `loomle/`.
5. Move or regenerate system-owned metadata into `.loomle-core/`.
6. Preserve bridge integration under `Plugins/LoomleBridge/`.
7. Leave a clear record of what was migrated.

The exact file-by-file migration script can be decided later. The structural
rule should be decided now.

## Documentation Migration

This refactor also requires a documentation migration.

At minimum, the following repository docs will need updates once the structural
refactor is implemented:

- `README.md`
- `docs/REPO_STRUCTURE.md`
- `workspace/README.md`
- install/bootstrap documentation
- release/install contract documentation
- role skill docs that currently still point at `workspace/`

Until those docs are updated, this document should be treated as the target
source of truth for the new structure direction.

## Packaging and Release Migration

`0.4.0` packaging should eventually distinguish between:

- global LOOMLE assets
- project integration assets
- Unreal plugin assets

That means release assembly should no longer think only in terms of "copy one
project-local workspace tree."

Instead, release assembly should understand at least these outputs:

- machine-level LOOMLE home content
- project-visible initialization content for `loomle/`
- project-hidden initialization content for `.loomle-core/`
- bridge/plugin bundle content

## Versioning Implication

Because the migration changes installation shape, project shape, and artifact
ownership, it should remain tied to:

- `0.4.0`

This should not be softened into a patch release or a minor hidden installer
change.

## Immediate Follow-Up Work

This document implies the following follow-up work items:

1. Redefine the install flow around global LOOMLE install plus project attach.
2. Update packaging and bootstrap logic to install global `loomle`, create `loomle/`, and
   `.loomle-core/` in the target project.
3. Define the role split between global `loomle` and the project runtime server.
4. Re-home `Dora` and future role outputs to the project-visible layer.
5. Reinterpret `workspace/` as install/package material rather than project
   artifact storage.
6. Define the compatibility window for `0.3.x` project upgrades.
7. Update repository and install documentation to the new structure model.
8. Add a migration path from the current `0.3.x` layout to the `0.4.0` layout.
9. Bump project versioning to `0.4.0` when the refactor lands.

## Decision

`LOOMLE 0.4.0` should adopt the following structure model:

```text
Global machine install:
  loomle executable
  reusable LOOMLE capabilities

Project root:
  loomle/
    concept/
    design/
    architecture/
    worklog/

  .loomle-core/
    config/
    runtime/
    state/
    index/
    cache/
    install/
```

This should be treated as the target structure for the upcoming LOOMLE Studio
refactor.
