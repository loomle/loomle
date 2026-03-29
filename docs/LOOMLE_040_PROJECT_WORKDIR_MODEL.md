# LOOMLE 0.4.0 Project Workdir Model

## Summary

The first `LOOMLE 0.4.0` workdir model should separate project-local content by
ownership and lifecycle.

For this phase, the project workdir should split into two top-level areas:

- `Loomle/`: machine-managed local install and local state
- `worklog/`: team-shared, human-authored collaboration content

This keeps install/update behavior predictable and keeps git policy simple.

## Problem

The project-local LOOMLE surface currently mixes several different kinds of
content:

- install and upgrade metadata
- versioned client payloads
- machine-written runtime outputs
- team-shared collaboration material

Those do not have the same owner, update policy, or git policy.

If they remain mixed together, then:

- update may overwrite content it should not own
- local state may leak into git
- team-shared work becomes harder to distinguish from install material
- non-technical users cannot tell which files are safe to edit

## Design Principle

The workdir model should be organized by owner:

1. installer-managed local product material
2. LOOMLE-managed local state
3. team-managed shared project collaboration content

The first two belong under `Loomle/`.

The third should live outside `Loomle/` in a dedicated tracked directory.

## Target Project Shape

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    README.md
    loomle(.exe)
    update.(sh|ps1)
    doctor.(sh|ps1)

    install/
      active.json
      versions/
        0.4.0/
          loomle(.exe)
          kit/
      pending/
      manifests/

    state/
      diag/
      captures/

  worklog/
    README.md
    decisions/
    workflows/
    prompts/
    memory/
```

## Directory Responsibilities

### `Loomle/`

This is the project-local LOOMLE control surface.

It should remain visible and non-hidden in this phase.

It owns:

- stable user and host entrypoints
- install and update control metadata
- versioned client payloads
- product-supplied stateless agent kit
- machine-written diagnostic and capture outputs

It should not be treated as team-shared content.

### `Loomle/install/`

This is the install and upgrade control plane.

It owns:

- active installed version selection
- installed version manifests
- pending update state
- versioned client payload directories

Recommended key file:

- `Loomle/install/active.json`

Recommended versioned payload location:

- `Loomle/install/versions/<version>/loomle(.exe)`

If LOOMLE ships stateless agent-facing reference material with a release, it
should live with the versioned payload:

- `Loomle/install/versions/<version>/kit/`

### `Loomle/state/`

This is machine-written runtime state and output.

It owns:

- diagnostics
- captures
- other runtime-generated output that should not be edited by hand

This directory is intentionally separate from install metadata.

### `worklog/`

This is the team-shared project collaboration layer.

It owns:

- decisions
- workflows
- prompts and playbooks
- shared memory
- human-authored collaboration artifacts that should be reviewed and evolve with
  the repository

It should not contain machine-managed install or runtime state.

## Git Policy

The first-cut git policy should stay simple:

- track `worklog/`
- ignore `Loomle/`
- ignore `Plugins/LoomleBridge/`

Recommended `.gitignore` intent:

```gitignore
/Loomle/
/Plugins/LoomleBridge/
```

This model intentionally does not rely on a hidden directory such as
`.loomle-core/`.

## Update Policy

The update system should own only installer-managed areas:

- `Plugins/LoomleBridge/`
- stable entrypoints under `Loomle/`
- `Loomle/install/`
- versioned payloads under `Loomle/install/versions/`
- product-owned stateless kit shipped with a version

It should not mutate:

- `worklog/`

`Loomle/state/` may be preserved or selectively cleaned by explicit policy, but
it should not be treated as the install control plane.

## Why This Is Better

This model is a better fit for the first `0.4` cut because it gives:

- a visible local directory for non-technical users
- a clear distinction between install metadata and runtime output
- an update path that does not need to reason about tracked project content
- a simple git policy with no hidden-directory requirement

## Deferred Work

The following remain out of scope for this phase:

- hidden internal directories such as `.loomle-core/`
- global machine-level LOOMLE home
- Studio-specific directory ownership
- more granular tracked/untracked mixing inside `Loomle/`

## Decision

For the first `LOOMLE 0.4.0` workdir model:

- use `Loomle/` for local install material and local state
- use `Loomle/install/` for install metadata and versioned payloads
- use `Loomle/state/` for machine-written runtime outputs
- use `worklog/` for team-shared tracked collaboration content
- ignore `Loomle/` and `Plugins/LoomleBridge/` in git
