# Golden Layout Examples

## Consumer-owned health-condition Helix

This example was measured from a human-refined Loomle test Blueprint. A Branch
consumes one Boolean comparison:

```text
Branch.Condition
└─ Greater
   ├─ A: Get CurrentHealth
   └─ B: Multiply
      └─ A: Get MaxHealth
```

The human placed all five nodes at the same stored X coordinate and ordered
them vertically as follows:

| Node | Stored Y | Measured visual Y range |
| --- | ---: | ---: |
| Branch | 400 | 400–494 |
| Greater | 496 | 496–562 |
| Get CurrentHealth | 560 | 560–598 |
| Multiply | 608 | 608–680 |
| Get MaxHealth | 688 | 688–726 |

### Why the pattern works

- The comparison is immediately below its Branch consumer.
- The comparison's A subtree precedes its B subtree, matching input-pin order.
- The Multiply node precedes its own MaxHealth input, producing a recursive
  depth-first ordering.
- The data block is visually owned by the Branch rather than spread across a
  large left-side fan.
- The source and destination vertical orders match, so the contained return
  wires nest instead of crossing.
- Horizontal graph span is much smaller than a fully expanded left-side layout.

The comparison has multiple linked inputs, yet Helixing remains readable. This
is a counterexample to treating parameter count alone as a reason to force
left-side layout.

### Required refinement

The measured Greater bounds end at Y 562 while Get CurrentHealth begins at Y
560, producing a two-pixel measured overlap. Preserve the ordering and compact
ownership, but repack the nodes with the smallest positive local clearance that
removes the overlap. Re-query after moving because stored grid-aligned
positions and fractional visual bounds can differ.

### Scoring exception demonstrated

The pure-node outputs sit on the right side of their nodes while the consuming
inputs sit on the left side of nodes above them. Their data edges therefore
have negative raw X progress. These are valid local Helix returns, not semantic
backtracking. Penalize them only if they escape the consumer-owned block,
intersect unrelated nodes, cross ambiguously, or become disproportionately
long.

## Left-side fallback

Prefer a left-side candidate instead of copying the example when the upstream
data is shared, the downward tree collides with another branch lane, tall nodes
make the stack dominate the execution block, or pin-ordered nesting cannot
avoid crossings. Preserve strict left-to-right dependency layers and keep the
execution spine pin-aligned.

## Local getters for distant consumers

One `Get AvatarItems` variable getter originally fed three consumers:

```text
Get AvatarItems
├─ Is Valid Index
├─ Get (a copy)
└─ For Each Loop with Break
```

Keeping one shared getter was semantically valid, but the consumers occupied
different local blocks and the fan-out produced long, crossing wires. Compare
that layout with this topology candidate:

```text
Is Valid Index          Get (a copy)          For Each Loop with Break
└─ Get AvatarItems      └─ Get AvatarItems    └─ Get AvatarItems
```

Each cheap getter is immediately adjacent to and visually owned by one
consumer. Prefer this candidate when measured placement removes crossings,
node-body hits, and ambiguous fan-out enough to justify two additional nodes.
Do not copy this pattern onto a pure function call whose cost or evaluation
behavior has not been established. In a move-only request, report this exact
candidate instead of creating it.

## Rerouting a loop-break return

An inner Branch deliberately returned execution to
`For Each Loop with Break.Break`. Three existing knots made the long
backtracking spline mostly rectangular, but live geometry showed that its last
vertical segment remained obstructed:

```text
vertical segment anchor X: approximately 2100
Get AvatarItems visual X range: 2080..2212
```

That segment proxy passed through the getter body. A fourth knot shifted the
vertical segment to approximately X 2037, outside the measured getter bounds,
then used a short horizontal segment into `Break`:

```text
inner Branch
    └─ knot ─ knot
                  │
             knot │
                  │  clear of Get AvatarItems
             knot └────────────> Break
```

The fourth knot is justified because three knots fail the hard node-hit check.
The values are measured evidence from one graph, not reusable coordinates.
For another graph, derive every corner from its current anchors and bounds,
check each segment independently, and choose the smallest valid knot count.
