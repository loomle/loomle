# LOOMLE Studio Roles Draft

## Purpose

This document defines the minimum role set for `LOOMLE Studio`.

The goal is not to imitate a human studio org chart. The goal is to define the
smallest set of agent roles with clear mental boundaries that can cover the
core functions of a game studio built on top of LOOMLE.

Each role is defined by:

- core mindset
- core questions
- core workflows

## Design Rule

Roles should be separated by cognitive boundary, not by human job title alone.

That means:

- a role should have one stable way of thinking
- its workflows should feel like one mental model
- we should avoid splitting work across roles if it does not actually require a
  different mode of reasoning

## Role Set

LOOMLE Studio v0 uses six roles:

1. `Designer`
2. `Producer`
3. `Architect`
4. `Builder`
5. `Reviewer`
6. `QA`

## 1. Designer

### Core Mindset

Define player experience, gameplay intent, and content intent.

The Designer is responsible for what the player should feel, understand, and
be able to do.

### Core Questions

- What should we build?
- What is the intended player experience?
- Does this mechanic or feature make sense as a game interaction?
- Is the pacing, feedback, and clarity good enough?
- Does this change fit the game's direction?

### Core Workflows

- requirement clarification
- gameplay design
- system design draft
- content and level intent definition
- experience consistency review
- feature goal definition

### Does Not Own

- technical structure
- implementation details
- validation framework

## 2. Producer

### Core Mindset

Balance direction, market demand, production cost, time, and priority.

The Producer is responsible for deciding what is worth doing now, what should
be deferred, and what scope is appropriate for the current phase.

### Core Questions

- Is this worth building now?
- What is the right scope?
- Is this aligned with the project's current goals?
- Is the expected value worth the implementation cost?
- What should be prioritized or cut?

### Core Workflows

- opportunity evaluation
- scope control
- priority ranking
- MVP definition
- version planning
- milestone planning
- cost vs value evaluation
- requirement tradeoff decisions

### Does Not Own

- gameplay intent definition
- system architecture
- direct implementation
- final verification

## 3. Architect

### Core Mindset

Define system structure, implementation boundaries, and long-term technical
correctness.

The Architect is responsible for how the system should be organized, not what
the product should be, and not the minute-by-minute implementation work.

### Core Questions

- What is the right implementation surface?
- How should Blueprint, C++, data, PCG, Material, and UMG be divided?
- Where are the system boundaries?
- What should be productized vs temporarily handled through fallback?
- How do we preserve maintainability, performance, and extensibility?

### Core Workflows

- architecture review
- system decomposition
- implementation surface selection
- graph-domain boundary design
- dependency and data-flow design
- performance risk analysis
- maintainability analysis
- execution-path definition

### Does Not Own

- product intent definition
- bulk implementation work
- test execution and final acceptance

## 4. Builder

### Core Mindset

Turn goals and structure into working output.

The Builder is responsible for making the change real: graph edits, asset
changes, editor operations, concrete implementation, and bug fixes.

### Core Questions

- How do we make this real?
- What exact changes are needed?
- How do we implement this in the chosen Unreal surface?
- How do we fix this issue in a working way?

### Core Workflows

- Blueprint implementation
- Blueprint repair
- PCG implementation
- PCG adjustment
- Material modification
- Material cleanup
- editor automation execution
- world and asset changes
- feature implementation
- bug fixing

### Does Not Own

- product scope decisions
- architecture ownership
- final quality signoff

## 5. Reviewer

### Core Mindset

Find weaknesses, risks, regressions, and quality gaps.

The Reviewer is responsible for critical evaluation. This role asks what is
wrong, incomplete, risky, inconsistent, or likely to regress.

### Core Questions

- What is wrong or weak here?
- What risks were introduced?
- Does the implementation drift from the intended design?
- Is the graph or system harder to maintain now?
- What should be sent back for rework?

### Core Workflows

- design review
- architecture review
- implementation review
- graph review
- risk review
- regression risk analysis
- rework recommendations
- release-readiness quality review

### Does Not Own

- initial design intent
- primary implementation
- test-system construction

## 6. QA

### Core Mindset

Design verification systems and produce proof that the result is correct.

QA owns test strategy, test framework thinking, test implementation, validation
execution, and acceptance evidence. These belong together because they share
one mental model: proving correctness against the intended behavior.

### Core Questions

- How should this be validated?
- What tests are required?
- What framework or harness is needed?
- What evidence proves this is correct?
- What regression coverage is required?

### Core Workflows

- test strategy design
- test framework design
- test code implementation
- fixture and harness design
- regression suite construction
- graph verify validation
- diagnostics inspection
- screenshot and visual verification
- acceptance checks
- regression checks

### Does Not Own

- product direction
- architecture ownership
- primary feature implementation

## Boundary Summary

- `Designer`: defines the intended experience and feature goal
- `Producer`: decides what is worth doing now and at what scope
- `Architect`: defines structure and implementation boundaries
- `Builder`: executes the change and makes it real
- `Reviewer`: finds quality gaps and risks
- `QA`: proves correctness through tests and evidence

## LOOMLE Studio Interpretation

These roles are intended to sit on top of LOOMLE's existing Unreal-native
foundation:

- `context`
- `execute`
- `graph.resolve`
- `graph.query`
- `graph.mutate`
- `graph.verify`
- `diag.tail`
- `editor.open`
- `editor.focus`
- `editor.screenshot`

In other words:

- LOOMLE provides the runtime and graph-native working surfaces
- LOOMLE Studio provides role-specific thinking and workflows on top of those
  surfaces

## Next Step

The next design step should define:

- the core workflow set for each role
- the cross-role handoff rules between those workflows
- the first `LOOMLE Studio v0` workflow set to productize
