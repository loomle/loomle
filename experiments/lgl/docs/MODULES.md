# LGL Modules

## Intent

LGL modules own domain-specific language features. The docs should be modular
in the same way the code should be modular: adding a new domain should usually
add or update one module document, not scatter changes across unrelated
language files.

## Module Contract

Each module document should define:

- intent and UE semantic boundary
- current implemented contract, if the module already exists
- agent workflows
- object statements
- canonical text
- accepted sugar and its canonical expansion
- query statements
- patch statements
- normalized JSON object model
- diagnostics
- examples

The shared language core defines bindings, constructors, references, arrays,
inline object values, and statement lists. Modules define what those constructs
mean for their domain.

Module docs should not invent new public names when existing implementation
names are still valid. For graph, that means preserving terms such as
`Graph`, `Query`, `Patch`, `Palette`, `Target`, `GraphRef`, `Node`, `Pin`,
`PinRef`, `Edge`, `Find`, `Condition`, `Binding`, and `Op` unless a deliberate
rename is called out as a future schema migration.

## Normalization Boundary

Modules may define syntax sugar only when the rewrite is pure LGL syntax.

Allowed normalization examples:

```lgl
begin.Then -> delay.Exec/Completed -> print.Exec
```

to graph readback canonical text:

```lgl
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Patch sugar may normalize differently because statement context matters:

```lgl
connect begin.Then -> print.Exec
```

to:

```lgl
connect(begin.Then, print.Exec)
```

Not allowed in pure normalization:

- resolving whether a UE node type exists
- deciding whether a pin exists
- checking whether an edge is legal
- picking a palette entry from fuzzy text
- loading or inspecting UE assets

Those belong to adapters and the UE bridge.

Each module should explicitly separate:

- sugar text accepted for agent convenience
- canonical text emitted by the formatter
- normalized JSON validated by schema and sent across the bridge

Each module that supports queries must define:

- supported `find` forms
- default result shape without `with`
- supported `with` expansions
- supported `where` fields and operators
- supported `order by` keys
- `limit` behavior and defaults

For currently implemented modules, the normalized JSON section must be based on
`schema/lgl-object.schema.json`, not a hypothetical replacement shape.

## Planned Modules

- [`modules/graph.md`](modules/graph.md): graph objects, nodes, pins, edges,
  graph queries, and graph patches.
- [`modules/asset.md`](modules/asset.md): asset discovery, resolution,
  registry metadata, and asset-level query results.
- [`modules/blueprint.md`](modules/blueprint.md): Blueprint class contract,
  member declarations, custom events, and component tree structure.
- [`modules/widget.md`](modules/widget.md): widget tree constructors, slots,
  and widget patching.

Graph is the only module drafted against an existing implementation today.
Asset, blueprint, and widget are design drafts.
