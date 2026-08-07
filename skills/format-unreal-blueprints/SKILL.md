---
name: format-unreal-blueprints
description: Format and audit Unreal Engine Blueprint K2 graphs through Loomle using exact live node and pin geometry. Use when arranging selected Blueprint nodes, repairing spaghetti, choosing compact Helixing versus left-side data layout, straightening execution flow, separating branches, or evaluating whether a graph has near-human visual quality.
---

# Format Unreal Blueprints

Format Blueprint K2 graphs as readable semantic diagrams. Preserve behavior and
use Loomle's public `editor`, `sal_query`, `sal_schema`, and `sal_patch` tools
without depending on an agent-specific tool-call syntax.

## Read the required guidance

- Read [layout-rules.md](references/layout-rules.md) before planning positions.
- Read [loomle-sal-workflow.md](references/loomle-sal-workflow.md) before
  querying or moving nodes.
- Read [golden-examples.md](references/golden-examples.md) when choosing between
  Helixing and left-side parameter layout or calibrating compactness.

## Follow this workflow

1. Call `editor` with no arguments first. Resolve the exact Blueprint Graph
   from that context or, when it is not the active surface, from an exact
   Blueprint `graphs` query. Never construct Graph identity from a display name.
   If selection is unavailable, say so and obtain an unambiguous graph or node
   identity before changing anything.
2. Query the selected region and its connected context `with layout`. Collect
   every page or neighboring query needed to understand the layout; query
   pagination does not limit which exact nodes a later patch may move.
3. Require authoritative live `visualBounds` for nodes and measured pin
   `placementAnchor` values for precise work. If Loomle reports
   `capability.layout_geometry_unavailable` and opening or focusing the exact
   Graph can resolve the reported reason, tell the user which Graph you need to
   open and ask for confirmation. After confirmation, call `editor` with
   `operation: "open"` and the canonical exact Graph Target, verify that the
   same Graph is focused, and retry the query. Do not ask the user to perform
   the opening manually. Without confirmation or live geometry, perform only
   conservative rough placement and label it as such.
4. Partition the graph into execution spines, branch lanes, consumer-owned data
   trees, shared data providers, comments, and knots. Preserve existing local
   human layout exemplars that satisfy the hard constraints.
5. Fix the execution structure first. Align connected execution pins rather
   than node tops and keep the expected continuation path straight when the
   graph semantics make that path clear.
6. Generate both Helixing and left-side candidates for each local data tree.
   Prefer the valid candidate with clearer ownership, shorter local wires, and
   less empty area. Never reject a contained Helix merely because its data
   wires travel left toward an input.
7. Validate candidates against real bounds and anchors. Reject node overlap,
   wires crossing unrelated nodes, ambiguous crossings, reversed non-loop
   execution flow, and data blocks invading neighboring execution lanes.
8. Express every chosen move explicitly with absolute `move @ref to (x, y)`.
   Never use relative `by` movement and never infer implicit comment members or
   query pages as move targets.
9. Dry-run the exact move-only patch. Inspect `valid`, `applied`, `planned`,
   `diff`, diagnostics, and each before/after coordinate. Do not apply a plan
   that differs from the intended node set or positions.
10. Apply the same absolute plan, then repeat the relevant query `with layout`
    and audit the measured result. Treat verification as incomplete until the
    readback matches the requested stored positions and the visual constraints.

Keep a Graph opened for geometry measurement open after the workflow unless
the user asks you to close it. Do not restore presentation state by closing a
Graph that is still needed for measured verification.

## Keep move-only scope

- Do not add, remove, reconnect, duplicate, or reconstruct nodes.
- Do not add `NOT`, `Sequence`, reroute, getter, comment, or knot nodes merely
  to improve appearance.
- Do not change defaults, pin links, function purity, or execution semantics.
- Report topology improvements separately. In particular, ordinary variable
  getters may sometimes be duplicated to shorten wires, but expensive pure
  functions must not be duplicated as a cosmetic shortcut.
- Keep compile and save operations separate from the move-only patch. Finalize
  the owning asset only when the user's requested workflow requires it.

## Report the outcome

State which region moved, which parameter style each data block uses, whether
live geometry was authoritative, what the post-apply audit verified, and any
remaining semantic or topology suggestions that move-only formatting could not
address.
