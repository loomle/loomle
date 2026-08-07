# Loomle SAL Layout Workflow

Use Loomle's public MCP tools. The JSON tool-call wrapper is agent-specific;
the SAL text shown here is portable.

## 1. Resolve the active target

Call `editor` with no arguments first. When it returns the intended Graph,
copy its canonical exact Graph Target and stable node references.

If the intended Graph is not the active surface, resolve its canonical Target
through the owning Blueprint interface before requesting permission to open
it. Start from an authorized Blueprint Asset Path, query its `summary` to obtain
the exact Blueprint identity, then query `graphs` or an exact `graph <name>`.
Copy the returned Graph Target with its `asset`, `blueprintId`, and `id`; never
construct these identities from a display name or stale remembered GUID.

If the active surface is a graph but selection is unavailable, do not invent a
selected node. Ask for an exact node identity or use another unambiguous query
the user has authorized.

Live geometry may require opening or focusing the exact Graph presentation.
Before calling `editor` with `operation: "open"`, tell the user which Blueprint
Graph will be opened and ask for confirmation. A formatting request does not by
itself authorize changing the visible Editor presentation. Once confirmed,
perform the open operation yourself; do not ask the user to navigate there
manually. Do not close a presentation after measurement unless the user asks.

Use `sal_schema` with module `graph` when an operation or result field is
unclear. Do not guess Graph syntax, palette identities, pins, or operations.

## 2. Query semantics and layout

Bind the exact Graph Target returned by Loomle:

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

Choose the query operation for the semantic region required by the task.
`with layout` enriches the nodes and pins already selected by that operation;
it does not create a snapshot, region, or layout-status object.

Collect more pages or neighboring queries when needed. A query page is only a
query result boundary. It does not define an atomic formatting region or limit
the exact nodes a later patch may name.

### Precise-use gate

Proceed with near-human placement only when:

- every applicable returned node has finite `visualBounds`;
- every relevant presented pin has `visualState: measured` and a finite
  `placementAnchor`;
- intentionally hidden pins explain their missing geometry with
  `geometryReasons`;
- the response has no `capability.layout_geometry_unavailable` warning.

Stored `at` is authoritative stored position. Stored `size`, when present, is
not a rendered-size substitute.

If the precise-use gate fails, inspect the diagnostic reason. When opening or
focusing the exact Graph can resolve it:

1. Ask the user to confirm opening the named exact Graph.
2. After confirmation, pass its bare canonical Target text to `editor`:

   ```text
   editor({
     operation: "open",
     target: "target { domain: graph, asset: \"/Game/BP_Example.BP_Example\", blueprintId: \"11111111-1111-1111-1111-111111111111\", id: \"22222222-2222-2222-2222-222222222222\" }"
   })
   ```

3. Require a successful terminal Editor result.
4. Call `editor` with no arguments and verify that its exact Graph Target
   matches the requested Target.
5. Retry the same `with layout` query. If the surface is still synchronizing,
   re-read context and retry once more rather than opening additional windows.

`editor open` establishes and focuses the real UE Graph presentation; it does
not prove that geometry is authoritative. Apply the complete precise-use gate
to the new Query result. If an active drag or other user interaction blocks
capture, ask the user to finish that interaction and then retry. Without user
confirmation or live geometry, rough placement may use stored facts
conservatively but must not be described as polished.

## 3. Build an explicit absolute plan

Name each node independently and use integer absolute coordinates:

```sal
patch g dry run
move @first-node-guid to (320, 0)
move @second-node-guid to (720, 32)
```

Never use `by`. Never treat comment containment, selection, query pagination,
or graph traversal as an implicit move target.

Keep the dry run move-only so Loomle can return the complete ordered move plan
and rich Graph diff. Check:

- `isError` is false;
- `valid` is true;
- `applied` is false;
- every `planned.operations` entry names the intended node and requested `to`;
- every `before.at` and `after.at` is plausible;
- no unexpected diagnostics exist;
- `diff.changes` contains exactly the changed moves, while planned no-ops may
  remain outside the diff.

A dry run is advisory and does not reserve the graph between requests. If the
graph may have changed, query again before applying.

## 4. Apply and verify

Apply the same ordered absolute moves:

```sal
patch g
move @first-node-guid to (320, 0)
move @second-node-guid to (720, 32)
```

Then repeat the relevant query `with layout`.

Verify both layers:

1. Stored `at` equals every requested `to` coordinate.
2. New live bounds and anchors pass the geometric and readability checks in
   [layout-rules.md](layout-rules.md).

Do not infer visual success from a successful patch alone. Movement does not
implicitly compile or save. Keep any owning-asset finalization separate from
the move-only patch and perform it only when the requested workflow requires
persistence.

## 5. Handle failure without guessing

- On `capability.layout_geometry_unavailable`, retain stored facts and inspect
  the reason. Ask permission to open or focus the exact Graph when that can fix
  the reason; after confirmation, use `editor open` yourself and re-run the
  precise-use gate.
- On an unresolved target or reference, re-read context or query stable
  identities; do not substitute display names.
- On a dry-run validation error, change the plan and dry-run again.
- On a live apply error or parity rollback, query current state before retrying.
- On incomplete graph scope, gather more queries; do not let one page or depth
  limit silently omit collision neighbors.
