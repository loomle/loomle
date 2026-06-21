# LGL Overview

## Intent

LGL is an agent-facing, line-oriented text language for Unreal Engine work. It
lets agents describe, query, and patch UE objects with compact text while the
bridge keeps a strict normalized JSON contract for validation, RPC, generated
types, and C++ codecs.

Agents should read and write LGL text. The bridge should execute normalized
JSON. Parsers, validators, and adapters own the conversion between them.

## Mental Model

LGL separates three concerns:

- text kinds: what the agent is trying to do
- text and JSON forms: how text becomes the bridge contract
- domains: which UE semantic area owns the meaning

The top-level language model should not be a union such as `Graph | Query |
Patch | Palette`. `Graph`, `Asset`, `Blueprint`, and `Widget` are domains.
`Palette` is discovery data or a patch binding source inside relevant domains,
not a top-level LGL text kind.

## Text Kinds

LGL has three agent-facing text kinds.

### Object Text

Object text describes UE objects, object fragments, or query results:

```lgl
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)

begin = node(graph: g, type: EventBeginPlay, id: "A001")
print = node(graph: g, type: PrintString, id: "A002", InString: "Ready")

begin.Then -> print.Exec
```

Object text is used for snapshots, snippets, search results, palette bindings,
and patch results.

### Query Text

Query text asks a domain to find or expand information. Query results should be
returned as object text:

```lgl
query g
find palette entry "Print String"
with pins
page limit 50
```

Each domain defines its default result shape, supported `find` forms, `where`
fields, `with` expansions, ordering, and pagination behavior.

### Patch Text

Patch text asks a domain to mutate an object:

```lgl
patch g
print = node(graph: g, type: PrintString, InString: "Ready")
add print
connect begin.Then -> print.Exec
```

Patch text may contain bindings, domain operations, and sugar statements. Patch
execution resolves and validates the whole patch before applying mutations.

## Palette

Palette is not a top-level text kind. It is the shared mechanism for discovering
stable creation entries that can be copied into patch text.

Agents should not create new UE content by guessing display names, node classes,
or editor menu text. A domain can expose a palette query that returns copyable
object text:

```lgl
query g
find palette entry "Print String"
with pins

PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")
PrintString.Exec = pin(type: exec, direction: in)
PrintString.InString = pin(type: string, direction: in)
PrintString.Then = pin(type: exec, direction: out)
```

Patch text can then use the stable palette id directly:

```lgl
patch g
print = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
add print
```

Domains may also define shortcut constructors for stable common creation intents.
Shortcut constructors and palette ids are both creation-entry mechanisms:
constructors are compact modeled forms, while palette ids are the explicit
fallback for unmodeled, plugin-defined, or otherwise editor-discovered entries.

## Text And JSON Forms

LGL text has three forms:

```text
sugar text -> canonical text -> normalized JSON
```

Sugar text is optional and agent-facing. Canonical text removes shorthand while
staying readable. Normalized JSON is the schema and bridge contract.

The language core defines shared statement, expression, value, binding,
constructor, reference, array, inline object, query, patch, and normalization
rules. Domain documents define what those constructs mean for UE concepts.

## Domains

Domains own UE-specific meaning. A domain defines its object text, query text,
patch text, normalized JSON, diagnostics, and examples.

- Graph: graph identity, node text, pin text, edge/path text, graph queries,
  graph patches, shortcut node creation, and palette fallback creation.
- Asset: asset discovery, asset identity, Asset Registry metadata, and asset
  query/reference results. Asset mutation is a later asset-tools concern.
- Blueprint: Blueprint class contract, variables, functions, dispatchers,
  custom events, components, class/member/component patches, and
  component-bound event references used by graph creation.
- Widget: widget tree structure, modeled widget constructors, slots, widget
  properties, widget queries, and widget tree patches.

Additional domains should follow the same shape instead of adding new global
language categories.

## Documentation Map

- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared LGL syntax and text-to-JSON
  principles.
- [`DOMAINS.md`](DOMAINS.md): domain document contract and normalization
  boundary.
- [`domains/graph.md`](domains/graph.md): graph domain design.
- [`domains/asset.md`](domains/asset.md): asset domain design.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint domain design.
- [`domains/widget.md`](domains/widget.md): widget domain design.
- [`notes/graph-migration.md`](notes/graph-migration.md): migration notes for
  remaining graph-domain implementation differences.

Some implementation documents still describe the graph-first experiment. Treat
them as factual implementation context until the schema and runtime are migrated
to the current language design.
