# LOOMLE Local Issues

This directory is the local issue tracker for `LOOMLE`.

Use this folder when:

- an issue needs a fuller design than a GitHub issue body can comfortably hold
- the team wants to iterate on scope, rollout, and acceptance criteria locally
- the implementation plan should live next to the repo and evolve with the code

Use GitHub issues only when they help external visibility, coordination, or
lightweight tracking. The local issue in this folder is the canonical design
record.

## Current Local Issues

1. `PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
- Parent local issue for upgrading PCG `graph.query` into the primary
  full-coverage node readback surface.
- Covers:
  - `pin_default`
  - `effectiveSettings`
  - `childGraphRef`
  - `residual_gap`
- Defines:
  - current baseline
  - priority tiers
  - target node-family models
  - rollout phases
  - acceptance criteria

2. `BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md`
- Local issue for reclassifying Blueprint nodes whose real truth spans both the
  graph node and a Blueprint-owned template object.
- Covers:
  - `UK2Node_Timeline`
  - `UK2Node_AddComponent`
  - `UK2Node_AddComponentByClass` as a nearby but distinct upgrade candidate
- Defines:
  - the `embedded-template nodes` category
  - target testing classification
  - dedicated recipe direction
  - rollout phases
  - acceptance criteria

## Recommended Structure

Each local issue should try to answer these questions clearly:

1. What problem are we solving?
2. What is the intended product direction?
3. What is already true today?
4. What are the upgrade tiers or rollout phases?
5. What is explicitly in scope and out of scope?
6. What does “done” mean?

## Naming

- Prefer all-caps file names with underscores.
- Use stable product-facing names in titles and section headings.
- Treat the local issue document as a living design record, not a frozen draft.
