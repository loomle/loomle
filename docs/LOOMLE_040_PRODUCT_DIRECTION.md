# LOOMLE 0.4.0 Product Direction

## Summary

`LOOMLE 0.4.0` should be treated as a scoped runtime and install refactor.

The first `0.4` cut should do three things only:

- replace the old custom RPC runtime path with Unreal-hosted native MCP
- make `loomle` itself the primary agent-facing MCP entrypoint
- replace binary-installer flows with script-first install and update flows

The first `0.4` cut should explicitly **not** do these things:

- no global machine-level LOOMLE install
- no LOOMLE Studio directory-model migration
- no global skills/workflows/home layer

This keeps `0.4` focused on runtime correctness and install simplicity.

## Product Decision

### 1. `loomle` remains project-local in the first 0.4 cut

For the first `0.4` implementation, `loomle` should continue to be installed
into the Unreal project:

- `<ProjectRoot>/Loomle/loomle(.exe)`

This avoids introducing a second large migration while runtime transport and
protocol are still changing.

### 2. `loomle` itself is the primary agent interface

The agent-facing runtime entrypoint should be:

- `Loomle/loomle`

This should be the stable MCP session surface for agents.

In this first `0.4` cut, the `loomle` binary should no longer be treated as a
general multi-command CLI. It should keep one core responsibility only:

- act as the project-local MCP client / launcher

Install, update, and doctor flows should move out to scripts.

It should not depend on Codex-managed MCP config as the primary runtime path.

### 3. Unreal owns runtime authority

Runtime authority remains inside Unreal and `LoomleBridge`.

For `0.4`, the runtime path should be:

- agent -> `loomle`
- project-local `loomle`
- project-scoped MCP endpoint
- `LoomleBridge` native MCP runtime
- Unreal authority

### 4. Install and update should be script-first

`0.4` should not keep the temporary downloaded installer-binary model as the
official path.

The preferred direction is:

- `install.sh`
- `install.ps1`
- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

Site should own install. Installed projects should own update and doctor.

They should not install a machine-global CLI in the first `0.4` cut.

Installed projects should keep only the maintenance scripts that remain useful
after install:

- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

Bootstrap-only install scripts do not need to live in the installed project.

### 5. Studio refactor is deferred

The `LOOMLE Studio` visible/hidden directory split remains a valid future
direction, but it should not be bundled into the first `0.4` runtime cut.

For this phase:

- keep project-visible install material under `Loomle/`
- keep project integration under `Plugins/LoomleBridge/`
- do not introduce `loomle/` vs `.loomle-core/` migration yet

## Why This Scope Is Better

Without this scope cut, `0.4` tries to do too many migrations at once:

- runtime protocol migration
- transport migration
- install model migration
- repository/installed-layout migration
- Studio artifact migration

That would make failures hard to attribute and upgrades hard to reason about.

With the narrowed scope:

- runtime changes are testable in isolation
- install/update changes are understandable
- project layout stays familiar during the protocol cutover

## First 0.4 Shipping Shape

The first shippable `0.4` product shape should be:

- project-local `loomle`
- project-local `Loomle/`
- project-local `Plugins/LoomleBridge/`
- Unreal-hosted native MCP runtime
- script install
- script update
- script doctor

Notably absent:

- no global LOOMLE home
- no global `loomle`
- no separate installer binary
- no Studio-layer migration

## Relationship To Later 0.4+ Work

Later phases may still introduce:

- global install
- global capability layers
- `loomle/` vs `.loomle-core/` split
- Studio artifact ownership cleanup

But those should be treated as later work, not as blockers for the first native
MCP cut.

## Decision

`LOOMLE 0.4.0` first implementation should be documented and implemented as:

- a project-local `loomle` client
- a project-attached Unreal native MCP runtime
- a script-first install/update/doctor model
- no global install and no Studio directory migration in this phase
