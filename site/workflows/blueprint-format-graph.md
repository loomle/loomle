---
layout: default
title: Format a Blueprint Graph
parent: Workflows
nav_order: 1
description: Format a Blueprint K2 graph with resident workflow guidance, live geometry, absolute move-only patches, and measured readback.
---

# Format a Blueprint Graph

Loomle's resident `format-unreal-blueprints` Skill turns Blueprint formatting
into a measured inspect, plan, move, and verify workflow. It preserves graph
topology and behavior: formatting moves existing nodes only.

## 1. Load the Resident Skill

```text
agent_skill({ name: "format-unreal-blueprints" })
```

Follow the returned `SKILL.md` and references. They define the current layout
rules, precise-use gate, candidate styles, and acceptance checks that match the
installed Loomle version.

## 2. Resolve the Exact Graph and Selection

Open the intended Blueprint Graph, select the region when the request is
selection-scoped, and call:

```text
editor_context({})
```

Copy the returned exact Graph Target and StableRefs. Do not infer a selected
node from the visible UI when selection is unavailable.

## 3. Read Semantics and Live Layout

Query the selected or connected region with layout information:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Example.BP_Example",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query g
context @consumer-node-guid depth 3
with layout
```

Gather neighboring queries or additional pages until the complete formatting
region and its collision context are understood. Query pagination does not
define the nodes a later Patch may move.

Near-human placement requires finite live `visualBounds` for applicable nodes
and measured pin `placementAnchor` values. Stored `at` is the authoritative
stored position; stored `size` is not rendered collision geometry. If Loomle
reports `capability.layout_geometry_unavailable`, open and synchronize the
exact Graph and retry. Without live geometry, only conservative rough placement
is justified.

## 4. Plan the Layout

Partition the region into execution spines, branch lanes, consumer-owned data
trees, shared providers, comments, and knots.

- Align connected execution pins and preserve forward execution flow.
- Keep each local data dependency close to its consumer.
- Compare compact Helixing below a consumer with a strict left-side data
  layout; choose using measured bounds, anchors, ownership, wire clarity, and
  local graph density.
- Reject overlap, wires through unrelated node bodies, ambiguous crossings,
  reversed non-loop execution flow, and data blocks invading another branch
  lane.

Do not add, remove, reconnect, duplicate, or reconstruct nodes as part of this
move-only workflow.

## 5. Dry-Run Explicit Absolute Moves

Name every moved node independently and use absolute coordinates:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Example.BP_Example",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

patch g dry run
move @first-node-guid to (320, 0)
move @second-node-guid to (720, 32)
```

Inspect `valid`, `applied`, `planned`, `diff`, diagnostics, and every before and
after coordinate. The planned node set and positions must exactly match the
intended move plan. Never use relative `by` movement or treat selection,
comments, query depth, or pagination as implicit move targets.

## 6. Apply and Measure Again

Apply the same ordered absolute moves with `dry run` removed, then repeat the
relevant query `with layout`.

Verify that every stored `at` equals its requested `to`, and that the new live
bounds and pin anchors pass the layout checks. A successful Patch alone does
not prove visual quality.

Movement does not implicitly compile or save. Keep owning-Blueprint
finalization separate and perform it only when the requested workflow requires
persistence.
