# Authorized Blueprint Topology Follow-ups

Use this workflow only after move-only formatting reports a concrete local
getter-duplication or reroute recommendation and the user explicitly authorizes
that topology change. Keep the authorized node and edge scope exact.

## Resolve native creation semantics

Use `sal_schema` and the Graph Palette rather than guessing a getter, reroute,
pin name, or contextual creation entry. Query the exact existing consumers,
edges, and obstacle geometry before changing topology.

## Materialize stable nodes first

Create each authorized getter or reroute through an explicit Graph Patch. Dry
run the creation, inspect the complete plan, then apply the same creation.
After every applied creation, query the exact Graph again and capture the new
stable Node and Pin identities. Do not carry a creation alias or provisional
Pin identity into a later request.

For several reroutes, create and resolve them incrementally so every node in
the final path has stable identity and measured geometry. Recalculate later
corners when an earlier knot's measured anchor differs from its requested
position.

## Rewire exact edges

Build one explicit ordered rewiring plan from stable references:

1. Record every old exact edge that the authorized change replaces.
2. Record every new exact edge in source-to-destination order.
3. Disconnect each replaced old edge before connecting a new source to its
   already-linked destination input.
4. Express every path segment as its own `connect` statement. Never assume that
   connecting another execution source replaces the existing incoming edge.
5. Dry run the complete rewiring plan and require the planned operations to
   match the intended old and new edge sets exactly.
6. Apply the same ordered plan without adding unrelated cleanup.

For a duplicated getter, replace only the old getter-to-consumer edge, connect
the matching new getter output to that exact consumer input, and preserve every
other consumer until its own authorized replacement is ready. Remove the
original getter only when an exact query proves it has no remaining required
consumers.

For a reroute path, replace the original edge with the full stable chain. If an
older knot path reaches the same destination, explicitly disconnect that old
path before connecting the new final segment.

## Measure and clean up

Re-query the affected context and flow after rewiring. Verify:

- the exact intended edges exist and the replaced edges do not;
- no unintended additional incoming execution edge exists;
- every new node and relevant Pin has authoritative live geometry;
- each reroute segment proxy clears unrelated `visualBounds`;
- local getters remain inside their consumer-owned blocks;
- no disconnected or semantically ambiguous region was introduced.

Only after the replacement path passes should you dry-run and remove exact
obsolete reroute or getter nodes. Query once more after cleanup. Compile and
save the owning Blueprint through a separate finalization Patch when the user
requested persistence; do not mix finalization into topology edits.

## Stop on scope or identity drift

If a stable reference no longer resolves, an old edge differs from the plan,
or live geometry changes the proposed route, stop and re-query. Do not replace
an edge by display name, reconnect an approximate Pin, or broaden the user's
authorized topology scope.
