# LGL Language Core

## Scope

The language core defines the text shape shared by all LGL domains:
statements, expressions, values, bindings, constructors, references, and
syntax sugar normalization.

It does not define graph nodes, asset registry search, widget trees, or
UE-specific patch operations. Those belong to domain documents.

## Basic Form

LGL is a line-oriented text language. A document is a sequence of statements:

```lgl
# Name objects, then refer to them from later statements.
bp = asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
print = node(graph: g, id: "node-guid", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

Short statements may stay on one line. Long statements may wrap according to
the shared delimiter rule below. Blank lines are allowed. Indentation is visual
only and does not create hierarchy.

## Statement Boundaries And Line Wrapping

Single-line and multi-line forms are the same LGL syntax. Line wrapping changes
only presentation; it must not change parsing, normalized JSON, statement
order, or execution.

A newline ends the current statement only when delimiter depth is zero. Inside
matched `(...)`, `[...]`, or `{...}`, a newline is ordinary whitespace:

```lgl
print = node(graph: g, id: "node-guid", type: "/Script/BlueprintGraph.K2Node_CallFunction")
```

is exactly equivalent to:

```lgl
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
| Binding | `target = Expression` | `g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)` |
| Domain statement | domain-defined line | `query g` |
| Sugar statement | domain-defined shorthand | `begin.Then -> print.Exec` |

Comments have one normalized model and two text forms. `# ` is the compact
single-line form:

```lgl
# Inspect a Blueprint event graph.
```

An exact depth-zero line containing only `###` opens or closes the multi-line
form:

```lgl
###
schema

fields:
  NodeComment: FString; read, write
###
```

The delimiters are not comment content. Text between them is preserved with its
line breaks and blank lines. It is opaque comment text: LGL does not interpret
indentation, Markdown, constructors, or references inside it. Multi-line
comments do not nest, and a content line containing only `###` closes the
comment. An unclosed multi-line comment is a syntax error at its opening
delimiter. A formatter uses `# text` for a one-line `Comment.text` and the
`###` form when `Comment.text` contains a newline.

Bindings name objects or values so later statements can reference them. The
binding target is usually a local identifier:

```lgl
delay = node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

Domains may also allow member paths as binding targets when they compactly
express document-local ownership. Graph pin object text uses this form:

```lgl
delay.Duration = pin(id: "pin-guid", type: "<FEdGraphPinType native text>", direction: in, DefaultValue: "1.0")
```

Domain statements are owned by a domain. The core language only requires them
to remain line-oriented and normalize through the same text-to-JSON pipeline.

Normalized JSON:

```ts
interface Binding {
  target: BindingTarget;
  value: BindingValue;
}

type BindingTarget =
  | { kind: "local"; name: string }
  | { kind: "member"; object: string; member: string };

type BindingValue =
  | Expr
  | NodeCreation;
```

Local targets cover aliases such as `g` or `print`. Member targets cover
document-local paths such as `delay.Duration`, `door.Health`, or `stack.start`.
Domains define valid member targets. Most bindings normalize to `Expr`; graph
patch creation bindings may normalize to `NodeCreation`.

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

Quoted values are strings. Unquoted words are symbols resolved by domains or
adapters. In the target normalized JSON model, symbols map to `Name`.

Normalized JSON:

```ts
type Expr =
  | Value
  | Ref
  | Call;

type Value =
  | null
  | boolean
  | number
  | string
  | Name
  | Value[]
  | { [key: string]: Value };

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
resolve what each name means.

## References

Local references point at previously named values or objects. Stable id
references resolve existing target state. Member references are domain-owned:

```lgl
begin.Then
delay.Duration
```

In the graph domain, `begin.Then` refers to a pin on the node binding `begin`.
Other domains may define their own member meanings.

Local aliases and member paths exist only inside the current LGL document. They
make ordered object text readable, but they are not stable identities and must
not be carried into a later query or mutation.

Existing objects expose their native `id`; aliases are document-local handles.
A creation binding uses an alias before the native object exists. After
creation, the adapter returns the created object with its actual `id`. Use
aliases and member paths within one LGL document, and use a typed stable
reference across queries or later operations.

Stable references always state the object kind before `@`:

```lgl
node@A001
pin@P001
graph@G001
```

`object@id` is the common cross-query reference for concrete objects with a
native UE identifier. A bare `@id` is invalid. Domains map the public `id` field
to native identity such as BlueprintGuid, GraphGuid, VarGuid, VariableGuid,
TimelineGuid, NodeGuid, or PinId. The object word is part of the reference and
must match the returned object kind; an adapter never guesses it from context.

LGL does not define object-specific ref constructors such as `graph_ref`,
`variable_ref`, `component_ref`, or `pin_ref`. A domain must return unknown or
ambiguous rather than resolve an id by display name. A Blueprint Asset Path is
its current load location; the Blueprint object's stable `id` maps to its
persisted `BlueprintGuid`.

Normalized JSON:

```ts
type Ref =
  | { kind: "local"; name: string }
  | { kind: "member"; object: string; member: string }
  | StableRef;

interface StableRef {
  kind: string;
  id: string;
}
```

Each adapter schema defines the object words it supports. For example, Graph
may use `node`, `pin`, and `graph`. The word before `@` selects that schema's
identity namespace; it is not a native UE `type` and is not inferred from one.
Member references remain two-segment, document-local paths in normalized JSON;
a domain operation may then select one native field from the referenced object.

## Constructors

The Core parses every `Name(named: arguments)` expression through the same
generic Call syntax. It does not reserve `asset`, `graph`, `node`, `pin`,
`variable`, or other domain object names as built-in business constructs.

Each domain adapter may define a small set of object constructors that make its
ordered text explicit and compact:

```lgl
asset(path: "/Game/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

A domain constructor is an object-shape label comparable to a schema
discriminator in JSON. It may identify the object's field schema, identity
namespace, ownership shape, and text layout. It must not translate native UE
`type`, encode a UE business role, or decide an operation merely from its name.
For example, `graph(...)` identifies Graph-shaped object text; it does not mean
Function Graph, Event Graph, or Dispatcher Signature Graph.

A constructor expression has no mutation side effect. It may describe an
existing object returned by a query or bind an unmaterialized object inside a
Patch. A separate ordered Operation such as `add` performs creation when the
resolved adapter schema permits it:

```lgl
door.Health = variable(type: "<FEdGraphPinType native text>")
add door.Health
```

The same domain constructor is used for readback and mutation bindings. A
domain should define one only when it materially clarifies an independently
addressable object, its identity space, or its structural relationships. It
must not add one constructor per native Class, type value, editor label, or
object role. Every public constructor remains part of the documented domain
contract; adapters cannot introduce one silently.

Constructor arguments are named because they are clear for agents, easy to
validate, and safe to evolve:

```lgl
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
```

Positional constructor arguments are not supported:

```lgl
g = graph(blueprint, bp, EventGraph)
```

## UE Native Type Text

LGL never defines or translates its own type system. Every field named `type`
carries the canonical native UE text supplied by the UE system that owns that
object. It is not a friendly label, an LGL category, or a closed LGL enum.

The native source varies because UE itself uses different representations:

- Asset `type` is `FAssetData::AssetClassPath`, such as
  `"/Script/Engine.Blueprint"`.
- Blueprint `type` is native `EBlueprintType` text, such as `BPTYPE_Normal`.
- Graph `type` is native `EGraphType` text returned by its Schema, such as
  `GT_Function`, `GT_Ubergraph`, or `GT_Macro`.
- Node, Component, Timeline, and similar UObject `type` fields use their exact
  native Class Path.
- Pin and Blueprint Variable `type` fields use canonical
  `FEdGraphPinType` text.
- Reflected Property `type` fields use the owning `FProperty` native text.

The owning adapter selects the native source and codec, preserves every
required detail, and validates the text through UE. Query results must return
type text that can be copied back into a compatible create or edit operation.
LGL must not replace it with friendly asset, Graph, Node, value, container,
Struct, or Enum aliases. It must not use localized editor labels as type
identity.

Native enum tokens may use ordinary unquoted Name syntax. Paths and structured
native text use strings. Normalized JSON uses those ordinary `Name` or string
values and preserves the native text without semantic remapping; there is no
type-specific AST or JSON model.

The adapter-defined object word or normalized discriminator remains structural
information such as `asset`, `graph`, `node`, or `pin`. `domain` and `domains`
remain adapter routing information. None of them substitute for native `type`.

## Arrays And Objects

Arrays use JSON-like brackets and only mean arrays:

```lgl
at: [320, 0]
domains: [asset, blueprint]
```

Inline objects use JSON-like braces and only mean value objects:

```lgl
options: {TraceComplex: false, DrawDebugType: ForOneFrame}
```

Braces are not structural blocks:

```lgl
asset "/Game/BP_Door.BP_Door" {
  type: "/Script/Engine.Blueprint"
}
```

## Text And JSON Forms

LGL has three forms with different audiences:

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

Canonical text keeps the LGL surface, but removes shorthand. It is still
agent-readable and domain-specific:

```lgl
# Sugar text
begin.Then -> delay.Exec/Completed -> print.Exec

# Canonical text
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Patch sugar follows the same rule:

```lgl
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

`summary` asks the adapter that owns a target for a compact orientation view:

```lgl
summary <target>
```

The language core defines only this request shape. The owning domain adapter
decides which existing LGL objects best summarize that target. Different
domains, and different target types within one domain, may return different
kinds of objects. The core does not define entry points or any other universal
summary content.

A summary result is an ordered LGL document made from existing object statements
and `#` comment statements. Adapters may use comments for counts or other
agent-facing context. Objects and comments may be interleaved, and the formatter
must preserve the adapter's order instead of regrouping statements by object
type.

Summary introduces no result constructor, section syntax, or summary-specific
object type. `summary <target>` is a standalone statement and does not accept
`find`, `where`, `with`, `order by`, or `page` clauses.

Normalized JSON preserves that standalone meaning:

```ts
interface Summary {
  kind: "summary";
  target: Target;
}
```

The parser resolves the required target binding into its canonical `Target`.
The document-local alias is not target identity and need not cross the RPC
boundary.

### Local Queries

Query text uses a shared multi-line envelope around one domain-owned primary
operation:

```lgl
query <target>
<primary operation>
<allowed clauses>
```

Local reads follow one small, reusable model:

```lgl
summary <target>
<objects> ["text"]
<object> <name>
find <object>@<id>
```

The plural object form enumerates or searches one domain-owned collection. The
singular object form resolves one exact object by its current local name inside
the bound target. `find` has one meaning only: resolve one existing object by a
typed stable id. Stable references always include their object word, such as
`node@id`, `pin@id`, or `graph@id`; bare `@id` is invalid. A domain supports
only the forms that match its UE objects; for example, Class Reflection has no
universal id and Graph Nodes have no reliable local name.

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
| `with` | expand beyond the domain default result | `with registryTags`, `with pins, defaults, layout` |
| `order by` | deterministic result ordering | `order by score desc, path asc` |
| `page limit` | maximum result count | `page limit 50` |
| `page after` | continue after a returned cursor | `page after "cursor"` |

There is no `select` clause. Each primary operation defines its default result
shape and allowed expansions. Unsupported clauses are errors rather than
ignored generic options.

The optional quoted text after a plural collection operation is its primary
search text. `where` is for structured filters:

```lgl
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

```lgl
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
stable-id operations carry a typed reference. Relationship operations define
their own fields.

The old normalized `find` property represented the earlier find-centric text
model and is not part of the target contract. `find node@id` becomes an
ordinary operation such as
`{kind: "find_by_id", target: {kind: "node", id: "..."}}`; other primary
operations must not be forced through a field named `find`.

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

The current TypeScript schema, parser, and adapters still use the old `find`
field. They must migrate to `operation` domain by domain; target documentation
must not preserve the old shape merely for implementation compatibility.

### Object Schema Expansion

`schema` is the shared object-discovery expansion:

```lgl
query target
find node@id
with schema
```

An exact name operation may request the same expansion:

```lgl
query actorClass
property Health
with schema
```

The subject must resolve to exactly one existing object or one exact creation
entry. Existing objects use either a typed stable reference or a domain-owned
singular name operation; creation entries use the owning domain's exact palette
identity. `with schema` is not valid for summary results, collections, or
ambiguous palette searches.

The ordinary object or creation text remains the query result. The adapter adds
one immediately following multi-line Comment containing the primary subject's
complete usable schema:

- fields, with native UE type text, readable/writable status,
  required/default behavior, source, and constraints when known
- adapter-owned editing Operations, with named parameters, current
  availability, primary outputs, a copyable `invoke` template, and UE source

There is no separate `with operations` expansion. Operations are normally
short enough that one `with schema` read should tell the agent everything it can
read, write, reset, or invoke on the exact subject. The result does not
introduce `schema(...)`, `field(...)`, `operation(...)`, or another
schema-specific object syntax.

The Comment uses a stable plain-text layout, not a nested LGL or Markdown
grammar:

```lgl
###
schema

fields:
  NodeComment: FString; read, write

operations:
  AddExecutionPin()
    availability: available
    output pin: one Pin
    invoke: invoke node@sequence-id AddExecutionPin() as pin: next
    UE action: FGraphEditorCommands::AddExecutionPin
    native: UK2Node_ExecutionSequence::AddInputPin
###
```

An Operation name is a stable PascalCase name owned by the adapter and grounded
in UE editor semantics. Prefer an exact non-localized UE Editor Action identity;
otherwise use the closest native interface behavior. Do not expose arbitrary
C++ methods. The schema records the UE Action and native execution path so the
public semantic name remains traceable even when a Node's implementation method
has a misleading name or later changes.

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

The current TypeScript schema, parser, and adapters do not implement the
`schema` expansion yet.

Pagination is cursor-based. If `page limit` is omitted, domains normally use
50. If `page after` is omitted, the query returns the first page. Results with
more data return an opaque cursor that agents pass back unchanged:

```lgl
query g
palette entries "Print String"
page limit 50

query g
palette entries "Print String"
page limit 50
page after "cursor-from-previous-result"
```

LGL text should not expose offset pagination. Bridge adapters may internally
map cursors to offsets or other backend-specific pagination state.

## Patch Text

Patch text is a statement list:

```lgl
patch target
binding = constructor(arg: value)
operation ...
```

Core reserves six common Patch operations whose intent remains stable across
domains:

| Operation | Common intent |
| --- | --- |
| `add` | Materialize one declared binding as a domain-owned lifecycle object |
| `remove` | Delete one domain-owned lifecycle object |
| `set` | Write one schema-approved field |
| `reset` | Restore one field through its schema-approved native reset behavior |
| `move` | Change one object's layout position or authored collection order |
| `invoke` | Execute one target-local Operation discovered through `with schema` |

The common operation name does not bypass the domain adapter. Each domain
defines the exact supported object kinds, operand forms, constraints, native UE
path, cascades, and result. `move`, for example, may support `to` and `by` for
Graph layout while an authored collection supports `before` and `after`.

Domains may additionally define operations that depend on their own model.
Graph owns `connect`, `disconnect`, `break`, and `insert` because their meanings
require Pins, Edges, Nodes, and a Graph Schema. Those are not Core lifecycle
operations merely because Graph was the first Patch domain.

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

```lgl
patch g
print = node(palette: "P_PrintString")
add print
connect pin@begin-then-id -> print.execute
```

Each binding occupies one complete line. It declares an unmaterialized local or
member-path alias; the owning domain defines which `add` form materializes it.
Inline binding forms such as `add print = node(...)` are invalid. A domain may
define operation-local sugar, but it must lower to ordinary ordered statements
before validation. For example, Graph
`add print pin@source-id -> print.execute` lowers to `add print` followed by
`connect(pin@source-id, print.execute)`.

Object text describes state. Patch text always requires an explicit operation:
a bare Graph Edge is object text and cannot mean `connect` merely because it
appears inside a Patch.

`invoke` is the shared Patch operation for adapter-owned object interfaces:

```lgl
invoke <target> <Operation>(named arguments) [as <selector>: <alias>, ...]
```

For example:

```lgl
invoke node@sequence-id AddExecutionPin() as pin: next
invoke pin@vector-id SplitStructPin() as subpins.X: x, subpins.Z: z
```

The target is one typed stable reference or one already materialized local
alias. `Operation` is copied exactly from the target's `with schema` result.
Arguments use the existing named-argument and line-wrapping rules; when the
argument list wraps, the closing `)` and optional `as` clause remain in the same
statement. `invoke` is a statement, never an expression nested inside another
call or binding. Operation lookup is target-local; the same Operation name may
have a different schema on a different target type.

The optional `as` clause binds selected primary outputs for later statements in
the same Patch. A selector is an exact adapter-provided role such as `pin` or a
keyed-many selector such as `subpins.X`; LGL defines no universal `members` or
`items` role. Fixed multi-output Operations expose multiple roles, for example
`key` and `value`. The caller may omit outputs it does not need. Every alias is
unique and becomes valid only after the `invoke` statement succeeds. It is then
an ordinary object reference equivalent in operation position to a stable typed
reference.

Normalized JSON:

```ts
interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  statements: PatchStatement[];
}

type PatchStatement = BindingStatement | Invoke | PatchOp;

interface BindingStatement {
  kind: "binding";
  target: BindingTarget;
  value: BindingValue;
}

interface Invoke {
  kind: "invoke";
  target: Ref;
  operation: string;
  args: Record<string, Expr>;
  outputs: InvokeOutputBinding[];
}

interface InvokeOutputBinding {
  selector: string;
  alias: string;
}
```

The single `statements` array preserves exact source order. Bindings and
operations must not be regrouped into parallel arrays because binding lifetime,
creation, member resolution, and execution all depend on that order. The
`binding` value is a normalized JSON discriminator, not an LGL keyword.

`PatchOp` is the closed schema union of domain-specific operation payloads;
`Invoke` is the shared operation shape above. Core normalization preserves
output-binding order but does not decide whether the target supports the
Operation, whether arguments are valid, or which UE API executes it. The owning
adapter validates all of those against the same schema it returns to the agent.

Stable object references are typed in text and JSON: for example, `node@id` normalizes to
`{kind: "node", id}`, while a document alias remains a `LocalRef` and an alias
member remains a `MemberRef`. A bare stable `@id` does not exist.

Dry run is a mutation mode, not a separate language: parse, resolve, validate,
and plan through the real path, then stop before applying changes. Every Patch
is ordered and atomic; a failed validation applies nothing, and an apply failure
must restore the entire Patch rather than return partial success.

## Creation Results

Creation discovery is a query result pattern for domains that need stable ways
to create new objects, such as graph nodes or widget tree entries.

Agent-facing query text uses domain creation-discovery operations such as:

```lgl
palette entries "Button"
with defaults, properties
```

The returned object text should be directly copyable into patch text whenever
possible:

```lgl
Button = Button(text: "")
InventorySlot = widget(class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C")
PluginFancy = widget(palette: "widget.palette:plugin-fancy")
```

Normalized JSON:

```ts
interface PaletteResult {
  kind: "palette_result";
  target: Target;
  entries: CreationEntry[];
}

type CreationEntry =
  | ShortcutEntry
  | ClassEntry
  | PaletteEntry;

interface ShortcutEntry {
  name: string;
  constructor: Call;
  defaults?: Record<string, Expr>;
  properties?: Property[];
  pins?: Pin[];
}

interface ClassEntry {
  name: string;
  class: string;
  label?: string;
  category?: string;
  defaults?: Record<string, Expr>;
  properties?: Property[];
}

interface PaletteEntry {
  name: string;
  palette: PaletteSourceRef;
  label?: string;
  category?: string;
  defaults?: Record<string, Expr>;
  properties?: Property[];
  pins?: Pin[];
}

interface Property {
  name: string;
  type: string;
  default?: Expr;
  writable?: boolean;
  category?: string;
}
```

`ShortcutEntry` is for semantic constructors, `ClassEntry` for class-path
creation identities, and `PaletteEntry` for action, palette, or template ids.
Domains define which forms they return and how patch text consumes them.

This grouped `PaletteResult` is not a universal result requirement. A migrated
domain may return creation bindings inside its ordinary ordered domain result.
The Graph domain does exactly that: Graph Palette queries return
`GraphResult.statements` and do not use `ShortcutEntry`, `ClassEntry`, or a
separate Graph `PaletteResult`.

## Results And Diagnostics

Bridge-facing results use normalized JSON plus diagnostics. SDK results format
successful objects back to LGL text.

Normalized JSON:

```ts
interface Result {
  object?: LglObject;
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

Queries and mutations use the same `object: LglObject` content model and the
same LGL formatter. `MutationResult` only adds execution state around that
ordinary object; it does not introduce a second mutation-specific object or
text format. Optional revision fields remain absent from a concrete tool's
public response until that tool enforces them.

`Comment.text` stores the content without text delimiters. A one-line value
formats as `# text`; a value containing a newline formats between depth-zero
`###` delimiter lines. The delimiters and a block's final line ending are not
part of `text`. Comments are data in the ordered object, not an auxiliary
comments array. Schema is one use of an ordinary multi-line Comment, not a new
result statement type.

A domain result that represents multiple LGL statements must serialize them in
one `statements` array. Bindings, comments, and any domain-owned statement
shapes are interleaved in exact formatter order. It must not serialize parallel
arrays such as `nodes`, `pins`, `properties`, `defaults`, and `comments` and
then ask the formatter to reconstruct reading order. Each domain defines its
closed statement union; the class domain's first concrete model is defined in
[`domains/class.md`](domains/class.md).

The ordered result container is normalized JSON only. It does not add
`document(...)`, `result(...)`, section syntax, or any other LGL text form.
Blank lines are canonical formatting and are not result statements.

`page.next` is opaque. Agents should pass it back through `page after` without
parsing it. Diagnostics should be teachable: they should name the failed
construct and, when possible, point to the query, patch, or object text that
should be changed.

## Core Rules

1. LGL text is a statement list.
2. A depth-zero newline ends a statement; newlines inside matched `()`, `[]`,
   or `{}` are presentation-only whitespace.
3. Structure comes from bindings, references, constructors, arrays, inline
   objects, and domain statements.
4. `{}` is only an inline value object, never a structural block.
5. `[]` is only an array.
6. Constructors use named arguments.
7. Positional constructor arguments are not part of LGL.
8. Query text and patch text use statement lists, not JSON-like bodies.
9. Sugar text must have a canonical text form.
10. Canonical text must lower to normalized JSON.
11. The language core does not decide whether a symbol is a class, enum, asset,
   field, node type, or operation. Domains and adapters resolve those meanings.
12. Comments normalize to ordered `Comment` objects. `# text` is the one-line
    form and depth-zero `###` delimiter lines enclose the multi-line form.
13. `invoke` is the shared Patch operation for schema-discovered object
    Operations; domains and adapters own the available Operations and outputs.
