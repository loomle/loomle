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
- summary behavior for each supported target type
- query text
- patch text
- palette entry behavior, when the domain exposes discoverable
  creation entries
- normalized JSON object model
- diagnostics
- examples

The shared language core defines text kinds, bindings, constructors,
references, arrays, inline object values, and statement lists. Domains define
what those constructs mean for their UE area.

For the `summary` query operation, the owning adapter defines the useful
orientation view. It chooses the existing LGL objects, comments, and statement
order returned for that target. The shared core does not require every
graph-like target to expose entry points, nodes, or any other common summary
object. Summary content remains domain semantics, not shared language syntax.

Domain docs should be the source of truth for the domain language. They should
describe the intended syntax, object model, query behavior, patch behavior, and
adapter boundary directly.

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

- supported summary targets and the orientation each adapter returns
- supported plural collection, singular exact-name, and stable-id forms
- default result shape without `with`
- supported `with` expansions
- how `with schema` discovers fields for an exact instance or creation entry
- supported `where` fields and operators
- supported `order by` keys
- pagination behavior and defaults

`with schema` has shared language semantics, but adapters own the discovered
content. They derive it from their real edit surface, UE Reflection, Graph
Schema, spawners, template objects, or other UE-owned metadata. A schema should
describe only fields the adapter can faithfully read or write, identify LGL
common, adapter-modeled, native, and relationship-backed sources when relevant,
and remain valid for the exact subject and context that produced it.

Adapters return schema guidance as comments around ordinary object or creation
text. They must not define domain-specific schema result objects.

For currently implemented domains, the normalized JSON section should describe
the current schema and explicitly call out any implementation gap that affects
agent-facing behavior.

## Current Domains

- [`domains/graph.md`](domains/graph.md): graph objects, nodes, pins, edges,
  graph queries, graph patches, and Palette-backed creation.
- [`domains/asset.md`](domains/asset.md): asset discovery, resolution,
  registry metadata, and asset-level query/reference results.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint class contract,
  variables, dispatchers, graphs, component trees, and compound Timeline Node
  backing state.
- [`domains/class.md`](domains/class.md): Class Reflection identity, hierarchy,
  Properties, Functions, Parameters, Metadata, effective Class Defaults, and
  Blueprint-backed Defaults patches.
- [`domains/widget.md`](domains/widget.md): authored Widget objects, tree views,
  Panel Slot and Named Slot relationships, queries, Palette-backed creation,
  and widget tree patching.

The TypeScript experiment includes parser, formatter, schema, and in-memory
adapter coverage for graph, asset, Blueprint, and widget, but some implemented
query text still reflects the earlier `find`-centric design. The Class domain
and UE-backed adapters remain separate implementation work.
