# Blueprint K2 Layout Rules

## Contents

1. Priority model
2. Semantic partitioning
3. Execution layout
4. Data layout candidates
5. Branches, comments, and reroutes
6. Geometry and scoring
7. Community basis

## Priority model

Evaluate layout in this order. A lower-priority improvement must not violate a
higher-priority rule.

1. Preserve graph topology and authored behavior.
2. Eliminate node overlap and wires crossing unrelated node bodies.
3. Keep non-loop execution flow readable, forward, and pin-aligned.
4. Minimize ambiguous crossings and make every connection's endpoints clear.
5. Keep each local data dependency close to its consumer.
6. Match data-node order to the consumer's input-pin order.
7. Reduce unnecessary wire length, bounding area, and empty space.
8. Match a valid local human-authored exemplar before imposing a generic style.

Treat compactness as a readability tool, not as a license to overlap nodes or
hide pin relationships.

## Semantic partitioning

Classify nodes before placing them:

- **Execution spine**: the primary left-to-right chain of impure nodes.
- **Branch lane**: one path leaving a Branch, Sequence, Switch, or other
  multi-exec node.
- **Consumer-owned data tree**: pure upstream nodes used only by one local
  consumer.
- **Shared provider**: a data node whose output feeds multiple consumers or a
  distant region.
- **Structural presentation**: comments and knots that describe or route an
  existing region.

Do not force a shared provider into one consumer's local block. Do not let a
consumer-owned data tree dictate the position of the execution spine.

## Execution layout

- Arrange ordinary execution flow from left to right.
- Align the centers of connected execution-pin anchors. Allow node tops to be
  staggered when node heights or pin rows differ.
- Prefer a short horizontal white execution wire over a straight data wire.
- Keep the likely continuation or happy path horizontal when semantics are
  unambiguous. Place terminal, exceptional, or less likely paths below it.
- Do not insert a Boolean `NOT` to make a preferred branch occupy the straight
  lane. Layout must follow semantics rather than rewrite them.
- Stack branch lanes in output-pin order unless semantic continuation gives a
  stronger reason. Reserve the complete height of each lane, including its
  local data trees.
- Preserve deliberate loops. Distinguish a semantic loop from accidental
  execution backtracking.

## Data layout candidates

Generate at least a Helixing and a left-side candidate when the surrounding
space permits both. Choose by measured validity and readability rather than a
single global parameter style.

### Helixing

Use Helixing for a compact, mostly single-consumer data tree when the area
below the consumer is free.

1. Place the root producer immediately below its consumer.
2. Traverse upstream producers depth-first in consumer input-pin order from
   top to bottom.
3. Keep sibling subtrees in the same order as their destination pins. This
   prevents nested return wires from crossing.
4. Prefer a consistent left edge for small nodes, but shift a node when real
   widths, tall pins, or wire clearance require it.
5. Keep the whole tree visually owned by the consumer and out of adjacent
   branch lanes.
6. Permit short, contained U-turn or S-shaped data wires. Do not count them as
   harmful reverse flow solely because source X is greater than destination X.
7. If a callable node has a distinct Target or context input, consider placing
   that provider above the callable and the remaining parameters below it.
   Preserve pin order and use this mixed form only when it reduces ambiguity.

Helixing remains a candidate even when a consumer has multiple linked inputs.
Reject it based on actual crossings, height, sharing, or lane invasion rather
than parameter count alone.

### Left-side layout

Use left-side layout when it produces a clearer strict data flow.

1. Place the data tree to the left of its consumer.
2. Rank nodes by dependency depth so data progresses left to right.
3. Order nodes within each rank by destination input-pin order.
4. Reserve enough horizontal room for node widths and pin-to-pin wires without
   pushing the execution spine apart unnecessarily.
5. Keep shared providers outside consumer-owned blocks and route their fan-out
   as a separate concern.

Prefer left-side layout when any of these conditions holds:

- the data tree is tall enough to invade the next branch lane;
- several independent parameter trees create ambiguous nested return wires;
- a provider feeds multiple consumers or a distant region;
- a tall node or expanded pin set makes a downward stack disproportionate;
- the Helixing candidate creates a node hit, crossing, or unreadable long loop;
- the local project already uses a consistent valid left-side exemplar.

### Hybrid layout

Allow small local leaves to Helix below a consumer while keeping a large or
shared data chain on the left. Treat each resulting block as a separate region
and re-run collision and ownership checks.

## Branches, comments, and reroutes

- Keep branch outputs visually separated; include each output's data block in
  the lane height.
- Use comments for meaningful behavior groups, not for every small calculation.
  Lay out contained nodes first, then size or move the comment with consistent
  padding when comments are explicitly in scope.
- Recommend reroutes only for long edges, obstacle avoidance, delayed fan-out,
  or unavoidable backtracking. Do not equate reroute count with quality.
- In move-only work, report a needed reroute, duplicated getter, comment, or
  refactor without creating it.

## Geometry and scoring

Use live `visualBounds` and measured pin `placementAnchor` values. Do not use
stored `size` as rendered collision geometry.

### Hard acceptance checks

- Node-to-node body overlap: zero.
- Wire proxy through an unrelated node body: zero.
- Non-loop execution edge that reverses across regions: zero.
- Data block crossing into an unrelated branch lane: zero.
- Unexplained or visually ambiguous crossings: zero where nodes can be moved to
  remove them.
- Requested stored positions differ from post-apply readback: zero.

Use a small positive visual clearance determined from the local graph's density
and grid. There is no universal pixel spacing. Snap final stored positions to
the graph's established grid where possible, then remeasure because fractional
visual bounds may still overlap.

### Soft comparisons

Compare valid candidates using:

- execution-pin Y error;
- total local data-wire length;
- distance from each data tree to its consumer;
- number and ambiguity of crossings;
- local block bounding area and unused whitespace;
- consistency with nearby valid human-authored blocks.

Pin anchors and node bounds do not reveal the exact rendered spline. A straight
anchor-to-anchor segment can serve as a conservative node-hit proxy, but do not
claim exact spline crossing counts from that proxy. For a Helix, also verify
that source and destination vertical orders match; this is more meaningful than
requiring every data edge to have positive X progress.

## Community basis

These rules synthesize production and community conventions rather than one
formatter's output:

- [Allar UE style guide](https://github.com/Allar/ue5-style-guide/blob/main/README.md#34-blueprint-graphs): align wires rather than node tops and prioritize white execution lines.
- [Blueprint Assist format commands](https://blueprintassist.github.io/features/command-list/#format-node): defines Helixing and left-hand-side parameter styles.
- [Blueprint Assist formatting settings](https://blueprintassist.github.io/miscellaneous/settings/#formatting-options): limits Helixing by parameter height and expands dense connections.
- [OpenUnrealConventions Blueprint guide](https://jonasreich.github.io/OpenUnrealConventions/Blueprint/): favors short, straight, compact connections and pin-ordered parameter placement.
- [RewindGravity organization guide](https://rewindgravity.com/2017/06/14/how-to-keep-your-blueprint-code-organized-and-easy-to-read/): keeps inputs local, often below consumers, and repeats cheap getters to avoid long wires.
- [TechArtHub Blueprint organization guide](https://techarthub.com/tips-for-blueprint-organization-in-unreal-engine/): describes the compactness/readability tradeoff between below and side placement.
- [Blueprint Auto Layout](https://www.alexcoulombepresents.com/repos/blueprint-auto-layout): uses live sizes, pin-aware execution alignment, branch separation, and crossing minimization.
- [Phil Raharu on pure functions](https://raharuu.github.io/unreal/blueprint-pure-functions-complicated/): explains why expensive pure functions must not be duplicated as though they were cached getters.
