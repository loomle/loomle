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
`Palette` is the global creation-discovery concept inside domains, not a
top-level LGL text kind.

## Text Kinds

LGL has three agent-facing text kinds.

### Object Text

Object text describes UE objects, object fragments, or query results:

```lgl
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)

begin = node(graph: g, id: "A001", type: "/Script/BlueprintGraph.K2Node_Event")
print = node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")

begin.then -> print.execute
```

Object text is used for snapshots, snippets, search results, palette creation
entries, and patch results.

### Query Text

Query text asks a domain to find or expand information. Query results should be
returned as object text:

```lgl
summary g
```

`summary <target>` asks the owning domain adapter for a compact orientation
view. Its result is an ordered LGL document made from existing object statements
and `#` comments. The adapter owns the content and order; LGL does not define a
summary-specific result object or require all domains to summarize the same
concepts.

Local queries use the shared query statement list:

```lgl
query g
palette entries "Print String"
page limit 50
```

Local reads share one small mental model:

```lgl
summary <target>
<objects> ["text"]
<object> <name>
find <object>@<id>
```

Plural object operations enumerate or search, singular object operations
resolve a current local name inside the bound target, and `find` is reserved
for exact typed stable-id reads. Each domain defines which forms its UE objects
actually support, plus its result shape, `where` fields, `with` expansions,
ordering, and pagination behavior.

### Patch Text

Patch text asks a domain to mutate an object:

```lgl
patch g
print = node(palette: "P_PrintString")
add print
connect begin.then -> print.execute
```

Patch text may contain bindings, domain operations, and sugar statements. Patch
execution resolves and validates the whole patch before applying mutations.

## Palette

Palette is not a top-level text kind. It is the global creation-discovery
concept: domains expose palette queries that return stable creation entries
that can be copied into patch text.

Agents should not create new UE content by guessing display names, node classes,
or editor menu text. A domain can expose a palette query that returns copyable
object text:

```lgl
query g
palette entries "Print String"

PrintString = node(palette: "P_PrintString")
PrintString.execute = pin(type: "<FEdGraphPinType native text>", direction: in)
PrintString.InString = pin(type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "<native default text>")
PrintString.then = pin(type: "<FEdGraphPinType native text>", direction: out)
```

Patch text can then use the stable palette id directly:

```lgl
patch g
print = node(palette: "P_PrintString")
add print
```

The Graph domain uses this single Palette-backed creation path for all UE and
plugin Nodes. It does not maintain a parallel catalog of shortcut constructors.
Other domains own their creation models independently.

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
Diagnostics use a shared shape so errors become agent repair guidance rather
than free-form text.

## Domains

Domains own UE-specific meaning. A domain defines its object text, query text,
patch text, normalized JSON, diagnostics, and examples.

- Graph: graph identity, node text, pin text, edge/path text, graph queries,
  graph patches, and Palette-backed node creation.
- Asset: asset discovery, asset identity, Asset Registry metadata, and asset
  query/reference results. Asset mutation is a later asset-tools concern.
- Blueprint: Blueprint class contract, variables, functions, dispatchers,
  custom events, components, class/member/component patches, and
  component-bound event references used by graph creation.
- Class: UClass Reflection identity and hierarchy, effective Properties and
  Functions, Parameters, Metadata, source provenance, effective Class
  Defaults, and Blueprint-backed Defaults patches.
- Widget: widget tree structure, modeled widget constructors, slots, widget
  properties, widget queries, and widget tree patches.

Additional domains should follow the same shape instead of adding new global
language categories.

## Documentation Map

- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared LGL syntax and text-to-JSON
  principles.
- [`DIAGNOSTICS.md`](DIAGNOSTICS.md): shared diagnostic shape, error layers,
  and agent repair guidance.
- [`DOMAINS.md`](DOMAINS.md): domain document contract and normalization
  boundary.
- [`domains/graph.md`](domains/graph.md): graph domain design.
- [`domains/asset.md`](domains/asset.md): asset domain design.
- [`domains/blueprint.md`](domains/blueprint.md): Blueprint domain design.
- [`domains/class.md`](domains/class.md): Class domain design, including
  Reflection and Class Defaults.
- [`domains/widget.md`](domains/widget.md): widget domain design.

Some bridge implementation documents still describe earlier spike plans. Treat
them as factual UE implementation context, not as the language source of truth.
