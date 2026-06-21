# LGL Domains

## Intent

LGL domains own UE-specific language features. The docs should be organized the
same way the runtime should be organized: adding a new domain should usually add
or update one domain document, not scatter changes across unrelated language
files.

## Domain Contract

Each domain document should define:

- intent and UE semantic boundary
- agent workflows
- object text
- canonical text
- accepted sugar and its canonical expansion
- query text
- patch text
- palette or creation-entry behavior, when the domain exposes discoverable
  creation entries
- normalized JSON object model
- diagnostics
- examples

The shared language core defines text kinds, bindings, constructors,
references, arrays, inline object values, and statement lists. Domains define
what those constructs mean for their UE area.

Domain docs should be the source of truth for the target domain language. They
may mention current implementation gaps, but the main body should describe the
final target shape instead of reading like a migration checklist.

Domain docs should keep public names stable when those names still match the
target design. Deliberate renames belong in the relevant domain document and,
for implemented areas, in migration notes.

## Normalization Boundary

Domains may define syntax sugar only when the rewrite is pure LGL syntax.

Allowed normalization examples:

```lgl
begin.Then -> delay.Exec/Completed -> print.Exec
```

to canonical graph object text:

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

Each domain should explicitly separate:

- sugar text accepted for agent convenience
- canonical text emitted by the formatter
- normalized JSON validated by schema and sent across the bridge

Each domain that supports queries must define:

- supported `find` forms
- default result shape without `with`
- supported `with` expansions
- supported `where` fields and operators
- supported `order by` keys
- pagination behavior and defaults

For currently implemented domains, the normalized JSON section should describe
the target schema and explicitly call out any current implementation gap that
must be migrated.

## Planned Domains

- [`domains/graph.md`](domains/graph.md): graph objects, nodes, pins, edges,
  graph queries, graph patches, shortcut constructors, and palette fallback
  creation.
- [`domains/asset.md`](domains/asset.md): asset discovery, resolution,
  registry metadata, and asset-level query/reference results.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint class contract,
  member declarations, custom events, component tree structure, and
  class/member/component patching.
- [`domains/widget.md`](domains/widget.md): modeled widget constructors, widget
  tree structure, slots, queries, and widget tree patching.

Graph is drafted against the current graph-first implementation. Asset,
blueprint, and widget are target designs.
