# SAL Overview

## Intent

SAL (Structured Agent Language) is a token-efficient shared text language for
humans and agents to inspect, exchange, and modify complex non-text objects. It
expresses object structure, relationships, capabilities, and edits as compact,
ordered, copyable text while preserving the native system's semantics.

SAL has three equal design goals:

- faithfully textualize complex non-text objects without replacing their
  native model
- support direct human-agent collaboration through readable, copyable Query,
  Object, and Patch Text
- reduce total token cost across the complete discovery, read, reasoning,
  modification, and verification loop

Agents should read and write SAL text. The bridge should execute normalized
JSON. Parsers, validators, and adapters own the conversion between them.

## Mental Model

SAL separates three concerns:

- text kinds: what the agent is trying to do
- text and JSON forms: how text becomes the bridge contract
- domains: which UE semantic area owns the meaning

The top-level language model should not be a union such as `Graph | Query |
Patch | Palette`. `Graph`, `Asset`, `Blueprint`, and `Widget` are domains.
`Palette` is the global creation-discovery concept inside domains, not a
top-level SAL text kind.

## Domain Object Constructors

The Core has one generic named-call form and no predeclared constructor
vocabulary. Adapters return calls such as `graph(...)`, `node(...)`, or
`pin(...)` in ordered Object Text. Their names express small SAL structural
roles; they do not select an adapter, map a UE type, or route a domain. The
resolved object's actual UE Class and ownership chain determine its composed
capabilities.

Every `type` field remains exact native UE text. A constructor expression only
describes or binds an object and has no side effect. Palette returns every
constructor accepted by direct `add`, including its exact creation-capability
id and required parameters. In Patch text, `add` materializes that binding:

```sal
patch door
door.Health = variable(
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
)
add door.Health
```

Query results and Patch bindings use the same object-shape constructors. The
agent copies their names and argument shapes from adapter output rather than
guessing them. A constructor name never replaces a UE Class, type value, editor
label, or object role. Creation-only `palette` is omitted from materialized
object state.

## Text Kinds

SAL has three agent-facing text kinds.

### Object Text

Object text describes UE objects, object fragments, or query results:

```sal
bp = blueprint(asset: "/Game/BP_Door.BP_Door", id: "blueprint-guid")
g = graph(asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)

begin = node(graph: g, id: "A001", type: "/Script/BlueprintGraph.K2Node_Event")
print = node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")

begin.then -> print.execute
```

Object text is used for snapshots, snippets, search results, palette creation
entries, and patch results.

### Query Text

Query text asks a domain to find or expand information. Query results should be
returned as object text:

```sal
query g
summary
```

The `summary` primary operation asks the resolved target for a compact
orientation view. Its result is ordered SAL Object Text made from existing
object statements and `#` comments and may reuse request aliases. The target's
composed capabilities own the content and order; SAL does not define a
summary-specific result object or require all targets to summarize the same
concepts.

Local queries use the shared query statement list:

```sal
query g
palette entries "Print String"
page limit 50
```

Local reads share one small mental model:

```sal
query <target>
summary

query <target>
<objects> ["text"]

query <target>
<object> <name>

query <target>
<object>@<id>
```

Plural object operations enumerate or search, singular object operations
resolve a current local name inside the bound target, and a typed stable
reference is itself the exact-id operation. Each domain defines which forms its
UE objects actually support, plus its result shape, `where` fields, `with`
expansions, ordering, and pagination behavior.

### Patch Text

Patch text asks a domain to mutate an object:

```sal
patch g
print = node(palette: "P_PrintString")
add print
connect begin.then -> print.execute
```

Patch text may contain bindings, domain operations, and sugar statements. Patch
execution resolves and validates the whole patch before applying mutations.

## Palette

Palette is not a top-level text kind. It is the global creation-capability
catalog: every binding created directly by `add` starts from a Palette result
that can be copied into Patch text.

Agents should not create new UE content by guessing display names, node classes,
or editor menu text. A domain can expose a palette query that returns copyable
object text:

```sal
query g
palette entries "Print String"

PrintString = node(palette: "P_PrintString")
PrintString.execute = pin(type: "<FEdGraphPinType native text>", direction: in)
PrintString.InString = pin(type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "<native default text>")
PrintString.then = pin(type: "<FEdGraphPinType native text>", direction: out)
```

Patch text can then use the stable palette id directly:

```sal
patch g
print = node(palette: "P_PrintString")
add print
```

The same rule applies across domains. Palette may wrap a native action,
template, factory, or parameterized adapter capability, but the returned value
is always an ordinary constructor `Call` containing `palette`. Core defines no
parallel shortcut, class, or domain-constructor catalog. `add` revalidates the
entry in its current target context. Objects created as outputs or native
effects of `invoke` or another lifecycle action are not separate direct adds.

## Text And JSON Forms

SAL text has three forms:

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
- Blueprint: Class Settings, variables, functions, dispatchers,
  custom events, components, class/member/component patches, and
  component-bound event references used by graph creation.
- Class: UClass Reflection identity and hierarchy, effective Properties and
  Functions, Parameters, Metadata, source provenance, effective Class
  Defaults, and Blueprint-backed Defaults patches.
- Widget: authored Widget objects, tree views, Panel Slot and Named Slot
  relationships, native properties, queries, Palette-backed creation, and
  widget tree patches.

Additional domains should follow the same shape instead of adding new global
language categories.

## Documentation Map

- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md): shared SAL syntax and text-to-JSON
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
