# LOOMLE Repository Structure

## Summary

For the first `LOOMLE 0.4.0` cut, repository and installed-shape discussion
should stay narrow.

This phase is about:

- native MCP runtime in `LoomleBridge`
- project-local client retained in `Loomle/`
- script-first install/update/doctor

This phase is not about:

- global LOOMLE home
- global skills/workflows layer
- Studio artifact directory migration

## Current Source Layout Meaning

### `engine/LoomleBridge/`

Owns:

- Unreal integration
- native MCP runtime
- authority-side tool execution

### `client/`

Owns:

- project-local `loomle`
- MCP client/session behavior
- source-owned script entrypoints:
  - `install.sh`
  - `install.ps1`
  - `update.sh`
  - `update.ps1`
  - `doctor.sh`
  - `doctor.ps1`
- no binary install/update command surface

Publication rule:

- `install.*` is site-served only
- `update.*` and `doctor.*` are copied into installed projects

### `workspace/Loomle/`

Owns project-local install material that gets copied into:

- `<ProjectRoot>/Loomle/`

For this phase, it remains install material, not a Studio artifact model.

### `packaging/`

Owns:

- release bundle assembly
- install/update scripts
- manifest shape
- install contract

## Installed Project Shape

The intended installed project shape for this phase is:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    loomle(.exe)
    update.(sh|ps1)
    doctor.(sh|ps1)
    README.md
    runtime/
    workflows/
    examples/
```

## Runtime Shape

At runtime, the effective split is:

- `Loomle/loomle(.exe)` = MCP client / launcher only
- `Plugins/LoomleBridge/` = native MCP runtime authority

Install and maintenance entrypoints are scripts, not subcommands on `loomle`.
The public install path is site-served; installed projects keep only
`update.*` and `doctor.*`.

The old `mcp/server` bridge layer is no longer part of the target structure.

## What Is Deferred

These structure moves remain deferred beyond the first `0.4` cut:

- global `cli/`
- global capability layer
- project-visible `loomle/` Studio artifact split
- project-hidden `.loomle-core/`

## Decision

For the first `0.4` cut, the structural model should be documented as:

- keep project-local install shape
- remove Rust `mcp/server`
- keep client source ownership under `client/`
- keep `workspace/Loomle/` as project install material
- install only `update.*` and `doctor.*` scripts into the project
- defer broader global and Studio restructuring
