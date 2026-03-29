# LOOMLE 0.4.0 Install and Upgrade Design

## Summary

The first `LOOMLE 0.4.0` install model should stay deliberately narrow.

It should support:

- project-local install only
- site-served script install
- script-first project-local update
- script-first project-local doctor
- upgrade of the project-local client and Unreal integration together
- a versioned client payload under `Loomle/install/versions/`

It should not support in this phase:

- global machine install
- global capability bundles
- installer-binary bootstrap flow
- Studio directory migration

## Installed Shape

After install, the Unreal project should contain:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    loomle(.exe)
    update.(sh|ps1)
    doctor.(sh|ps1)
    README.md
    install/
    state/
    local/
    workflows/
    examples/

  worklog/
```

This keeps the installed shape aligned with the current project-local model
while the runtime protocol changes underneath it.

## Official Entry Points

The official entrypoints for this phase should be:

- site-served install scripts:
  - `install.sh`
  - `install.ps1`
- installed project maintenance scripts:
  - `update.sh`
  - `update.ps1`
  - `doctor.sh`
  - `doctor.ps1`

All of these scripts should target a specific Unreal project root.

Source ownership for this phase should live under:

- `client/install.sh`
- `client/install.ps1`
- `client/update.sh`
- `client/update.ps1`
- `client/doctor.sh`
- `client/doctor.ps1`

Recommended command shape:

- `install.sh --project-root /path/to/Project`
- `install.ps1 -ProjectRoot C:\Path\To\Project`
- `Loomle/update.sh --project-root /path/to/Project`
- `Loomle/update.ps1 -ProjectRoot C:\Path\To\Project`
- `Loomle/doctor.sh --project-root /path/to/Project`
- `Loomle/doctor.ps1 -ProjectRoot C:\Path\To\Project`

The exact argument spelling can still evolve, but the model should stay:

- script entrypoint
- explicit project root
- no downloaded installer binary

## Script Responsibilities

Install, update, and doctor scripts should stay small.

Install/update should:

1. resolve the target project root
2. download or locate the release bundle
3. verify the bundle
4. copy plugin content into `Plugins/LoomleBridge/`
5. copy workspace content into `Loomle/`
6. write/update machine-readable install state
7. print a clear success/failure result

The public install and installed maintenance paths should perform this work
directly in shell or PowerShell. They should not depend on Python or an
internal bundle helper.

Doctor should:

1. resolve the target project root
2. confirm `Plugins/LoomleBridge/` exists
3. confirm `Loomle/loomle(.exe)` exists
4. confirm `Loomle/install/active.json` is readable
5. report runtime endpoint readiness separately from install completeness

They should not become a second runtime or migration engine.

They should not own:

- complex runtime discovery
- protocol behavior
- graph/runtime compatibility logic
- Studio artifact migrations

Those belong in the installed product and in later refactors.

## Upgrade Model

For this first `0.4` cut, upgrade should also stay simple.

Supported upgrade modes:

### 1. Repair

Re-copy the owned project-local LOOMLE material when files are missing or
corrupt.

### 2. Version upgrade

Replace the project-local client and plugin integration with a newer release.

The preferred client shape is:

- stable launcher at `Loomle/loomle(.exe)`
- versioned payload at `Loomle/install/versions/<version>/loomle(.exe)`

This avoids self-overwrite of the active client binary during update.

### 3. Force reinstall

Replace all LOOMLE-owned project-local installed material.

The unit of upgrade is still the project-local install bundle, not a machine
home plus project attach split.

## Ownership Rules

In this phase, LOOMLE owns only these installed areas:

- `Plugins/LoomleBridge/`
- `Loomle/`
- `Loomle/install/`
- required editor-performance setting written by install

Within `Loomle/`, install/update should treat these areas differently:

- installer-managed:
  - stable entrypoints
  - `Loomle/install/`
- preserved local state:
  - `Loomle/state/`
  - `Loomle/local/`
- team-shared tracked content outside installer ownership:
  - `worklog/`

Anything outside those owned areas is out of scope for installer mutation.

## Why Not Global Install Yet

A global install would introduce another migration surface at the same time as:

- runtime protocol migration
- endpoint model migration
- packaging migration

That is not a good first cut.

The first `0.4` job is to make runtime and install predictable, not to maximize
future structure purity.

## Relationship To Runtime

The install bundle should now be interpreted like this:

- `Loomle/loomle(.exe)` is the project-local MCP client entrypoint and nothing else
- `Plugins/LoomleBridge/` hosts the native MCP runtime authority

Install/update only need to materialize those two sides correctly.

The installed project should also contain only the maintenance scripts that are
useful after installation:

- macOS/Linux:
  - `Loomle/update.sh`
  - `Loomle/doctor.sh`
- Windows:
  - `Loomle/update.ps1`
  - `Loomle/doctor.ps1`

Bootstrap-only `install.*` scripts do not need to be copied into the project.
They should live on the site only, not inside release assets.

## Transition From Current State

The first `0.4` migration should therefore do this:

1. keep the project-local `Loomle/` install shape
2. replace the old Rust `mcp/server` bridge path with native MCP runtime inside
   `LoomleBridge`
3. remove binary-installer bootstrap assumptions from docs and release flows
4. move official install/update guidance to scripts

## Deferred Work

The following are explicitly deferred beyond this phase:

- global `loomle` installation
- project attach/init split
- `.loomle-core/` initialization
- Studio directory migration
- global skill/workflow installation

## Decision

The first `LOOMLE 0.4.0` install and upgrade model should be:

- project-local only
- script-first
- installer-binary-free
- `loomle` binary limited to MCP client duty
- focused on updating `Plugins/LoomleBridge/` and `Loomle/`
- versioned for the real client payload under `Loomle/install/versions/`
