# Graph Migration Notes

## Intent

This note tracks remaining differences between the implemented graph-domain
experiment and the target design in [`../domains/graph.md`](../domains/graph.md).

The graph domain document is the source of truth for the final language shape.
This note should stay short and only record implementation audit items.

## Completed

The TypeScript experiment has migrated the graph path to the current
statement-list design:

- graph, query, and patch documents use explicit `asset(...)` and `graph(...)`
  bindings
- node readback uses `name = node(graph: g, type: Type, id: "...")`
- pin readback uses `node.pin = pin(type: ..., direction: ...)`
- edge chains use `pin -> pin` sugar and normalize to explicit edge JSON
- query text uses clause-per-line `find`, `where`, `with`, `order by`, and
  `page`
- patch text supports `add`, `insert`, `connect`, `disconnect`, `set`, `move`,
  `remove`, and `reconstruct`
- palette fallback creation uses `node(palette: "entry-id", defaults...)`
- stable shortcut creation uses constructors such as `delay(...)`,
  `branch(...)`, `get(...)`, and `event(...)`
- patch execution in the memory adapter preflights the whole patch before
  applying mutations
- SDK query results expose opaque `page.next` cursors

Old graph-first forms such as `alias@id: Type(...)`, `palette({id: ...})`, and
`node(..., source: PaletteBinding)` are not target syntax.

## Remaining Audit Items

- Keep schema fixtures aligned with each normalized JSON section in
  `domains/graph.md`.
- Expand negative parser tests when new sugar is added.
- Keep memory-adapter behavior as the executable model for SDK semantics, while
  leaving UE-specific validation to the future bridge adapter.
- When bridge implementation begins, compare UE behavior against the graph
  domain document first, not old graph-first notes.
