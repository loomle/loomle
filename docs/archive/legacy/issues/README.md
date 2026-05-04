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

3. `GRAPH_QUERY_SURFACE_MODEL.md`
- Shared local issue for defining one cross-graph `graph.query` surface model.
- Covers:
  - `pin_default`
  - `effectiveSettings`
  - `childGraphRef`
  - `residual_gap`
  - likely future promoted surface categories
- Defines:
  - shared design rules
  - graph-type mapping
  - rollout phases
  - acceptance criteria

4. `BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md`
- Local issue for promoting Blueprint residual gaps into real product
  capabilities instead of leaving them as one accounting bucket.
- Covers:
  - `embedded_template`
  - `graph_boundary_summary`
  - `context_sensitive_construct`
- Defines:
  - target node families
  - proposed query shapes
  - rollout order
  - acceptance criteria
- Current status:
  - Blueprint catalog has now promoted these lanes into product vocabulary
  - current documented Blueprint `residual_gap` count is `0`

5. `JOBS_LONG_RUNNING_TASK_RUNTIME.md`
- Local issue for introducing a shared long-task runtime with top-level `jobs`
  management, action-based lifecycle inspection, and tool-level
  `execution.mode = "job"` submission.
- Covers:
  - top-level `jobs`
  - tool-level job-mode submission
  - long-task lifecycle
  - job registry and polling model
  - serial-first scheduling for Unreal workloads
- Defines:
  - first-version tool shape
  - protocol draft for submission and polling
  - job state model
  - session relationship
  - rollout phases
  - acceptance criteria

6. `PROFILING_RUNTIME_ANALYSIS_INTERFACE.md`
- Local issue for introducing a top-level `profiling` interface as an official
  Unreal profiling data bridge instead of forcing agents to infer performance
  state from screenshots or raw console text.
- Covers:
  - `unit`
  - `game`
  - `gpu`
  - `ticks`
  - `memory`
  - `capture`
- Defines:
  - official-family action model
  - native data-shape rules
  - direct structured bridge targets
  - jobs integration for heavy captures
  - rollout phases
  - acceptance criteria
- Current status:
  - `profiling.action = "unit"` is implemented
  - `profiling.action = "game"` is implemented
  - `profiling.action = "gpu"` is implemented
  - `profiling.action = "ticks"` is implemented
  - `profiling.action = "memory"` is implemented for `kind = "summary"`
  - `capture` and memory modes beyond `summary` remain follow-up work
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
