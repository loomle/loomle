# LOOMLE Lite Product Plan

## Status

This document describes the earlier lightweight-kernel direction.

It is now superseded by the `0.4.0` direction documented in:

- `LOOMLE_040_PRODUCT_DIRECTION.md`
- `LOOMLE_040_STRUCTURE_REFACTOR.md`
- `LOOMLE_040_INSTALL_UPGRADE_DESIGN.md`
- `LOOMLE_040_RUNTIME_CONNECTIVITY.md`

Keep this file only as historical product context.

## Summary

`LOOMLE` should currently be refined as a lightweight Unreal project kernel.

The product should stay small, explicit, and easy to adopt.

It should not currently optimize for:

- machine-wide installation
- automatic discovery
- automatic orchestration
- complex upgrade automation
- heavy platform infrastructure

It should optimize for:

- project-local clarity
- strong role skills
- clear output contracts
- manual installability
- explicit user control
- understandable runtime permissions

This document consolidates the current product direction.

## Product Identity

`LOOMLE` is an Unreal-native AI kernel for project-local work.

It combines:

- Unreal runtime and graph access through `LoomleBridge`
- a local project control surface
- explicit role skills
- structured collaboration output

The near-term product is not a broad automation platform.

It is a sharp local core that helps humans and agents work inside a real Unreal
project with clear boundaries.

## Core Design Principle

The product should be:

- local before global
- explicit before automatic
- manual before over-automated
- role-contract-driven before workflow-heavy
- understandable before infrastructure-rich

This means LOOMLE should first prove value as a compact kernel before it grows
into a heavier platform.

## Installed Project Shape

The current target installed shape should stay simple:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
      ...

  loomle/
    skills/
    worklog/
    ...
```

### `Plugins/LoomleBridge/`

This remains the Unreal-side integration surface.

It owns:

- the plugin
- bridge-coupled runtime behavior
- MCP server payloads shipped with the plugin

### `loomle/`

This is the project-local LOOMLE directory.

It is visible and simple on purpose.

LOOMLE should not currently split this into visible and hidden project layers.

That earlier direction added too much structure too early.

### `loomle/skills/`

This contains the project-local role skills.

These skills are designed in skill format, but they are local project assets,
not global installed capabilities.

### `loomle/worklog/`

This contains the worklog.

This is the one collaboration directory that LOOMLE should explicitly
standardize during the lightweight kernel phase.

## What LOOMLE Standardizes Now

LOOMLE should standardize:

- the existence of the local `loomle/` directory
- the existence of `loomle/skills/`
- the existence of `loomle/worklog/`
- role skill structure
- role output contracts
- manual installation steps
- manual update steps

## What LOOMLE Does Not Standardize Yet

LOOMLE should not currently force:

- a required `design/` directory
- a required `architecture/` directory
- a required `concept/` directory
- a full project documentation taxonomy
- global skill installation
- automatic host registration
- automatic skill discovery
- global commands

Users should remain free to store design and architecture artifacts according to
their own habits.

Role skills may define output format, but they should not force a specific
storage location.

## Role Skill Model

`LOOMLE Studio` roles should still be built as skills.

Their purpose is to define:

- a stable mindset
- a clear boundary
- a clear usage condition
- a clear output contract

They should not assume:

- machine-wide installation
- automatic host discovery
- automatic invocation
- automatic handoff orchestration

The operating model should be:

- the human explicitly invokes a role
- the role responds using its defined format
- the human decides where to keep the resulting artifact

## Explicit Invocation

Roles should be explicit tools, not ambient assistants.

That means:

- no broad auto-triggering
- no assumption that the host will discover local skills
- no assumption that the host will automatically wire local role files into its
  skill picker

The product should assume deliberate invocation.

Example:

- `@Dora`

This should be treated as an explicit role call, not something that happens by
ambient routing.

## Role Example: Dora

`Dora` remains the first role skill.

Her responsibilities in the lightweight model are:

- define design intent
- clarify player-facing behavior
- structure a design brief
- maintain output quality at the format level

She should not:

- force a `design/` directory
- force one project document taxonomy
- assume global role installation

She may recommend where to store a design artifact, but the user controls the
actual storage location.

When a worklog entry is appropriate, it should go to:

- `loomle/worklog/`

## Install Model

The install model should be intentionally lightweight.

LOOMLE should provide:

- a release archive
- clear written installation steps
- clear written update steps

LOOMLE should not currently depend on:

- a maintained bootstrap script
- a machine-wide installer
- a required global CLI

The practical install flow should be:

1. download the LOOMLE archive
2. copy `Plugins/LoomleBridge/` into the Unreal project
3. copy `loomle/` into the Unreal project
4. follow the usage instructions

This is easier for both humans and coding agents to reason about.

## Update Model

The update model should also stay lightweight.

LOOMLE does not currently need a full automatic upgrader.

Instead, it should provide a clear manual update contract.

That contract should explain:

- which files are machine-provided
- which files users are expected to edit
- which paths should be preserved during update
- which paths can be refreshed from a new archive

The product should prefer a clear manual update process over premature upgrade
automation.

## Capability Tiers

LOOMLE should be understood as having two capability tiers.

### 1. Static Collaboration Tier

This tier should remain usable even when runtime connectivity is unavailable.

It includes:

- role skills
- guides
- catalogs
- examples
- worklog
- planning
- review
- design output
- architecture discussion

This tier does not depend on live Unreal runtime connectivity.

### 2. Runtime Integration Tier

This tier depends on local execution and runtime connectivity.

It includes:

- the local LOOMLE client
- the local MCP server
- Unreal bridge connectivity
- graph-native operations
- runtime `execute`
- `context`
- `graph.query`
- `graph.mutate`
- `graph.verify`

This tier should be treated as an authorization-dependent enhancement layer.

## Permission Model

Permission handling is a core product concern.

LOOMLE should not assume that the runtime tier is always available under a
host's default permissions.

The key distinction is:

- static tier should be usable under limited/default access more often
- runtime tier may require explicit permission to execute local binaries and
  launch local server processes

## Permission Failure Reality

There are at least three distinct runtime failure points:

1. the host may not allow execution of the local LOOMLE client at all
2. the LOOMLE client may run, but may not be allowed to access required project
   paths
3. the LOOMLE client may run, but may not be allowed to spawn the local MCP
   server

There is also a fourth category:

4. the client and server may run, but Unreal runtime or bridge connectivity may
   still be unavailable

These failure points should not be collapsed into one generic error.

## Product Rule For Permissions

LOOMLE should not rely on the agent to always infer that it needs to request
authorization.

Instead:

- the product should clearly describe runtime prerequisites
- runtime failure should be treated as a distinct capability state
- users and agents should be told when they are in static-only mode
- users and agents should be told when enabling runtime requires explicit local
  execution permission

## Doctor and Diagnostics

`doctor` is useful, but it should not be treated as a universal permission
solution.

Reason:

- `doctor` itself depends on being able to run the local LOOMLE client

So:

- if the host blocks execution of the client itself, `doctor` cannot help
- if the client runs but runtime connectivity fails, `doctor` can become useful

This means LOOMLE should conceptually separate:

- pre-execution permission prerequisites
- post-execution runtime diagnosis

## Current Product Direction

The near-term product should focus on:

1. strong role skills
2. strong output contracts
3. a simple local project directory
4. excellent manual install and update instructions
5. clear permission expectations
6. a clean distinction between static and runtime capabilities

It should not currently focus on:

1. global skill systems
2. host automation layers
3. full installer automation
4. heavy project structure enforcement
5. ambitious upgrade machinery

## Recommended Next Documentation Set

To support this lightweight product direction, LOOMLE should maintain a small
set of clear documents:

1. lightweight product model
2. role skill definitions
3. manual install guide
4. manual update guide
5. permission and capability model

This is enough to ship a coherent kernel without overbuilding the platform.

## Decision

LOOMLE should currently be designed as:

- a lightweight project-local kernel
- with explicit role skills
- with manual install and update
- with `loomle/skills/` and `loomle/worklog/`
- with user-controlled artifact storage
- with clear static-tier and runtime-tier separation
- with explicit permission expectations for runtime features

This should be the main product direction until the kernel proves itself strong
enough to justify heavier automation.
