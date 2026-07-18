# SAL Language Core

## Scope

The language core defines the text shape shared by all SAL domains:
statements, expressions, values, bindings, constructors, references, and
syntax sugar normalization.

It does not define graph nodes, asset registry search, widget trees, or
UE-specific patch operations. Those belong to domain documents.

## Basic Form

SAL is a line-oriented text language. A document is a sequence of statements:

```sal
# Name objects, then refer to them from later statements.
door = blueprint(asset: "/Game/BP_Door.BP_Door", id: "blueprint-guid")
g = graph(asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
print = node(graph: g, id: "node-guid", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

Short statements may stay on one line. Long statements may wrap according to
the shared delimiter rule below. Blank lines are allowed. Indentation is visual
only and does not create hierarchy.

## Statement Boundaries And Line Wrapping

Single-line and multi-line forms are the same SAL syntax. Line wrapping changes
only presentation; it must not change parsing, normalized JSON, statement
order, or execution.

A newline ends the current statement only when delimiter depth is zero. Inside
matched `(...)`, `[...]`, or `{...}`, a newline is ordinary whitespace:

```sal
print = node(graph: g, id: "node-guid", type: "/Script/BlueprintGraph.K2Node_CallFunction")
```

is exactly equivalent to:

```sal
print = node(
  graph: g,
  id: "node-guid",
  type: "/Script/BlueprintGraph.K2Node_CallFunction"
)
```

The rule applies uniformly to constructors, calls, arrays, inline objects,
condition grouping, query text, Patch text, and returned Object Text. In
particular:

- delimiters must be balanced
- indentation carries no meaning
- no continuation backslash exists
- existing comma requirements are unchanged by wrapping
- a quoted string does not become a multi-line string because its surrounding
  expression is wrapped
- single-line and multi-line comments are independent depth-zero statements,
  not content inserted inside a delimited expression
- after the final delimiter closes, the next depth-zero newline ends the
  statement

Parsers may report an unclosed delimiter at its opening span rather than
guessing where a wrapped statement was intended to end. Formatters may choose a
single-line or multi-line layout, but both must normalize identically.

## Statements

| Statement | Syntax | Example |
| --- | --- | --- |
| Single-line comment | `# text` | `# Inspect a Blueprint event graph.` |
| Multi-line comment | `###` lines around text | see below |
| Binding | `target = Expression` | `g = graph(asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)` |
| Domain statement | domain-defined line | `query g` |
| Sugar statement | domain-defined shorthand | `begin.Then -> print.Exec` |

Comments have one normalized model and two text forms. `# ` is the compact
single-line form:

```sal
# Inspect a Blueprint event graph.
```

An exact depth-zero line containing only `###` opens or closes the multi-line
form:

```sal
###
schema

fields:
  NodeComment: FString; read, write
###
```

The delimiters are not comment content. Text between them is preserved with its
line breaks and blank lines. It is opaque comment text: SAL does not interpret
indentation, Markdown, constructors, or references inside it. Multi-line
comments do not nest, and a content line containing only `###` closes the
comment. An unclosed multi-line comment is a syntax error at its opening
delimiter. A formatter uses `# text` for a one-line `Comment.text` and the
`###` form when `Comment.text` contains a newline. Consequently, normalized
multi-line `Comment.text` cannot itself contain a line that trims to `###`;
an adapter must escape such opaque native or user text, or preserve it in a
string field, before constructing the normalized result. The one-line comment
value `###` remains representable as `# ###`.

Bindings name objects or values so later statements can reference them. The
binding target is usually a local identifier:

```sal
delay = node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

Domains may also allow member paths as binding targets when they compactly
express document-local ownership. Graph pin object text uses this form:

```sal
delay.Duration = pin(id: "pin-guid", type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "1.0")
```

Domain statements are owned by a domain. The core language only requires them
to remain line-oriented and normalize through the same text-to-JSON pipeline.

Normalized JSON:

```ts
interface Binding {
  target: BindingTarget;
  value: Expr;
}

type BindingTarget =
  | LocalRef
  | BindingMemberRef;

interface BindingMemberRef {
  kind: "member";
  object: LocalRef;
  path: string[];
}
```

Local targets cover aliases such as `g` or `print`. Member targets cover
document-local paths such as `delay.Duration`, `door.Health`, or `stack.start`.
Domains define valid member targets. Every binding value normalizes through the
same `Expr` union. A constructor used for creation remains an ordinary `Call`;
Core does not give it a second creation-only expression type.

## Expressions And Values

| Expression | Syntax | Example |
| --- | --- | --- |
| Constructor | `Name(arg: value)` | `node(graph: g, type: "/Script/BlueprintGraph.K2Node_CallFunction")` |
| Reference | `name` | `g` |
| Member reference | `name.member` | `begin.Then` |
| Stable reference | `object@id` | `node@A001` |
| String | `"text"` | `"Ready"` |
| Number | number literal | `1.0` |
| Boolean | `true`, `false` | `enabled: true` |
| Null | `null` | `DefaultValue: null` |
| Symbol | unquoted word | `PrintString` |
| Array | `[value, value]` | `[320, 72]` |
| Inline object | `{key: value}` | `{TraceComplex: false}` |

`true`, `false`, and `null` are literal keywords and cannot be local aliases or
bare `Name` values.

Quoted values are strings. Unquoted words are symbols resolved by domains or
adapters. In the target normalized JSON model, symbols map to `Name`.

Normalized JSON uses one recursively composable expression model:

```ts
type Expr =
  | null
  | boolean
  | number
  | string
  | Name
  | Ref
  | Call
  | Expr[]
  | { [key: string]: Expr };

interface Name {
  kind: "name";
  name: string;
}

interface Call {
  kind: "call";
  callee: string;
  args: Record<string, Expr>;
}
```

`Name` is the normalized form for unquoted symbols. Domains and adapters
resolve what each name means. Arrays and inline objects may contain the same
references and calls that are valid at the top level. This adds no container
syntax; it only keeps ordinary values composable. For example, an adapter-owned
relationship map may contain `widget@id` values without translating them into
strings or inventing a relationship object.

## References

Local references point at previously named values or objects. Stable id
references resolve existing target state. Member references are domain-owned:

```sal
begin.Then
delay.Duration
```

In the graph domain, `begin.Then` refers to a pin on the node binding `begin`.
Other domains may define their own member meanings.

Local aliases and member paths exist only inside the current SAL document. They
make ordered object text readable, but the alias alone is not identity. A later
request may repeat a complete target binding and choose any local alias for it;
it must not carry only the earlier alias without its locator fields.

Existing objects expose native `id` only when UE provides a stable one; other
objects retain their native Path or exact scoped name. Aliases are
document-local handles. A creation binding uses an alias before the native
object exists. After creation, the adapter returns its real native locator,
including `id` when applicable. Use aliases and member paths within one SAL
document, and repeat the complete owner locator chain in later requests.

Stable references always state the object kind before `@`:

```sal
node@A001
pin@P001
graph@G001
```

`object@id` is the common cross-query spelling for concrete objects with a
native UE identifier, but it is always resolved inside the exact request
target. It is a scoped selector, not a global address. A bare `@id` is invalid.
The current stable-id token contains no whitespace or `.`; `.` separates a
following member path. Current typed stable refs map to UE Guid-like ids. If a
future domain has a native stable id that needs `.` or whitespace, its quoted
form must be designed explicitly rather than emitted as ambiguous text.
Domains map the public `id` field to native identity such as BlueprintGuid,
GraphGuid, VarGuid, VariableGuid, NodeGuid, or PinId. The object word is part of
the reference and must match the returned object kind; an adapter never guesses
it from context.

SAL does not define object-specific ref constructors such as `graph_ref`,
`variable_ref`, `component_ref`, or `pin_ref`. A domain must return unknown or
ambiguous rather than resolve an id by display name. A Blueprint Asset Path is
its load address; the Blueprint object's stable `id` maps to its persisted
`BlueprintGuid`. The Path locates the asset and the Guid verifies its identity;
the Guid is not a project-wide asset lookup key.

Normalized JSON:

```ts
type Ref =
  | LocalRef
  | MemberRef
  | StableRef;

interface LocalRef {
  kind: "local";
  name: string;
}

interface StableRef {
  kind: string;
  id: string;
}

interface MemberRef {
  kind: "member";
  object: LocalRef | StableRef;
  path: string[];
}
```

Each adapter schema defines the object words it supports. For example, Graph
may use `node`, `pin`, and `graph`. The word before `@` selects that schema's
identity namespace; it is not a native UE `type` and is not inferred from one.
`MemberRef.path` preserves every member segment after either a local alias or a
typed stable reference. The same shape therefore covers `begin.Then`,
`node@id.NodeComment`, and `menu.NamedSlots.Body` without separate field-path
types.

### Request Targets And Locator Chains

A Query or Patch target is a document-local alias whose preceding bindings
form a complete locator chain. Each binding contributes only the native address
or scoped identity required to resolve the next object:

```sal
door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

eventGraph = graph(
  asset: door,
  id: "graph-guid"
)

query eventGraph
node@node-guid
```

Normalized JSON uses one target shape:

```ts
interface Target {
  alias: string;
  value: Call | Name;
}
```

For a bound target, normalization recursively replaces local references in the
target value with their preceding bound values. The example above therefore
contains a `graph(...)` Call whose `asset` argument is the complete nested
`blueprint(...)` Call. `alias` is presentation context for compact result
references; it never participates in identity. A collection root such as
`query asset` uses `{alias: "asset", value: {kind: "name", name: "asset"}}`.
Patch requires a bound `Call`, not a collection-root `Name`.

Only the final target alias survives normalization. Intermediate locator
aliases are expanded away and therefore cannot be referenced by Query clauses
or Patch statements. Every other local reference must point to a binding or
`invoke` output declared earlier in the same ordered document.

`Target` has no public domain field and no Asset-, Blueprint-, Widget-, Class-,
or Graph-specific variant. The executor resolves the generic Call through the
active object schema and decides which arguments are locator fields. Other
arguments carried from full Object Text are not fallback identity or implicit
state assertions.

Resolution is ordered: load the Blueprint by Asset Path, verify its
BlueprintGuid, resolve the GraphGuid inside that Blueprint, then resolve the
NodeGuid inside that Graph. `node@node-guid` is exact only because
`eventGraph` already establishes its owner scope.

A locator binding is a projection of ordinary Object Text, not a second text
syntax. It may omit descriptive state such as native fields, `name`, `type`,
layout, or status when those values do not participate in resolution. Full
returned Object Text remains valid input, but non-locator fields do not become
fallback identity or implicit state assertions.

Each domain must define:

- its globally resolvable root locator, such as Asset Path or Class Path;
- the owner scope of every native id;
- which locator fields are required for Query and Patch;
- whether an exact current name is available for discovery;
- how missing, mismatched, or duplicate identity is reported.

An id-bearing object uses its typed id for later exact access. An exact-name
Query may discover such an object, but Patch must not use its current name as a
substitute for the returned id. An object without a native stable id continues
to use its UE Path or an exact name inside an already resolved owner. An object
that does not exist yet uses a Patch-local alias until mutation returns its real
id.

A scoped stable reference cannot stand alone as the request target:

```sal
query graph@graph-guid
patch graph@graph-guid
```

Both forms are invalid because they omit the owning target chain. Request
targets use a complete local binding; stable references select objects inside
that target. A domain-wide collection root such as `query asset` is the narrow
exception because it does not claim to identify one object.

## Constructors

The Core parses every `Name(named: arguments)` expression through the same
generic `Call` syntax. It neither reserves nor predeclares a constructor
vocabulary. Names such as `asset`, `graph`, `node`, `pin`, and `variable` are
data returned by adapters in Object Text, not built-in business constructs:

```sal
asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
graph(asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

A returned constructor name is an object-shape label comparable to a schema
discriminator in JSON. It may identify the object's field schema, identity
namespace, ownership shape, and text layout. It must not translate native UE
`type`, encode a UE business role, or decide an operation merely from its name.
For example, `graph(...)` identifies Graph-shaped object text; it does not mean
Function Graph, Event Graph, or Dispatcher Signature Graph.

Constructors also do not select an adapter domain. The adapter resolves the
real UE object and composes capabilities from its native Class and inheritance
chain. A `blueprint(...)` target that loads a `UWidgetBlueprint`, for example,
retains the same Blueprint-shaped locator while gaining the valid Widget
operations of that concrete UE type. SAL does not repeat that decision through
a `domain` argument or translate native Classes into constructor names.

A constructor expression has no mutation side effect. It may describe an
existing object returned by a query or bind an unmaterialized object inside a
Patch. Every constructor accepted by direct `add` is discovered through
Palette. The Palette Entry returns the complete copyable `Call`, including its
creation-capability `palette` id and any required parameters. A separate
ordered `add` performs creation:

```sal
door.Health = variable(
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
)
add door.Health
```

The agent copies the constructor name and argument shape returned by the
adapter; it does not guess them from a UE Class, type value, editor label, or
object role. The same object-shape constructor is used for materialized
readback and creation bindings, while creation-only arguments such as `palette`
are absent from materialized object state. The Palette query need not occur in
the same request as the Patch, but `add` always resolves and revalidates the
Palette Entry in the current target context before applying anything.

Creation performed by `invoke`, or native subordinate effects such as default
Graph Nodes, generated Pins, and Dispatcher Signature Graphs, is not a second
direct `add` and therefore does not require another Palette Entry.

Constructor arguments are named because they are clear for agents, easy to
validate, and safe to evolve:

```sal
g = graph(asset: door, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
```

Positional constructor arguments are not supported:

```sal
g = graph(blueprint, bp, EventGraph)
```

## UE Native Type Text

SAL never defines or translates its own type system. Every field named `type`
carries the canonical native UE text supplied by the UE system that owns that
object. It is not a friendly label, an SAL category, or a closed SAL enum.

The native source varies because UE itself uses different representations:

- Asset `type` is `FAssetData::AssetClassPath`, such as
  `"/Script/Engine.Blueprint"`.
- Blueprint `type` is native `EBlueprintType` text, such as `BPTYPE_Normal`.
- Graph `type` is native `EGraphType` text returned by its Schema, such as
  `GT_Function`, `GT_Ubergraph`, or `GT_Macro`.
- Node, Component, Widget, and similar UObject `type` fields use their exact
  native Class Path.
- Pin and Blueprint Variable `type` fields use canonical
  `FEdGraphPinType` text.
- Reflected Property `type` fields use the owning `FProperty` native text.

The owning adapter selects the native source and codec, preserves every
required detail, and validates the text through UE. Query results must return
type text that can be copied back into a compatible create or edit operation.
SAL must not replace it with friendly asset, Graph, Node, value, container,
Struct, or Enum aliases. It must not use localized editor labels as type
identity.

Native enum tokens may use ordinary unquoted Name syntax. Paths and structured
native text use strings. Normalized JSON uses those ordinary `Name` or string
values and preserves the native text without semantic remapping; there is no
type-specific AST or JSON model.

The adapter-defined object word or normalized discriminator remains structural
information such as `asset`, `graph`, `node`, or `pin`. Optional capability
hints such as an Asset result's `domains` list are descriptive discovery data,
not target-routing selectors. None of them substitute for native `type` or the
resolved UE Class.

## Arrays And Objects

Arrays use JSON-like brackets and only mean arrays:

```sal
at: [320, 0]
domains: [asset, blueprint]
```

Inline objects use JSON-like braces and only mean value objects:

```sal
options: {TraceComplex: false, DrawDebugType: ForOneFrame}
```

Braces are not structural blocks:

```sal
asset "/Game/BP_Door.BP_Door" {
  type: "/Script/Engine.Blueprint"
}
```

## Text And JSON Forms

SAL has three forms with different audiences:

| Form | Audience | Purpose |
| --- | --- | --- |
| Sugar text | Agents | Compact, readable authoring and object text |
| Canonical text | Parsers and validators | Unambiguous text after sugar expansion |
| Normalized JSON | Bridge and schemas | Explicit RPC contract for execution |

The conversion direction is one-way:

```text
sugar text -> canonical text -> normalized JSON
```

Agents read and write text. The UE bridge executes normalized JSON. Parsers,
validators, and adapters own conversion. Sugar text is optional and
domain-defined; it must normalize without UE state or schema lookup.

Canonical text keeps the SAL surface, but removes shorthand. It is still
agent-readable and domain-specific:

```sal
# Sugar text
begin.Then -> delay.Exec/Completed -> print.Exec

# Canonical text
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Patch sugar follows the same rule:

```sal
# Sugar text
connect begin.Then -> print.Exec

# Canonical text
connect(begin.Then, print.Exec)
```

Normalized JSON is the schema and RPC contract. It must be explicit enough for
the bridge to parse, resolve, validate, plan, and apply operations without
guessing from display text. Shared JSON belongs here; domain JSON belongs in
the relevant domain feature sections.

## Query Text

### Summary

`summary` is the shared orientation primary operation:

```sal
query <target>
summary
```

The owning domain adapter decides which existing SAL objects best summarize
that target. Different domains, and different target types within one domain,
may return different kinds of objects. The core does not define entry points or
any other universal summary content.

A summary result is an ordered SAL document made from existing object statements
and `#` comment statements. Adapters may use comments for counts or other
agent-facing context. Objects and comments may be interleaved, and the formatter
must preserve the adapter's order instead of regrouping statements by object
type.

Summary introduces no result constructor, section syntax, or summary-specific
object type. It is one ordinary query operation and does not accept `where`,
`with`, `order by`, or `page` clauses.

Normalized JSON uses the same query envelope as every other read:

```ts
interface SummaryOperation {
  kind: "summary";
}
```

The shared `Query.operation` union includes `SummaryOperation`. The parser
resolves the query target binding into its canonical `Target`; the
document-local alias is not target identity and need not cross the RPC
boundary.

### Query Envelope

Query text uses a shared multi-line envelope around one domain-owned primary
operation:

```sal
query <target>
<primary operation>
<allowed clauses>
```

Reads follow one small, reusable model. Each operation below occupies the line
after `query <target>`:

```sal
summary
<objects> ["text"]
<object> <name>
<object>@<id>
```

The plural object form enumerates or searches one domain-owned collection. The
singular object form resolves one exact object by its current local name inside
the bound target. A typed stable reference is itself the exact-id primary
operation; it needs no `find` prefix. Stable references always include their
object word, such as `node@id`, `pin@id`, or `graph@id`; bare `@id` is invalid.
A domain supports only the forms that match its UE objects; for example, Class
Reflection has no universal id and Graph Nodes have no reliable local name.

Domains may also define clear relationship operations such as Graph `context`,
`exec flow`, `data flow`, `palette entries`, and `palette @id`. Every query
contains exactly one primary operation. That operation explicitly defines
whether `where`, `with`, `order by`, `page`, or operation-local arguments such
as `depth` are legal.

Query text is clause-per-line. Do not combine `query`, its primary operation,
or trailing clauses on one line. Object text may prefer compact single-line
statements, but query text should keep its structure visible.

Query syntax summaries use literal words for required keywords, `<name>` for
semantic placeholders, `[item]` for optional parts, and `a|b` for choosing one
literal.

| Clause | Purpose | Example |
| --- | --- | --- |
| `query` | target domain or bound object | `query asset`, `query g` |
| primary operation | choose one domain-defined read | `assets "door"`, `context node@node-id depth 2` |
| `where` | structured filter expression | `where type = "/Script/Engine.Blueprint" and not loaded` |
| `with` | expand beyond the domain default result | `with registryTags`, `with schema, layout` |
| `order by` | deterministic result ordering | `order by score desc, path asc` |
| `page limit` | maximum result count | `page limit 50` |
| `page after` | continue after a returned cursor | `page after "cursor"` |

There is no `select` clause. Each primary operation defines its default result
shape and allowed expansions. Unsupported clauses are errors rather than
ignored generic options.

The optional quoted text after a plural collection operation is its primary
search text. `where` is for structured filters:

```sal
assets "door"
where root = "/Game" and type = "/Script/Engine.Blueprint"

nodes "Print"
where type = "/Script/BlueprintGraph.K2Node_CallFunction"
```

Field-level fuzzy conditions are allowed for advanced filters, but primary
search text belongs on the plural collection operation. Domains may define
operation-local arguments, such as graph palette pin-context arguments `from
<pin>` and `to <pin>`.

Condition expressions use a small SQL-like subset:

```sal
where type = "/Script/Engine.Blueprint"
where root = "/Game" and type = "/Script/Engine.Blueprint"
where NodeComment ~= "debug"
where not loaded
```

Supported condition operators:

| Operator | Meaning |
| --- | --- |
| `=` | equal |
| `!=` | not equal |
| `~=` | domain-defined contains or fuzzy contains |
| `>` `>=` `<` `<=` | ordered comparison |
| `not` | negation |
| `and` | conjunction |
| `or` | disjunction |
| `(...)` | grouping |

Condition precedence follows the usual SQL subset: parentheses first, then
`not`, then `and`, then `or`.

Normalized JSON uses one explicit primary-operation field. The shared envelope
does not interpret domain operations, but it guarantees that every local query
has exactly one:

```ts
interface Query {
  kind: "query";
  target: Target;
  operation: {kind: string};
  where?: Condition;
  with?: string[];
  orderBy?: OrderBy[];
  page?: Page;
}
```

The interface above shows only the shared envelope. The actual JSON Schema
replaces `operation: {kind: string}` with a closed union assembled from the
operation types defined by implemented domains, and replaces `with: string[]`
with the shared `schema` literal plus the closed set of domain details. Every
operation has a readable snake_case `kind` and only its own arguments. Plural
operations normally carry optional `text`; singular operations carry `name`;
stable-id operations carry `id`, with their `kind` preserving the typed object
word. Relationship operations define
their own fields. Summary uses `{kind: "summary"}` and carries no operation
arguments.

The old normalized `find` property represented the earlier find-centric text
model and is not part of the contract. `node@id` normalizes directly to
`{kind: "node", id: "..."}`; `variable Health` normalizes to
`{kind: "variable", name: "Health"}`. There is no internal `find_by_id`
operation.

Shared condition, ordering, and pagination value shapes remain:

```ts
type Condition =
  | { kind: "eq"; field: FieldPath; value: Expr }
  | { kind: "ne"; field: FieldPath; value: Expr }
  | { kind: "contains"; field: FieldPath; value: Expr }
  | { kind: "compare"; op: "gt" | "gte" | "lt" | "lte"; field: FieldPath; value: Expr }
  | { kind: "not"; condition: Condition }
  | { kind: "and"; conditions: Condition[] }
  | { kind: "or"; conditions: Condition[] };

interface FieldPath {
  path: string[];
}

interface OrderBy {
  key: string;
  direction: "asc" | "desc";
}

interface Page {
  limit?: number;
  after?: string;
}
```

Domain documents define their primary operations, supported `where` fields,
`with` items, ordering keys, and pagination defaults. `~=` lowers to
`contains`; domains decide its exact match behavior.

### Object Schema Expansion

`schema` is the shared object-discovery expansion:

```sal
query target
node@id
with schema
```

An exact name operation may request the same expansion:

```sal
query actorClass
property Health
with schema
```

The subject must resolve to exactly one existing object, one exact
object-backed value surface, or one exact creation entry. Existing objects use
either a typed stable reference or a domain-owned singular name operation;
object-backed values such as one Class Default reuse the exact owning object;
creation entries use the owning domain's exact palette identity. `with schema`
is not valid for summary results, collections, or ambiguous palette searches.

The ordinary object, value, or creation text remains the query result. The
adapter adds one immediately following multi-line Comment containing the
primary subject's complete usable schema:

- fields, with native UE type text, readable/writable status,
  required/default behavior, source, and constraints when known
- target-local Query operations, with their accepted clauses, expansions,
  current availability, and copyable request text when useful
- adapter-owned editing Operations, with named parameters, current
  availability, primary outputs, a copyable `invoke` template, and UE source
- direct Patch statements available when that exact object is the request
  target, with current availability, constraints, and copyable request text

There is no separate `with operations` expansion. Operations are normally
short enough that one `with schema` read should tell the agent everything it can
read, write, reset, or invoke on the exact subject. The result does not
introduce `schema(...)`, `field(...)`, `operation(...)`, or another
schema-specific object syntax.

The Comment uses a stable plain-text layout, not a nested SAL or Markdown
grammar:

```sal
###
schema

fields:
  NodeComment: FString; read, write

operations:
  AddExecutionPin()
    availability: available
    output pin: one Pin
    invoke: invoke node@sequence-id AddExecutionPin() as next
###
```

When the exact subject is also the request target, the same Comment may expose
its context-sensitive Query surface:

```sal
###
schema

query:
  exec flow from|to node@id|pin@id [depth N]
    availability: available
    with: layout
###
```

`query:` lists primary operations accepted when the exact subject is the
request target. `operations:` is reserved for object interfaces called through
`invoke`. `patch:` lists direct statements accepted when the exact subject is
the Patch target. `copy:` contains complete request text the agent may reuse.
These are stable sections of opaque Comment text, not nested SAL grammar or
additional schema objects. A Query or direct Patch statement that is
temporarily unavailable remains in its section with the UE-derived reason.

An Operation name is a stable PascalCase name owned by the adapter and grounded
in UE editor semantics. Prefer an exact non-localized UE Editor Action identity;
otherwise use the closest native interface behavior. Do not expose arbitrary
C++ methods. Adapters maintain the UE Action and native execution-path mapping.
The runtime schema includes optional `UE action:` or `native:` provenance only
when it disambiguates a non-obvious mapping or explains an availability or
constraint decision.

An Operation name is resolved inside the exact target object's schema, not in a
global Operation namespace. Reusing a UE Action or native interface name across
object types does not imply identical parameters, outputs, or effects. The
agent must use the contract and copyable template returned for that target by
`with schema`; it must not infer one target's contract from another target that
happens to expose the same name.

Schema lists every Operation supported by the subject type. Instance reads mark
current availability and give the UE reason when an Operation is unavailable.
Outputs describe only ordinary objects the agent may need to reference later:
zero outputs are omitted, fixed outputs use named roles, and variable outputs
use an ordered keyed role such as `subpins.X`. A primary output may be newly
created or may be an existing object that the Operation makes relevant or usable
for subsequent statements. Mirrored objects, reconstructed call sites, removed
Edges, and other cascades are effects reported by preflight and mutation results
rather than primary outputs.

Schema applies only to the primary subject. It does not recursively add schemas
for expanded children such as Pins. Child objects require their own exact
query. An adapter that cannot provide the requested schema returns a capability
diagnostic rather than silently omitting it.

For a creation entry, `with schema` describes accepted creation fields,
constraints, and Operations determinable for the initial created state in the
current domain context. A domain may expose an identity for the creation entry
itself; that identity is not a future object id. The query does not invent Node,
Pin, or other instance ids before UE creates them. After creation, the mutation
result returns the real objects and ids, whose instance schema may differ and
may be queried separately.

Pagination is cursor-based. If `page limit` is omitted, domains normally use
50. If `page after` is omitted, the query returns the first page. Results with
more data return an opaque cursor that agents pass back unchanged:

```sal
query g
palette entries "Print String"
page limit 50

query g
palette entries "Print String"
page limit 50
page after "cursor-from-previous-result"
```

SAL text should not expose offset pagination. Bridge adapters may internally
map cursors to offsets or other backend-specific pagination state.

## Patch Text

Patch text is a statement list:

```sal
patch target
binding = constructor(arg: value)
operation ...
```

Core reserves seven common Patch operations whose intent remains stable across
domains:

| Operation | Common intent |
| --- | --- |
| `add` | Materialize one declared binding as a domain-owned lifecycle object |
| `remove` | Delete one domain-owned lifecycle object |
| `set` | Write one schema-approved field |
| `reset` | Restore one field through its schema-approved native reset behavior |
| `move` | Change one object's layout, authored collection order, or domain-owned structural placement |
| `invoke` | Execute one target-local Operation discovered through `with schema` |
| `save` | Persist the Patch target's owning UE Package through the native save path |

The common operation name does not bypass the domain adapter. Each domain
defines the exact supported object kinds, operand forms, constraints, native UE
path, cascades, and result. `move`, for example, may support `to` and `by` for
Graph layout while an authored collection supports `before` and `after`.

Domains may additionally define operations that depend on their own model.
Graph owns `connect`, `disconnect`, `break`, and `insert` because their meanings
require Pins, Edges, Nodes, and a Graph Schema. Those are not Core lifecycle
operations merely because Graph was the first Patch domain. Widget likewise
owns `wrap` and `replace` because preserving Panel Slots, Named Slots, Root
placement, Widget identity, and references requires the Widget ownership model.

A domain must not expose two equivalent ways to perform the same mutation. If
an object is a declared lifecycle object, ordinary creation and deletion use
`add` and `remove`, not target-local `Add...`, `Delete`, or `Remove` Operations.
`invoke` remains appropriate for specialized subordinate behavior that is not
valid for the object kind in general, such as removing one editable signature
Parameter Pin through `RemoveParameter()`; this does not make arbitrary Pins
valid `remove` targets.

An operation exists only where its domain defines an exact meaning. For
example, Class Defaults use `reset` to remove a local default override and
resume inheritance. Bindings make later operations precise:

```sal
patch g
print = node(palette: "P_PrintString")
add print
connect pin@begin-then-id -> print.execute
```

Each binding occupies one complete statement. It declares an unmaterialized local or
member-path alias; the owning domain defines which `add` form materializes it.
Every binding passed directly to `add` must contain the `palette` identity of an
exact creation capability returned by Palette. Missing, stale, or context-
invalid Palette identities fail validation. Core still treats the binding value
as an ordinary `Call`; the adapter owns capability resolution and argument
validation.

A domain operation may explicitly consume and materialize an unmaterialized
Palette-backed binding when creation and a native structural transformation are
one indivisible operation. Graph `insert` materializes its Node binding; Widget
`wrap` materializes its wrapper binding, and Widget `replace` may materialize a
replacement binding. Such a binding is not also passed to `add`, and the domain
operation must document its exact identity, placement, and failure semantics.
This is a domain operation, not Core creation sugar.

Inline binding forms such as `add print = node(...)` are invalid. A domain may
define operation-local sugar, but it must lower to ordinary ordered statements
before validation. For example, Graph
`add print pin@source-id -> print.execute` lowers to `add print` followed by
`connect(pin@source-id, print.execute)`.

Object text describes state. Patch text always requires an explicit operation:
a bare Graph Edge is object text and cannot mean `connect` merely because it
appears inside a Patch.

`save` is the shared terminal Patch operation:

```sal
patch target
save
```

The target may be an Asset, Blueprint, Graph, Widget, or another domain object
whose real UE ownership resolves to persistent Package state. Core does not
require the caller to retarget the request to a separate Asset binding merely
because UE ultimately serializes a Package. A transient target or a target
without resolvable persistent ownership does not support `save`.

`save` persists only dirty Package state. A clean Package returns a successful
`already clean` result without rewriting it. Saving follows UE's non-interactive,
source-control-aware path: enabled Source Control may check out an existing
file or mark a new file for add, and checkout, read-only, or disk failures are
returned as diagnostics. Native external Package ownership, such as a World's
external packages, remains UE-defined. Core does not add `save all`, interactive
prompts, checkout flags, or a second Asset-only save syntax.

`save` may appear at most once and must be the last statement in its Patch. It
may be the only statement, or it may follow a terminal statement whose domain
explicitly permits that sequence. It cannot be mixed with bindings, authored
source mutation, or arbitrary `invoke` statements. The Blueprint domain, for
example, permits `compile` followed by `save`; `save` followed by `compile`,
repeated terminal statements, and undeclared terminal combinations are invalid.

`save` normalizes directly to `{kind: "save"}`. Blueprint `compile` follows the
same terminal-statement rule and normalizes to `{kind: "compile"}`.

`invoke` is the shared Patch operation for adapter-owned object interfaces:

```sal
invoke <target> <Operation>(named arguments) [as <alias>]
invoke <target> <Operation>(named arguments) as <selector>: <alias>, ...
```

For example:

```sal
invoke node@sequence-id AddExecutionPin() as next
invoke pin@vector-id SplitStructPin() as subpins.X: x, subpins.Z: z
```

The target is one typed stable reference or one already materialized local
alias. `Operation` is copied exactly from the target's `with schema` result.
Arguments use the existing named-argument and line-wrapping rules; when the
argument list wraps, the closing `)` and optional `as` clause remain in the same
statement. `invoke` is a statement, never an expression nested inside another
call or binding. Operation lookup is target-local; the same Operation name may
have a different schema on a different target type.

The optional `as` clause binds primary outputs for later statements in the same
Patch. When the Operation has exactly one primary output, `as <alias>` binds it
directly; its object kind comes from the exact target's schema and is not
repeated in text. When an Operation has multiple primary outputs, each binding
uses an exact adapter-provided selector such as `key` or a keyed-many selector
such as `subpins.X`. SAL defines no universal `members` or `items` role. The
caller may omit outputs it does not need from a multi-output Operation. Every
alias is unique and becomes valid only after the `invoke` statement succeeds.
It is then an ordinary object reference equivalent in operation position to a
stable typed reference.

Normalized JSON:

```ts
interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  statements: PatchStatement[];
}

type PatchStatement = Binding | PatchOperation;

interface Invoke {
  kind: "invoke";
  target: Ref;
  operation: string;
  args: Record<string, Expr>;
  outputs: InvokeOutputBinding[];
}

interface InvokeOutputBinding {
  selector?: string;
  alias: string;
}
```

An omitted JSON `selector` is valid only when the resolved Operation schema has
exactly one primary output. Pure normalization preserves that omission; the
owning adapter resolves and validates it against the same schema used by
`with schema`.

The single `statements` array preserves exact source order. Bindings appear
directly as `{target, value}` and are not wrapped in a second `binding`
statement. Operations must not be regrouped into parallel arrays because
binding lifetime, creation, member resolution, and execution all depend on
that order.

`PatchOperation` is the closed schema union of Core and domain-specific
operation payloads; `Invoke` is the shared operation shape above. Direct fields
mirror SAL keywords: `connect` and `disconnect` carry `from` and `to`; `move`
carries exactly one of `to`, `by`, `before`, or `after`; `wrap` and `replace`
carry `with`; `save` and `compile` carry no arguments. Core normalization
preserves output-binding order but does not decide whether the target supports
the Operation, whether arguments are valid, or which UE API executes it. The
owning adapter validates all of those against the same schema it returns to the
agent.

Stable object references are typed in text and JSON: for example, `node@id` normalizes to
`{kind: "node", id}`, while a document alias remains a `LocalRef` and an alias
member remains a `MemberRef`. A bare stable `@id` does not exist.

Dry run is a mutation mode, not a separate language: parse, resolve, validate,
and plan through the real path, then stop before applying changes. Authored
source Patches are ordered and atomic; a failed validation applies nothing,
and an apply failure must restore the entire Patch rather than return partial
success.

Terminal execution is ordered but not rollbackable. A terminal Patch validates
its complete sequence before executing the first statement, but a later
external failure cannot undo an earlier compile or disk write. The result must
report every completed step honestly. If a Blueprint compiles and its following
`save` then fails, the mutation returns `isError: true` and `applied: true`, the
ordinary Blueprint object contains the resulting compile state, and the save
diagnostic explains the failure. It must not claim that compilation rolled back.

## Creation Discovery

Palette is the universal discovery path for every object created directly by a
binding followed by `add`. Each domain exposes Palette in the target context
through the existing plural search and exact-id operations:

```sal
query door
palette entries "Variable"

query door
palette @P_BlueprintVariable
with schema
```

The same discovered constructor Call is used when a documented domain
operation materializes a binding directly, such as Graph `insert` or Widget
`wrap` and Palette-backed `replace`.

A Palette result is ordinary ordered Object Text. Each entry contains a binding
whose value is the complete constructor `Call` the agent may copy into Patch
text. The adapter chooses the constructor name, the `palette` identity, and the
argument shape:

```sal
Variable = variable(
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
)

PrintString = node(palette: "P_PrintString")
```

`with schema` adds the accepted fields, native type text, constraints, and
initially available Operations through the shared structured comment contract.
Domain-owned child previews such as future Graph Pins may follow the creation
binding in the same ordered Object Text. They are descriptive output, not more
objects the Patch must create directly.

There is no closed `ShortcutEntry | ClassEntry | PaletteEntry` union and no
separate grouped `PaletteResult`. A Palette creation binding normalizes exactly
like any other binding, with an ordinary `Call` value:

```json
{
  "target": {"kind": "local", "name": "Variable"},
  "value": {
    "kind": "call",
    "callee": "variable",
    "args": {
      "palette": "P_BlueprintVariable",
      "type": "<FEdGraphPinType native text>"
    }
  }
}
```

The `palette` value identifies a creation capability in one adapter context. It
is not the stable id of the future UE object and is not persisted as materialized
object state. At `add`, the adapter resolves it again, validates the current
context and all arguments against the same entry schema, then returns the real
created object and native id through the domain's ordinary ordered Object Text.

## Results And Diagnostics

Bridge-facing results use normalized JSON plus diagnostics. SDK results format
successful objects back to SAL text.

Normalized JSON:

```ts
interface Result {
  object?: ObjectText;
  diagnostics: Diagnostic[];
  page?: {
    next?: string;
  };
}

interface MutationResult extends Result {
  isError: boolean;
  dryRun: boolean;
  valid: boolean;
  applied: boolean;
  assetPath?: string;
  operation: string;
  resolvedRefs?: unknown;
  planned?: unknown;
  diff?: unknown;
  previousRevision?: string;
  newRevision?: string;
}

interface Comment {
  kind: "comment";
  text: string;
}

interface ObjectText {
  statements: Statement[];
}

type Statement = Binding | Edge | Comment;

interface Edge {
  from: Ref;
  to: Ref;
}

interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  span?: SourceSpan;
  suggestion?: string;
}

interface SourceSpan {
  line: number;
  column: number;
  length?: number;
}
```

Queries and mutations use the same `object: ObjectText` content model and the
same SAL formatter. `MutationResult` only adds execution state around that
ordinary object; it does not introduce a second mutation-specific object or
text format. Optional revision fields remain absent from a concrete tool's
public response until that tool enforces them.

A result returns the state requested by its primary operation. It must not
repeat a complete target snapshot merely to identify the request again, and it
must not inherit local aliases from the request. When returned statements refer
to the target or another owner, the result first declares a compact binding for
that object using only the typed id, native Path, or other state needed by the
returned relationships. This compact result binding is not a substitute for a
complete request locator. When a result navigates to a different target, it
must provide enough owning locator information to construct a later
self-contained request; a scoped id alone is insufficient.

For an ordered terminal Patch, `applied` means that at least one terminal step
actually executed. `isError` reports whether the requested sequence completed
successfully. The combination `applied: true, isError: true` is therefore valid
when a later non-rollbackable step fails after an earlier one completed; the
ordered object comments and structured diagnostics identify the exact boundary.

`Comment.text` stores the content without text delimiters. A one-line value
formats as `# text`; a value containing a newline formats between depth-zero
`###` delimiter lines. The delimiters and a block's final line ending are not
part of `text`. A normalized multi-line value cannot contain its own standalone
delimiter line; result builders escape that opaque input before formatting.
Comments are data in the ordered object, not an auxiliary comments array.
Schema is one use of an ordinary multi-line Comment, not a new result statement
type.

Every result serializes its Object Text in one `statements` array. Bindings,
Edges, and Comments are interleaved in exact formatter order. It must not use
domain result wrappers such as `GraphResult`, `ClassResult`, `AssetResult`, or
`PaletteResult`, or parallel arrays such as `nodes`, `pins`, `properties`,
`defaults`, and `comments`. Exact Call fields and operation capabilities remain
executor-owned; the result container does not repeat the domain distinction.

The ordered result container is normalized JSON only. It does not add
`document(...)`, `result(...)`, section syntax, or any other SAL text form.
Blank lines are canonical formatting and are not result statements.

`page.next` is opaque. Agents should pass it back through `page after` without
parsing it. Diagnostics should be teachable: they should name the failed
construct and, when possible, point to the query, patch, or object text that
should be changed.

## Core Rules

1. SAL text is a statement list.
2. A depth-zero newline ends a statement; newlines inside matched `()`, `[]`,
   or `{}` are presentation-only whitespace.
3. Structure comes from bindings, references, constructors, arrays, inline
   objects, and domain statements.
4. `{}` is only an inline value object, never a structural block.
5. `[]` is only an array.
6. Constructors use named arguments.
7. Positional constructor arguments are not part of SAL.
8. Query text and patch text use statement lists, not JSON-like bodies.
9. Sugar text must have a canonical text form.
10. Canonical text must lower to normalized JSON.
11. The language core does not decide whether a symbol is a class, enum, asset,
   field, node type, or operation. Domains and adapters resolve those meanings.
12. Comments normalize to ordered `Comment` objects. `# text` is the one-line
    form and depth-zero `###` delimiter lines enclose the multi-line form.
13. `invoke` is the shared Patch operation for schema-discovered object
    Operations; domains and adapters own the available Operations and outputs.
14. `save` is the shared terminal Patch operation for a target with persistent
    owning Package state; it is final, non-interactive, and never means save all.
15. A Query or Patch target alias expands to a complete domain-defined locator
    chain; a scoped `object@id` cannot replace that chain.
16. Exact names discover existing id-bearing objects, typed ids access and
    modify them, native Paths or scoped names identify objects that have no
    native id, and Patch-local aliases identify objects that do not exist yet.
17. Constructors describe SAL object shape. Native UE Class and inheritance
    determine adapter capabilities and are never translated into constructor
    names or repeated through a public `domain` field.
