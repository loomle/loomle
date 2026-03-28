# LOOMLE 0.4.0 Install and Upgrade Design

## Summary

The first `LOOMLE 0.4.0` install model should stay deliberately narrow.

It should support:

- project-local install only
- script-first install
- script-first update
- script-first doctor
- upgrade of the project-local client and Unreal integration together

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
    update.sh
    update.ps1
    doctor.sh
    doctor.ps1
    README.md
    runtime/
    workflows/
    examples/
```

This keeps the installed shape aligned with the current project-local model
while the runtime protocol changes underneath it.

## Official Entry Points

The official install/update/doctor entrypoints for this phase should be small platform
scripts:

- `install.sh`
- `install.ps1`
- `update.sh`
- `update.ps1`
- `doctor.sh`
- `doctor.ps1`

These scripts should target a specific Unreal project root.

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
- `update.sh --project-root /path/to/Project`
- `update.ps1 -ProjectRoot C:\Path\To\Project`
- `doctor.sh --project-root /path/to/Project`
- `doctor.ps1 -ProjectRoot C:\Path\To\Project`

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

Bundle extraction may call an internal helper script for the actual file copy,
but that helper is implementation detail. The public contract remains the
platform scripts above.

Doctor should:

1. resolve the target project root
2. confirm `Plugins/LoomleBridge/` exists
3. confirm `Loomle/loomle(.exe)` exists
4. confirm `Loomle/runtime/install.json` is readable
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

### 3. Force reinstall

Replace all LOOMLE-owned project-local installed material.

The unit of upgrade is still the project-local install bundle, not a machine
home plus project attach split.

## Ownership Rules

In this phase, LOOMLE owns only these installed areas:

- `Plugins/LoomleBridge/`
- `Loomle/`
- `Loomle/runtime/install.json`
- required editor-performance setting written by install

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

- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

An internal helper may also live under:

- `Loomle/runtime/install_release.py`

Bootstrap-only `install.*` scripts do not need to be copied into the project.

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
