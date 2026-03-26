# LOOMLE 0.4.0 Install and Upgrade Design

## Summary

`LOOMLE 0.4.0` should introduce a new install and upgrade model.

This model should support:

- global LOOMLE installation on a machine
- project attach/init for Unreal projects
- lightweight upgrades that do not blindly replace everything
- migration from the current `0.3.x` project-local workspace model

The most important rule for `0.4.0` is:

- upgrade only the parts that actually changed and are owned by LOOMLE

This should replace the current all-or-nothing project workspace replacement
model.

## Why A New Upgrade Model Is Needed

The current install flow is built around copying whole trees into a project.

That was acceptable when the installed shape was simpler.

It becomes incorrect once LOOMLE has:

- global skills and workflows
- visible project collaboration artifacts
- hidden project system state
- long-lived human-edited documents

At that point, a full tree replacement becomes dangerous because it can:

- overwrite user-maintained project artifacts
- destroy local organization and notes
- make upgrades feel heavy and risky
- block future multi-project machine-level LOOMLE installs

`0.4.0` should move to a component-owned upgrade model instead.

## Install Model

The intended install model for `0.4.0` is:

1. Ensure the machine has a LOOMLE global home.
2. Ensure LOOMLE global capabilities are installed there.
3. Attach LOOMLE to a specific Unreal project.
4. Install or repair project integration assets.
5. Initialize visible project collaboration directories.
6. Initialize hidden project core directories.

This creates three install scopes:

- global scope
- project integration scope
- project artifact scope

## Install Scopes

### 1. Global Scope

This scope is machine-level and reusable across projects.

It should contain:

- role skills
- workflow skills
- shared references
- templates
- host integration metadata
- LOOMLE global install metadata

This scope should be installed once and upgraded independently from any single
project.

### 2. Project Integration Scope

This scope is attached to a single Unreal project.

It should contain:

- `Plugins/LoomleBridge/`
- project-visible `loomle/` initialization
- project-hidden `.loomle-core/` initialization

This scope should be upgradeable without destroying project-visible authored
content.

### 3. Project Artifact Scope

This scope contains human-readable project outputs:

- `loomle/concept/`
- `loomle/design/`
- `loomle/architecture/`
- `loomle/worklog/`

This scope should be treated as user collaboration space.

It must not be blindly overwritten during upgrade.

## Ownership Model

The upgrade strategy should be driven by ownership.

### Safe To Replace

These can usually be replaced directly if their owning component version
changes:

- global installed role skills
- global installed workflow skills
- generated machine-owned templates
- bridge/plugin binaries
- bridge-coupled MCP server binaries
- machine-owned metadata under `.loomle-core/`

### Safe To Create If Missing

These should be initialized if absent, but not forcibly reset if present:

- `loomle/`
- `loomle/concept/`
- `loomle/design/`
- `loomle/architecture/`
- `loomle/worklog/`
- `.loomle-core/`
- `.loomle-core/config/`
- `.loomle-core/state/`
- `.loomle-core/index/`
- `.loomle-core/cache/`

### Do Not Blindly Overwrite

These should never be replaced wholesale during a normal upgrade:

- human-authored design documents
- human-authored architecture documents
- worklog entries
- project-specific notes and organization choices inside `loomle/`

If LOOMLE needs to evolve the structure around them, it should migrate or
augment them, not delete and recreate them.

## Upgrade Modes

`LOOMLE 0.4.0` should distinguish three upgrade modes.

### 1. Repair Install

Purpose:

- restore missing machine-owned files
- re-create required directories
- repair broken integration state

Behavior:

- recreate missing global files
- recreate missing project integration assets
- recreate missing hidden metadata
- do not rewrite human-authored visible project artifacts unless explicitly
  requested

### 2. Standard Upgrade

Purpose:

- move a working installation from one LOOMLE version to another

Behavior:

- update only changed owned components
- preserve visible project artifacts
- migrate structure if required
- update install metadata to reflect the new version

This should be the normal user path.

### 3. Force Reinstall

Purpose:

- intentionally rebuild LOOMLE-managed layers from scratch

Behavior:

- replace all LOOMLE-owned generated/install components
- still protect visible project artifacts by default unless the user explicitly
  opts into replacing them

This mode should be explicit, not the default.

## Lightweight Upgrade Rule

The standard upgrade path should be component-aware.

That means:

- do not reinstall unchanged components
- do not rewrite directories just because the version changed
- do not replace a whole tree when only one owned file changed

Instead, the upgrader should reason in terms of components.

## Components To Track

At minimum, `0.4.0` should track these components separately:

- global skills bundle
- global workflow bundle
- global references/templates bundle
- Unreal plugin bundle
- MCP server bundle
- project visible structure initializer
- project hidden core initializer

This allows upgrades such as:

- update only the plugin and MCP server
- update only global skills
- add a new project-visible directory without touching existing design files
- update hidden metadata layout without touching the visible collaboration layer

## Manifest Direction

The release/install manifest should evolve from "where do I copy the workspace
tree" into "which owned components exist and where do they live."

Each component should describe at least:

- component name
- owner scope: global or project
- destination root
- replacement policy
- merge policy
- version or digest

The manifest does not need to be finalized in this document, but the install
design should assume this direction.

## Replacement Policies

Each component should have an explicit policy.

Recommended policy set:

- `replace`
  - fully machine-owned files or directories
- `merge_if_missing`
  - create missing files/dirs, leave existing user content in place
- `migrate`
  - transform structure from old shape to new shape
- `protect`
  - never overwrite without explicit user approval

Example mapping:

- global skill bundle -> `replace`
- plugin bundle -> `replace`
- `.loomle-core/` metadata -> `replace` or `migrate`
- `loomle/design/` -> `protect`
- `loomle/worklog/` -> `protect`
- visible directory initializers -> `merge_if_missing`

## Project Metadata

To support lightweight upgrades, LOOMLE should keep project-local machine
metadata under:

- `.loomle-core/`

This metadata should record enough information to answer:

- which LOOMLE version is installed
- which components are installed
- which component versions or digests were last applied
- whether this project originated from a legacy `0.3.x` install shape
- whether migration steps already ran

This metadata is what makes partial upgrade possible without guessing.

## Migration From 0.3.x

When upgrading a `0.3.x` project, the upgrader should:

1. detect the legacy shape
2. install or update the global LOOMLE home if needed
3. preserve working bridge integration
4. initialize `loomle/`
5. initialize `.loomle-core/`
6. migrate or regenerate machine-owned metadata out of legacy project-local
   workspace areas
7. preserve human-authored project-facing content
8. write migration records into `.loomle-core/`

The key principle is:

- migrate forward without treating the legacy layout as disposable user data

## Directory Initialization Rules

### `loomle/`

On first attach:

- create the directory if missing
- create standard subdirectories if missing
- add starter files only if missing

On upgrade:

- do not replace existing user-authored files by default
- only add or patch machine-owned scaffolding when necessary

### `.loomle-core/`

On first attach:

- create the directory and baseline internal structure

On upgrade:

- freely update machine-owned files
- run migrations for metadata schema changes

### `Plugins/LoomleBridge/`

On first attach:

- install plugin bundle

On upgrade:

- replace plugin-owned files as needed
- keep this logic isolated from visible artifact preservation rules

## Command Model Direction

`0.4.0` should move toward a command model that separates machine setup from
project attachment.

Illustrative direction:

1. global install or update
2. project attach/init
3. project repair
4. project upgrade

The exact command names can be decided later, but the distinction should be
real in the product model.

## Documentation Requirements

When this install model lands, documentation should explain:

- what is installed globally
- what is installed into a project
- what will be preserved during upgrade
- what will be replaced during upgrade
- how legacy `0.3.x` layouts are migrated

The user should never have to guess whether an upgrade will overwrite their
design documents.

## Decision

`LOOMLE 0.4.0` should adopt a lightweight, component-owned upgrade strategy.

The default upgrade path should:

- preserve human-authored visible project artifacts
- update only changed LOOMLE-owned components
- migrate legacy layouts forward
- keep plugin/runtime updates separate from project artifact updates

This should be treated as a core part of the `0.4.0` refactor, not as an
optional implementation detail.
