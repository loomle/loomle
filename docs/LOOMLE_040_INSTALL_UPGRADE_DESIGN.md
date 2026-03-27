# LOOMLE 0.4.0 Install and Upgrade Design

## Summary

`LOOMLE 0.4.0` should introduce a new install and upgrade model.

This model should support:

- global LOOMLE installation on a machine
- project attach/init for Unreal projects
- lightweight upgrades that do not blindly replace everything
- migration from the current `0.3.x` project-local workspace model
- a runtime model centered on a global `loomle` client/launcher

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
2. Ensure the global `loomle` executable is installed there.
3. Ensure LOOMLE global capabilities are installed there.
4. Attach LOOMLE to a specific Unreal project.
5. Install or repair project integration assets.
6. Initialize visible project collaboration directories.
7. Initialize hidden project core directories.

This creates three install scopes:

- global scope
- project integration scope
- project artifact scope

It also creates one explicit runtime split:

- global `loomle` client/launcher
- project-attached runtime server

For `0.4.0`, this install model should stay intentionally narrow:

- global `loomle` install is the only supported machine entrypoint
- project-level attach is the only supported Unreal integration mode
- project-local trial install mode is out of scope
- engine-level plugin install mode is out of scope

## Agent-Friendly Install Principle

`LOOMLE 0.4.0` should treat agent-assisted install and upgrade as a first-class
product requirement.

This does not mean every install or upgrade should complete with no host
approval.

It means the default official path should be:

- transparent enough for an agent to explain
- small enough for a host to approve confidently
- limited enough to stay inside user-owned directories

This leads to five hard requirements for the official install/update path:

1. it should stay user-level by default
2. it should make intended file writes explicit
3. it should rely on minimal platform-default tooling
4. it should keep complex install logic out of the bootstrap/update scripts
5. it should emit success and failure states that an agent can restate clearly

The main design goal is not "zero approval."

The main design goal is:

- predictable approval
- understandable execution
- a high chance that an agent can complete first install and later upgrade with
  clear user consent

## Official Install And Update Shape

For the first `0.4.0` phase, the official install and update path should prefer
small platform scripts over a temporary downloaded installer binary.

Directionally:

- bootstrap install should be script-first
- CLI self-update should also be script-first in the early phase
- project attach/repair should remain a `loomle` command concern

This means the official entrypoints should look more like:

- `install.sh`
- `install.ps1`
- `update.sh`
- `update.ps1`

And less like:

- download temporary `loomle-installer`
- execute that downloaded binary
- delete it afterwards

The reason is straightforward:

- a script is easier for users and agents to inspect
- a script produces a clearer approval story
- the install model is still evolving quickly in `0.4.0`
- early updater logic should stay flexible instead of being frozen into a
  bootstrap binary too early

This document therefore treats the current temporary-installer model as a
transitional implementation, not the target `0.4.0` design.

## Script Scope Must Stay Small

The bootstrap and update scripts should be deliberately narrow in scope.

They should only orchestrate machine-level setup steps such as:

- detect platform
- choose the correct published CLI asset
- download that asset
- verify its digest
- place it into the user-level LOOMLE install directory
- atomically replace the existing CLI where possible
- write minimal install metadata

They should not become a second product runtime.

They should not be responsible for:

- project attach/init policy
- project repair policy
- compatibility solving across project/runtime/global components
- runtime diagnostics beyond basic install sanity checks
- complex migration logic for project-owned content

Those responsibilities belong in the installed `loomle` executable.

## Platform And Dependency Constraints

Script-first install and update are acceptable only if their dependency surface
stays extremely small.

The shell scripts should assume only platform-default capabilities such as:

- macOS/Linux: `sh`, basic file commands, and `curl` or `wget`
- Windows: PowerShell with built-in filesystem and download support

The official scripts should not require:

- Python
- Node.js
- `jq`
- Cargo
- Homebrew
- any non-default language runtime

Cross-platform consistency should come from matching behavior and matching
responsibility boundaries, not from forcing a single universal script.

It is acceptable, and likely preferable, to maintain:

- one shell script path for macOS/Linux
- one PowerShell path for Windows

## Install Source And Upgrade Routing

`0.4.0` should explicitly track how `loomle` was installed.

At minimum, machine-level install metadata should record:

- install source: `bootstrap`, `homebrew`, `winget`, or `unknown`
- release channel: `stable`, `nightly`, or equivalent
- installed CLI version
- install root

This is required because upgrade routing should depend on install source.

Recommended rule:

- bootstrap-installed LOOMLE uses the official LOOMLE update path
- Homebrew-installed LOOMLE uses Homebrew upgrade
- Winget-installed LOOMLE uses Winget upgrade

`loomle update` should therefore behave as a router, not as an unconditional
self-updater.

It should never silently replace a package-manager installation with a
bootstrap-managed installation.

## Local Install Cache

The first machine-level install should also establish a small local LOOMLE
artifact cache.

This cache should hold reusable install bundles such as:

- global CLI assets
- global capability bundles
- project template bundles
- Unreal plugin/runtime server bundles

The purpose is simple:

- avoid re-downloading the same owned artifacts for every project attach
- make later project installs more reliable and faster
- improve the chance that an agent can complete project install without an
  additional network step

This cache should stay an optimization layer, not a source of truth.

Project attach/install should:

- prefer the local cache when the required bundle is already present
- download only when the cache is missing or version-incompatible
- continue to treat installed project state as distinct from machine cache

Project-specific state such as initialized `loomle/`, `.loomle-core/`, and
runtime connection metadata should not be treated as cache content.

## Relationship To Project Upgrade

Machine-level CLI install/update and project-level attach/repair must remain
separate flows.

The intended split is:

- machine-level bootstrap/update scripts acquire or replace the global
  `loomle` CLI
- the installed `loomle` command performs project attach/init/repair

This keeps the scripts small and keeps project-sensitive logic inside the
product binary that already understands:

- project structure
- version compatibility
- runtime discovery
- migration rules for LOOMLE-owned project content

## Install Scopes

### 1. Global Scope

This scope is machine-level and reusable across projects.

It should contain:

- global `loomle` executable
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
- project runtime server implementation assets
- project-owned runtime connection metadata

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

- global `loomle` executable
- global installed role skills
- global installed workflow skills
- generated machine-owned templates
- bridge/plugin binaries
- bridge-coupled runtime server binaries or scripts
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

- global CLI bundle
- global skills bundle
- global workflow bundle
- global references/templates bundle
- Unreal plugin bundle
- project runtime server bundle
- project visible structure initializer
- project hidden core initializer

This allows upgrades such as:

- update only the plugin and project runtime server
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
- which global CLI version is expected or compatible
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
- initialize runtime metadata location
- initialize compatibility/install records for runtime connection

On upgrade:

- freely update machine-owned files
- run migrations for metadata schema changes

### `Plugins/LoomleBridge/`

On first attach:

- install plugin bundle
- install project runtime server implementation assets

On upgrade:

- replace plugin-owned files as needed
- keep this logic isolated from visible artifact preservation rules

## Command Model Direction

`0.4.0` should move toward a command model that separates machine setup from
project attachment.

Illustrative direction:

1. global install or update
2. global CLI self-update
3. project attach/init
4. project repair
5. project upgrade
6. runtime doctor

The exact command names can be decided later, but the distinction should be
real in the product model.

Directionally, the command model should assume:

- `loomle install`
- `loomle update`
- `loomle doctor`
- `loomle mcp`

`loomle mcp` is the runtime protocol entrypoint, not a human convenience
wrapper.

## Documentation Requirements

When this install model lands, documentation should explain:

- what is installed globally
- what the global `loomle` executable owns
- what is installed into a project
- what will be preserved during upgrade
- what will be replaced during upgrade
- how legacy `0.3.x` layouts are migrated
- how global CLI and project integration compatibility is checked

The user should never have to guess whether an upgrade will overwrite their
design documents.

## Decision

`LOOMLE 0.4.0` should adopt a lightweight, component-owned upgrade strategy.

The default upgrade path should:

- preserve human-authored visible project artifacts
- update only changed LOOMLE-owned components
- migrate legacy layouts forward
- keep plugin/runtime updates separate from project artifact updates
- keep global CLI updates separate from project-attached runtime updates

This should be treated as a core part of the `0.4.0` refactor, not as an
optional implementation detail.
