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
