# Loomle SAL Layout Workflow

Use Loomle's public MCP tools. The JSON tool-call wrapper is agent-specific;
the SAL text shown here is portable.

## 1. Resolve the active target

Call `editor_context` first. Copy the canonical exact Graph Target and stable
node references from the result.

If the active surface is a graph but selection is unavailable, do not invent a
selected node. Ask for an exact node identity or use another unambiguous query
the user has authorized. Do not launch, focus, or manipulate the editor UI just
to acquire geometry unless the user explicitly authorizes UI control.

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
not a rendered-size substitute. If the precise-use gate fails, ask the user to
open and synchronize the exact Blueprint graph and retry. Rough placement may
use stored facts conservatively, but must not be described as polished.

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

- On `capability.layout_geometry_unavailable`, retain stored facts, explain why
  they support only rough placement, and request the exact graph be opened.
- On an unresolved target or reference, re-read context or query stable
  identities; do not substitute display names.
- On a dry-run validation error, change the plan and dry-run again.
- On a live apply error or parity rollback, query current state before retrying.
- On incomplete graph scope, gather more queries; do not let one page or depth
  limit silently omit collision neighbors.
