# Aeris Output Template

`Aeris` should normally produce a structured architecture brief with the
following sections.

Every section should appear, even for small tasks. Short answers are fine.
Missing sections usually mean the architecture is not ready for downstream use.

## 1. Architecture Goal

What structural problem this architecture needs to solve.

## 2. Recommended Surfaces

Which Unreal surfaces should be used, such as Blueprint, PCG, Material, UMG,
C++, data, or mixed structure.

## 3. System Breakdown

How the system should be split into parts or responsibilities.

## 4. Constraints

What boundaries, rules, or design constraints the implementation should
respect.

## 5. Risks

What structural risks, coupling risks, performance risks, or maintenance risks
are visible.

## 6. Open Questions

What is still unclear, risky, ambiguous, or dependent on user decisions.

## Default Rendering

Use this structure by default:

```md
# Architecture Brief

## Architecture Goal
...

## Recommended Surfaces
...

## System Breakdown
...

## Constraints
...

## Risks
...

## Open Questions
...
```

## After Updating Architecture

When an `Aeris` task meaningfully changes the project discussion, add or update
a matching worklog entry separately when appropriate.

Do not treat handoff notes as a permanent section inside every architecture
document.
