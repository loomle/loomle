# LOOMLE Lite Kernel Model

## Summary

This document defines a deliberately lighter product direction for LOOMLE.

The goal is not to maximize automation.

The goal is to ship a small, sharp core that is easy for humans or agents to
install, understand, and use without a heavy installer, global commands, or
machine-wide setup.

This direction intentionally simplifies several earlier ideas.

## Product Decision

LOOMLE should currently be shaped as a lightweight project-local kernel.

That means:

- no global LOOMLE install
- no machine-level LOOMLE home
- no maintained bootstrap/install scripts
- no required global command
- no automatic skill discovery requirement
- no forced project document layout beyond the LOOMLE folder itself

Instead, LOOMLE should provide:

- a clear manual install flow
- a downloadable archive
- a project-local `loomle/` directory
- project-local role skills
- a project-local worklog location
- role definitions that specify output format, not storage mandates

## Why This Simpler Direction

The heavier direction introduced too much operational weight too early.

Problems with the heavier direction:

- install flow became too complex
- global vs project-local responsibilities became harder to explain
- users had to trust more automation before seeing value
- upgrade design became larger than the product core
- the product risked becoming infrastructure-heavy before the role/skill kernel
  was proven

LOOMLE should first prove that the kernel itself is valuable:

- sharp role skills
- good output contracts
- clean project-local usage
- explicit user control

## Core Product Shape

The current target shape should be:

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

This is intentionally minimal.

The `loomle/` directory is the project-local LOOMLE home.

It is visible, simple, and directly understandable.

## Directory Model

### `Plugins/LoomleBridge/`

This remains the Unreal integration surface.

It contains the plugin and related runtime pieces that belong with the Unreal
side.

### `loomle/`

This is the project-local LOOMLE directory.

It is the visible collaboration and role-skill directory for the project.

For now, LOOMLE should not split this into separate visible and hidden project
layers.

That split can be revisited later if the product proves it needs it.

### `loomle/skills/`

This should contain the project-local role skill material.

The skills are designed in skill format, but they are not required to be
globally installed or automatically discovered.

They exist as explicit local project assets.

### `loomle/worklog/`

This should hold worklog artifacts.

Worklog is the one project-local output directory that LOOMLE should explicitly
standardize for now.

This creates a minimal shared memory surface without forcing a whole project
documentation taxonomy.

## What LOOMLE Should Not Standardize Yet

LOOMLE should not currently force:

- `design/`
- `architecture/`
- `concept/`
- any mandatory project-wide artifact directory taxonomy

Role skills may define output structure, but they should not force users to
store those outputs in a specific directory.

Users should be free to keep design and architecture artifacts wherever fits
their project habits.

This is important because different users and teams already have different
documentation conventions.

## Role Skill Philosophy

Roles should still be designed in skill format.

But their role is now narrower and clearer:

- define mindset
- define boundaries
- define output format
- define when to use the role

They should not assume:

- global install
- host-level auto-registration
- automatic discovery
- automatic orchestration

The expected usage model is:

- the human explicitly invokes the role
- the role responds in its defined format
- the human decides where the resulting artifact should live

## Explicit Invocation Model

Role skills should be treated as explicit tools, not ambient assistants.

That means:

- no broad automatic triggering
- no assumption that the host will auto-discover local skills
- no requirement to implement auto-registration before the product kernel ships

The intended operating model is:

- user or agent opens the local LOOMLE role skill
- user explicitly invokes the role by name
- role follows its defined output contract

This keeps LOOMLE intentional and low-friction.

## Dora Example

`Dora` still exists as a role skill.

But under this lighter model:

- `Dora` defines the design-output format
- `Dora` does not force design artifacts into a prescribed `design/` directory
- `Dora` may recommend structure, but the user controls storage
- `Dora` should write work progress into `loomle/worklog/` when a worklog entry
  is appropriate

This preserves the core value of the role without forcing a large project
structure decision.

## Install Model

The install model should now be intentionally simple.

LOOMLE should provide:

- a release archive
- clear written installation steps
- a clear written update process

LOOMLE should not currently depend on:

- maintained bootstrap scripts
- machine-level installer commands
- global update commands

The practical install model becomes:

1. download the LOOMLE archive
2. copy the plugin into the Unreal project
3. copy the `loomle/` directory into the Unreal project
4. follow the written setup/usage instructions

This is easier for both humans and coding agents to understand.

## Upgrade Model

Upgrade should also be lightweight.

For now, LOOMLE does not need a full automatic incremental upgrader.

Instead, LOOMLE should document:

- which project-local paths are machine-provided
- which project-local paths users are expected to edit
- which paths should be manually updated from a new archive release
- which paths should be preserved

The product should prefer a clear manual update contract over an overbuilt
installer.

## Release Philosophy

LOOMLE should first ship as a precise manual product, not as a highly automated
platform.

That means the near-term release value is:

- good plugin/runtime package
- good local role skills
- good project-local organization
- good written instructions

Not:

- heavy installation machinery
- aggressive automation
- hidden complexity

## Scope Of Standardization

For this lighter kernel phase, LOOMLE standardizes only:

- project-local `loomle/`
- project-local `loomle/skills/`
- project-local `loomle/worklog/`
- role-skill definitions
- role output formats
- manual installation and manual upgrade instructions

LOOMLE does not standardize yet:

- a universal project documentation tree
- global skill installation
- automatic host integration
- machine-wide LOOMLE orchestration

## Immediate Product Direction

The near-term product should therefore focus on:

1. making the role skills strong
2. making their output contracts clean
3. making `loomle/` easy to understand
4. writing excellent manual install/update documentation
5. shipping an archive that a human or AI can apply without special tooling

## Decision

LOOMLE should currently be refined as a lightweight project-local kernel.

Its defining properties should be:

- project-local, not global
- explicit, not ambient
- manual-installable, not installer-dependent
- output-contract-driven, not storage-taxonomy-heavy
- small and sharp before broad and automated
