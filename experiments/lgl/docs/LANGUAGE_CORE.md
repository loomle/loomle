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
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)
print = node(graph: g, id: "node-guid", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "<FMemberReference native text>")
```

Each statement should fit on one line whenever practical. Blank lines are
allowed. Indentation is visual only and does not create hierarchy.

## Statements

| Statement | Syntax | Example |
| --- | --- | --- |
| Comment | `# text` | `# Inspect a Blueprint event graph.` |
| Binding | `target = Expression` | `g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)` |
| Domain statement | domain-defined line | `query g` |
| Sugar statement | domain-defined shorthand | `begin.Then -> print.Exec` |

Bindings name objects or values so later statements can reference them. The
binding target is usually a local identifier:

```lgl
delay = node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_Delay")
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
| Constructor | `Name(arg: value)` | `node(graph: g, type: "/Script/BlueprintGraph.K2Node_Delay")` |
| Reference | `name` | `g` |
| Member reference | `name.member` | `begin.Then` |
| Id reference | `@id` | `@A001` |
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
aliases and member paths within one LGL document, and use `@id` across queries
or later operations.

Id references provide explicit stable ids when a domain needs them:

```lgl
@A001
```

`@id` is the common cross-query reference for concrete objects with a native UE
identifier. Domains map the same public `id` field to their native identity,
such as BlueprintGuid, GraphGuid, VarGuid, VariableGuid, TimelineGuid,
NodeGuid, or PinId. The query target and expected relationship provide owner
and object-kind context; the ref itself does not repeat them.

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
  | { kind: "id"; id: string };
```

Member references remain two-segment, document-local paths. Stable cross-query
identity uses `@id`; any richer public reference syntax requires a separate
language-design decision.

## Constructors

Constructors create typed LGL objects:

```lgl
asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)
node(graph: g, id: "A002", type: "/Script/BlueprintGraph.K2Node_Delay")
```

Constructor arguments are named because they are clear for agents, easy to
validate, and safe to evolve:

```lgl
g = graph(domain: blueprint, asset: bp, id: "graph-guid", name: EventGraph, type: event_graph)
```

Positional constructor arguments are not supported:

```lgl
g = graph(blueprint, bp, EventGraph)
```

## UE Native Type Text

LGL does not define or translate a separate UE value type system. When a
domain-owned `type` field describes a UE value type, it carries the canonical,
parseable native type text of the UE system that owns the object. For example,
reflected Properties, Blueprint variables, and graph Pins may use different
UE-native type representations because UE itself stores and validates them
through different structures.

The owning adapter selects the native codec, preserves every required native
type detail, and validates the text through UE. Query results must return type
text that can be copied back into a compatible create or edit operation.

LGL must not replace native type text with friendly aliases or LGL-specific
constructors such as translated object, container, Struct, or Enum types. It
must not use localized editor display labels as type identity. The language
core preserves the native type literal; it does not interpret, normalize, or
maintain a second JSON or AST representation of the type.

Other domain fields may also be named `type`, such as an asset type, Graph
role, or Node type. Those are domain-owned categories and are not UE value type
expressions.

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
  type: blueprint
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
find @id
```

The plural object form enumerates or searches one domain-owned collection. The
singular object form resolves one exact object by its current local name inside
the bound target. `find` has one meaning only: resolve one existing object by a
stable id without requiring the caller to repeat its object kind. A domain
supports only the forms that match its UE objects; for example, Class
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
| primary operation | choose one domain-defined read | `assets "door"`, `context @node-id depth 2` |
| `where` | structured filter expression | `where type = blueprint and not loaded` |
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
where root = "/Game" and type = blueprint

nodes "Print"
where type = "/Script/BlueprintGraph.K2Node_CallFunction"
```

Field-level fuzzy conditions are allowed for advanced filters, but primary
search text belongs on the plural collection operation. Domains may define
operation-local arguments, such as graph palette pin-context arguments `from
<pin>` and `to <pin>`.

Condition expressions use a small SQL-like subset:

```lgl
where type = blueprint
where root = "/Game" and type = blueprint
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
stable-id operations carry `id`. Relationship operations define their own
fields.

The old normalized `find` property represented the earlier find-centric text
model and is not part of the target contract. `find @id` becomes an ordinary
operation such as `{kind: "find_by_id", id: "..."}`; other primary operations
must not be forced through a field named `find`.

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
find @id
with schema
```

An exact name operation may request the same expansion:

```lgl
query actorClass
property Health
with schema
```

The subject must resolve to exactly one existing object or one exact creation
entry. Existing objects use either a stable `@id` or a domain-owned singular
name operation; creation entries use the owning domain's exact palette
identity. `with schema` is not valid for summary results, collections, or
ambiguous palette searches.

The ordinary object or creation text remains the query result. The adapter adds
`#` comments that describe the primary subject's usable fields: field name,
native UE type text, readable/writable status, required/default behavior,
source, and constraints when known. The result does not introduce `schema(...)`,
`field(...)`, or another schema-specific object syntax.

Schema applies only to the primary subject. It does not recursively add schemas
for expanded children such as Pins. Child objects require their own exact
query. An adapter that cannot provide the requested schema returns a capability
diagnostic rather than silently omitting it.

For a creation entry, `with schema` describes accepted creation fields and
constraints in the current domain context. A domain may expose an identity for
the creation entry itself; that identity is not a future object id. The query
does not invent Node, Pin, or other instance ids before UE creates them. After
creation, the mutation result returns the real objects and ids, whose instance
schema may be queried separately.

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
sugar statement
```

Domains own operations such as `add`, `set`, `reset`, `connect`, `insert`,
`move`, or widget tree edits. An operation exists only where its domain defines
an exact meaning; for example, Class Defaults use `reset` to remove a local
default override and resume inheritance. Bindings make later operations
precise:

```lgl
patch g
print = node(palette: "P_PrintString")
add print
connect begin.then -> print.execute
```

`add <binding>` is shared patch sugar for creating a binding and adding it in
one line:

```lgl
add print = node(palette: "P_PrintString")
```

Canonical text splits it into a binding plus an add operation:

```lgl
print = node(palette: "P_PrintString")
add print
```

The normalized JSON still contains one binding and one add operation. Domains
own what `add` means for the bound target.

Patch sugar follows the same sugar text to canonical text to normalized JSON
pipeline described above.

Normalized JSON:

```ts
interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  bindings: Binding[];
  ops: PatchOp[];
}
```

Patch operation payloads are domain-owned. The shared envelope only defines the
target, dry-run flag, bindings, and ordered operation list. Shared sugar lowers
before domain validation:

```txt
add <binding> -> <binding> + domain add operation
```

`PatchOp` is the schema union of domain operation payloads. Dry run is a
mutation mode, not a separate language: parse, resolve, validate, and plan
through the real path, then stop before applying changes.

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

`Comment.text` stores one non-empty line after the canonical `# ` prefix; it
contains neither that prefix nor a newline. A formatter adds the prefix;
comments are data in the ordered object, not an auxiliary comments array.

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
2. One line should carry one complete statement whenever practical.
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
