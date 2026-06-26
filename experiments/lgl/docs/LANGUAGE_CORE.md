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
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
print = node(graph: g, type: PrintString, InString: "Ready")
```

Each statement should fit on one line whenever practical. Blank lines are
allowed. Indentation is visual only and does not create hierarchy.

## Statements

| Statement | Syntax | Example |
| --- | --- | --- |
| Comment | `# text` | `# Inspect a Blueprint event graph.` |
| Binding | `target = Expression` | `g = graph(domain: blueprint, asset: bp, graph: EventGraph)` |
| Domain statement | domain-defined line | `query g` |
| Sugar statement | domain-defined shorthand | `begin.Then -> print.Exec` |

Bindings name objects or values so later statements can reference them. The
binding target is usually a local identifier:

```lgl
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)
```

Domains may also allow member references as binding targets when the member is
the object's stable identity. Graph pin object text uses this form:

```lgl
delay.Duration = pin(type: float, direction: in, value: 1.0)
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
domain-owned stable members such as `delay.Duration`, `door.Health`, or
`stack.start`. Domains define valid member targets. Most bindings normalize to
`Expr`; graph patch creation bindings may normalize to `NodeCreation`.

## Expressions And Values

| Expression | Syntax | Example |
| --- | --- | --- |
| Constructor | `Name(arg: value)` | `node(graph: g, type: Delay)` |
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

References point at previously named values or objects. Member references are
domain-owned:

```lgl
begin.Then
delay.Duration
```

In the graph domain, `begin.Then` refers to a pin on the node binding `begin`.
Other domains may define their own member meanings.

Id references provide explicit stable ids when a domain needs them:

```lgl
@A001
```

Normalized JSON:

```ts
type Ref =
  | { kind: "local"; name: string }
  | { kind: "member"; object: string; member: string }
  | { kind: "id"; id: string };
```

Member references are two-segment core references. Domains may define richer
refs when they need owners, paths, graph refs, or UE-specific identity.

## Constructors

Constructors create typed LGL objects:

```lgl
asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
graph(domain: blueprint, asset: bp, graph: EventGraph)
node(graph: g, type: Delay, id: "A002", Duration: 1.0)
```

Constructor arguments are named because they are clear for agents, easy to
validate, and safe to evolve:

```lgl
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
```

Positional constructor arguments are not supported:

```lgl
g = graph(blueprint, bp, EventGraph)
```

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

Query text uses a shared multi-line shape:

```lgl
query <target>
find ...
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

Query text is clause-per-line. Do not combine `query`, `find`, `where`, `with`,
`order by`, or `page` on one line. Object text may prefer compact single-line
statements, but query text should keep its structure visible.

Query syntax summaries use literal words for required keywords, `<name>` for
semantic placeholders, `[item]` for optional parts, and `a|b` for choosing one
literal.

| Clause | Purpose | Example |
| --- | --- | --- |
| `query` | target domain or bound object | `query asset`, `query g` |
| `find` | domain-defined result kind and optional primary search text | `find assets "door"`, `find palette entry "Print String"` |
| `where` | structured filter expression | `where type = blueprint and not loaded` |
| `with` | expand beyond the domain default result | `with registryTags`, `with pins, defaults` |
| `order by` | deterministic result ordering | `order by score desc, path asc` |
| `page limit` | maximum result count | `page limit 50` |
| `page after` | continue after a returned cursor | `page after "cursor"` |

There is no `select` clause. Each domain defines its default result shape.
`with` only requests additional expansion beyond that default.

The quoted text after `find` is the primary search text. `where` is for
structured filters:

```lgl
find assets "door"
where root = "/Game" and type = blueprint

find nodes "Print"
where type = PrintString
```

Field-level fuzzy conditions are allowed for advanced filters, but primary
search text belongs on `find`. Domains may define find-form local arguments,
such as graph pin-context arguments `from <pin>` and `to <pin>`.

Condition expressions use a small SQL-like subset:

```lgl
where type = blueprint
where root = "/Game" and type = blueprint
where comment ~= "debug"
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

Normalized JSON:

```ts
interface Query {
  kind: "query";
  target: Target;
  find?: Find;
  where?: Condition;
  with?: Detail[];
  orderBy?: OrderBy[];
  page?: Page;
}

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

Domain documents define `find` payloads, supported `where` fields, `with`
items, ordering keys, and pagination defaults. `~=` lowers to `contains`;
domains decide its exact match behavior.

Pagination is cursor-based. If `page limit` is omitted, domains normally use
50. If `page after` is omitted, the query returns the first page. Results with
more data return an opaque cursor that agents pass back unchanged:

```lgl
query g
find palette entry "Print String"
with pins
page limit 50

query g
find palette entry "Print String"
with pins
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

Domains own operations such as `add`, `set`, `connect`, `insert`, `move`, or
widget tree edits. Bindings make later operations precise:

```lgl
patch g
print = node(graph: g, type: PrintString, InString: "Ready")
add print
connect begin.Then -> print.Exec
```

`add <binding>` is shared patch sugar for creating a binding and adding it in
one line:

```lgl
add print = node(graph: g, type: PrintString, InString: "Ready")
```

Canonical text splits it into a binding plus an add operation:

```lgl
print = node(graph: g, type: PrintString, InString: "Ready")
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

Agent-facing query text uses domain find forms such as:

```lgl
find palette entry "Button"
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
