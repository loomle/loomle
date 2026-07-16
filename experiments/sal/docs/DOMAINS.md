# SAL Domains

## Intent

SAL domains own UE-specific language features. The docs should be organized the
same way the runtime should be organized: adding a new domain should usually add
or update one domain document, not scatter changes across unrelated language
files.

## Domain Contract

Each domain document should define:

- intent and UE semantic boundary
- agent workflows
- object text
- root locator and contained-object identity scopes
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

Every target-owning domain must define one complete locator chain. The chain
starts from a UE-global address such as Asset Path or Class Path and then adds
native ids only inside their real owner scopes. A typed `object@id` is exact
inside the resolved target but is never a replacement for the target chain.
Domains must distinguish exact-name discovery, stable-id access, native
path-based identity, and Patch-local creation aliases, and must reject identity
mismatch or ambiguity without falling back to display text.

Public constructors describe SAL object shape; they are not adapter selectors
or translated UE Classes. Once a locator loads its real UE object, capability
composition follows that object's native Class and inheritance chain. A
specialized Blueprint asset therefore retains one `blueprint(...)` target and
adds the capabilities valid for its actual subclass. Domain documents remain
modular descriptions of those capabilities, not competing public target types.

For the `summary` query operation, the resolved target's composed capabilities
define the useful orientation view. Their most-specific adapter chooses the
existing SAL objects, comments, and statement order returned for that target.
The shared core does not require every
graph-like target to expose entry points, nodes, or any other common summary
object. Summary content remains domain semantics, not shared language syntax.

Domain docs should be the source of truth for the domain language. They should
describe the intended syntax, object model, query behavior, patch behavior, and
adapter boundary directly.

## Normalization Boundary

Domains may define syntax sugar only when the rewrite is pure SAL syntax.

Allowed normalization examples:

```sal
begin.Then -> delay.Exec/Completed -> print.Exec
```

to canonical graph object text:

```sal
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Patch sugar may normalize differently because statement context matters:

```sal
connect begin.Then -> print.Exec
```

to:

```sal
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

- its request target locator fields and every contained id's owner scope
- whether a Path-only first discovery differs from later exact access or Patch
- supported summary targets and the orientation each adapter returns
- supported plural collection, singular exact-name, and stable-id forms
- default result shape without `with`
- supported `with` expansions
- how `with schema` discovers one exact object, object-backed value surface, or
  creation entry
- supported `where` fields and operators
- supported `order by` keys
- pagination behavior and defaults

When several capability modules apply to one concrete UE object, the resolved
target exposes their compatible operation union. It has one target-specific
`summary` and one combined `palette`; it does not require callers to select a
module with a public `domain` field. The most specific adapter owns summary
ordering, while Palette returns every direct creation capability valid for the
resolved target as ordinary typed constructor bindings.

`with schema` has shared language semantics, but adapters own the discovered
content. They derive it from their real edit surface, UE Reflection, Graph
Schema, spawners, template objects, or other UE-owned metadata. A schema should
describe only Query operations, fields, direct Patch statements, and object
Operations the adapter can faithfully execute; identify SAL common,
adapter-modeled, native, and relationship-backed sources when relevant; and
remain valid for the exact subject and context that produced it.

Adapters return schema guidance as comments around ordinary object, value, or
creation text. They must not define domain-specific schema result objects.

Normalized JSON must be documented only after its shared contract is confirmed.
Until then, a domain should mark the envelope as deferred and explicitly call
out incompatible legacy implementation shapes rather than letting them define
the SAL text design.

## Interface Schema Cards

The formal domain documents below preserve design intent, UE mapping, complete
semantics, and implementation boundaries. They are not returned directly by
`sal.schema("<module>")`.

Each active module has a compact interface card under `interfaces/`. The card
is the normative static Text returned by `sal.schema("<module>")`. It assumes
the resident Core guide, contains only operational target, Query, Object Text,
Palette, Patch, and handoff information, and should remain approximately
800–1200 model Tokens. It never loads UE state; exact instance capabilities
remain the role of `with schema`.

Current interface cards:

- [`interfaces/README.md`](interfaces/README.md): zero-argument active-module
  index
- [`interfaces/asset.md`](interfaces/asset.md)
- [`interfaces/blueprint.md`](interfaces/blueprint.md)
- [`interfaces/class.md`](interfaces/class.md)
- [`interfaces/graph.md`](interfaces/graph.md)
- [`interfaces/widget.md`](interfaces/widget.md)

## Current Domains

- [`domains/graph.md`](domains/graph.md): graph objects, nodes, pins, edges,
  graph queries, graph patches, and Palette-backed creation.
- [`domains/asset.md`](domains/asset.md): asset discovery, resolution,
  registry metadata, and asset-level query/reference results.
- [`domains/blueprint.md`](domains/blueprint.md): Class Settings for Blueprint
  assets, variables, dispatchers, graphs, component trees, and compound
  Timeline Node backing state.
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
