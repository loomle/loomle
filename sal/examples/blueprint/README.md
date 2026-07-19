# Blueprint SAL Examples

This small corpus demonstrates the current normalized SAL contract. Query and
Patch examples may place a possible Object Text response after `---`; the
response is illustrative and is not part of the request.

## Core

- `01-ordered-object-text.sal`
- `02-blueprint-summary.query.sal`
- `03-search-nodes.query.sal`
- `04-exec-flow.query.sal`
- `05-edit-graph.patch.sal`

## Extended

- `06-insert-node.patch.sal`
- `07-widget-tree.query.sal`

## Reference

- `08-palette-schema.query.sal`
- `09-reference-query.query.sal`

## Maintenance

- Use UE-native names and values inside constructors; do not invent SAL type mappings.
- Existing objects use typed stable references such as `node@id` and `pin@id`.
- New objects use local aliases until the executor returns their stable IDs.
- Keep pins next to their owning node and preserve the returned statement order.
