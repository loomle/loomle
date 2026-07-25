# Blueprint SAL Examples

This small corpus demonstrates the current normalized SAL contract. Query and
Patch examples place canonical Result Text after `---`; that response is
illustrative and is not part of the request. The first example is a standalone
Object Text fragment.

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

- Use UE-native names and values inside brace objects; do not invent SAL type
  mappings.
- Existing objects use Target-relative stable references such as `@node-guid`.
  Graph Pins always use `@node-guid/pin-guid`; an optional semantic tag may be
  written separately, for example `node @node-guid`.
- New objects use local aliases until the executor returns their stable IDs.
- Keep pins next to their owning node and preserve the returned statement order.
- Graph Patch never compiles or saves its owning Blueprint; follow the returned
  Blueprint Target handoff with a separate Blueprint Patch.
