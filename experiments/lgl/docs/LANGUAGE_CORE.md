# LGL Language Core

## Scope

The language core defines the text shape shared by all LGL modules:
statements, expressions, values, bindings, constructors, references, and
syntax sugar normalization.

It does not define graph nodes, asset registry search, widget trees, or
UE-specific patch operations. Those belong to module documents.

## Basic Form

LGL is a line-oriented object language. A document is a sequence of statements:

```lgl
# Inspect a Blueprint event graph.
bp = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)

begin = node(graph: g, type: EventBeginPlay, id: "A001")
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)
delay.Duration = pin(type: float, direction: in, value: 1.0)

begin.Then -> delay.Exec/Completed -> print.Exec
```

Each statement should fit on one line whenever practical. Blank lines are
allowed for readability. Indentation does not create hierarchy.

## Statements

| Statement | Syntax | Example |
| --- | --- | --- |
| Comment | `# text` | `# Inspect a Blueprint event graph.` |
| Binding | `target = Expression` | `g = graph(domain: blueprint, asset: bp, graph: EventGraph)` |
| Module statement | module-defined line | `query g` |
| Sugar statement | module-defined shorthand | `begin.Then -> print.Exec` |

Bindings name objects or values so later statements can reference them. The
binding target is usually a local identifier:

```lgl
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)
```

Modules may also allow member references as binding targets when the member is
the object's stable identity. Graph pin readback uses this form:

```lgl
delay.Duration = pin(type: float, direction: in, value: 1.0)
```

Module statements are owned by a module. The core language only requires them
to remain line-oriented and normalize through the same text-to-JSON pipeline.

## Query Statements

Query statements use one shared shape across modules:

```lgl
query target
find ...
where condition
with expansion, expansion
order by key asc, key desc
limit number
```

`query` and `find` identify the target and what to search for. `where`,
`with`, `order by`, and `limit` are optional.

| Clause | Purpose | Example |
| --- | --- | --- |
| `query` | target domain or bound object | `query asset`, `query g` |
| `find` | module-defined result kind | `find assets`, `find nodes` |
| `where` | one condition expression | `where type = blueprint and not loaded` |
| `with` | expand beyond the module default result | `with registryTags`, `with pins, defaults` |
| `order by` | deterministic result ordering | `order by score desc, path asc` |
| `limit` | maximum result count | `limit 10` |

There is no `select` clause. Each module defines its default result shape.
`with` only requests additional expansion beyond that default.

Condition expressions use a small SQL-like subset:

```lgl
where type = blueprint
where root = "/Game" and type = blueprint
where type = blueprint and (text ~= "door" or text ~= "gate")
where not loaded
```

Supported condition operators:

| Operator | Meaning |
| --- | --- |
| `=` | equal |
| `!=` | not equal |
| `~=` | module-defined contains or fuzzy contains |
| `>` `>=` `<` `<=` | ordered comparison |
| `not` | negation |
| `and` | conjunction |
| `or` | disjunction |
| `(...)` | grouping |

Condition precedence follows the usual SQL subset: parentheses first, then
`not`, then `and`, then `or`.

## Expressions

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

Quoted values are strings. Unquoted words are symbols resolved by modules or
adapters. In the current normalized JSON contract, symbols map to `Name`.

## Constructors

Constructors create typed LGL objects:

```lgl
asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
graph(domain: blueprint, asset: bp, graph: EventGraph)
node(graph: g, type: Delay, id: "A002", Duration: 1.0)
```

All constructor arguments are named:

```lgl
name: value
```

Named arguments are required because they are clearer for agents, easier to
validate, and safer to evolve:

```lgl
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
```

Positional constructor arguments are not part of LGL:

```lgl
g = graph(blueprint, bp, EventGraph)
```


## References

References point at values or objects already named by earlier statements:

```lgl
bp
g
```

Member references are module-owned:

```lgl
begin.Then
delay.Duration
```

In the graph module, `begin.Then` refers to a pin on the node binding `begin`.
Other modules may define their own member meanings.

Id references provide explicit stable ids when a module needs them:

```lgl
@A001
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

Braces are not used for document structure, object hierarchy, or module
blocks:

```lgl
asset "/Game/BP_Door.BP_Door" {
  type: blueprint
}
```

## Core Rules

1. LGL text is a statement list.
2. One line should carry one complete statement whenever practical.
3. Structure comes from bindings, references, constructors, arrays, inline
   objects, and module statements.
4. `{}` is only an inline value object, never a structural block.
5. `[]` is only an array.
6. Constructors use named arguments.
7. Positional constructor arguments are not part of LGL.
8. Query and patch documents are statement lists, not JSON-like bodies.
9. The language core does not decide whether a symbol is a class, enum, asset,
   field, node type, or operation. Modules and adapters resolve those meanings.

## Sugar And Canonicalization

Modules may define sugar text for agent convenience. Sugar must normalize
without UE state or schema lookup.

Graph path sugar:

```lgl
begin.Then -> delay.Exec/Completed -> print.Exec
```

Canonical graph text:

```lgl
edge(begin.Then, delay.Exec)
edge(delay.Completed, print.Exec)
```

Patch context may normalize similar sugar into operation calls:

```lgl
connect begin.Then -> print.Exec
```

Canonical patch text:

```lgl
connect(begin.Then, print.Exec)
```

## Normalized JSON

Normalized JSON is the schema and RPC contract. Text-level bindings,
references, and sugar must normalize to explicit JSON fields such as `Target`,
`GraphRef`, `PinRef`, `Edge`, and patch operation objects.

The UE bridge executes normalized JSON, not raw LGL text. Agents should read
and write LGL text; parsers, validators, and adapters own the conversion.
