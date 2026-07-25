# SAL Language Core

## Purpose

SAL is a line-oriented language for expressing UE Targets, ordinary object
data, native identities, relationships, queries, and ordered mutations. Core
defines shared syntax and normalized structure. Each Domain defines its Target
fields, identity environment, operations, Palette, schema, and UE behavior.

## Invariants

1. `{...}` is the only ordinary object expression.
2. An optional semantic tag is erasable presentation metadata.
3. `target { domain: ... }` is structural syntax, not an object expression.
4. `Target.domain` is the only Domain selector.
5. One request has one active Target and one Domain.
6. StableRefs are native identity paths relative to one exact Target.
7. A Target never contains another Target.
8. Parentheses never represent objects. They are used only by true calls or by
   an explicitly defined non-object grammar such as condition grouping and
   Graph coordinate pairs.
9. Cross-Domain work uses an independent related Target and explicit handoff.

## Lexical Structure

SAL is UTF-8 text. A depth-zero newline ends a statement. A newline inside
matched `{}`, `[]`, or grammar-defined `()` is ordinary whitespace; wrapping
must not change normalized JSON, statement order, or execution. Delimiters must
balance, indentation has no meaning, and SAL has no continuation backslash.
Comma requirements do not change merely because an expression wraps.

Quoted strings do not become multiline strings when their containing
expression wraps. After the final delimiter closes, the next depth-zero
newline ends the statement. An unclosed delimiter is a syntax error at its
opening span; the parser never guesses where a wrapped statement should end.

Identifiers use:

```text
[A-Za-z_][A-Za-z0-9_]*
```

The parser, normalized Schema, and Bridge validator maintain the same reserved
set. Parser/Schema parity and Bridge conformance tests keep their behavior
aligned. The set contains the JSON literals, the retired generic label
`object`, `target`, `domain`, the six Domain names, and the irreducibly
ambiguous exact-operation prefixes `tree`, `context`, and `palette`. A reserved
word cannot be a semantic tag, local alias, or unquoted SAL Name. It remains
legal as an ordinary object field key, where its position is data rather than
grammar.

JSON strings provide lossless spelling for arbitrary field keys and identity
segments. Numbers are finite JSON numbers; formatting preserves the distinct
JSON spelling `-0`, while `NaN` and infinities are invalid. Booleans and `null`
follow JSON meaning. Native UE symbolic values such as `GT_Function` remain SAL
Names rather than quoted strings when their exact native spelling fits the Name
grammar.

Comments are ordinary depth-zero ordered statements:

```sal
# one line

###
several lines
###
```

An exact line containing only `###` opens or closes a multiline Comment. The
delimiter lines are not content. Interior text and blank lines are preserved
verbatim and are opaque to SAL: indentation, braces, references, and apparent
operations inside the block are not parsed. Multiline Comments do not nest,
and an unclosed block is a syntax error at its opening delimiter.

Normalized `Comment.text` stores content without delimiters. A formatter uses
`# text` for one line and `###` for text containing a newline. A normalized
multiline value therefore cannot contain a line that trims to `###`; adapters
must escape such opaque native text or preserve it in a string field. The
single-line value `###` remains representable as `# ###`.

Comments cannot appear inside a delimited expression. Their placement is
semantic result order and must be preserved without regrouping.

## Expressions

### Scalars And Arrays

```sal
null
true
42
3.5
"UE string"
GT_Ubergraph
[1, 2, { key: "value" }]
```

### Object Expression

```text
object_expression =
  [semantic_tag whitespace] "{" [member {"," member}] "}"

member =
  (identifier | json_string) ":" expression
```

```sal
{
  id: "N",
  type: "/Script/...",
  "key with space": true,
  nested: { values: [1, 2] }
}
```

The JSON-compatible subset round-trips without loss. Duplicate decoded keys
are invalid.

An optional tag may improve reading:

```sal
node {
  id: "N",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
```

Tag erasure produces the same executable value:

```sal
{
  id: "N",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
```

A tag never:

- chooses a Domain;
- determines native or SAL type;
- supplies identity;
- selects a schema or operation;
- routes creation;
- changes validation, planning, effects, or mutation.

The normalized form stores fields separately from optional presentation:

```ts
interface ObjectExpr<E> {
  kind: "object";
  fields: Record<string, E>;
  semanticTag?: string;
}
```

The object fields named `kind`, `id`, `callee`, or `args` remain ordinary data
inside `fields`; they cannot collide with AST structure.

### Parentheses And True Calls

Parentheses are never an alternative object delimiter. Operation invocation
uses a true call:

```sal
invoke @node-guid Rename(displayName: "Start Button")
```

Domain operation names and arguments are validated by exact schema. Core does
not interpret an operation name as an object kind. Arguments are named;
positional call arguments are unsupported.

Parentheses may also occur where a grammar explicitly assigns non-object
meaning, for example:

```sal
where not (loaded or path ~= "/Developers/")
move @node-guid to (640, 0)
```

The first is condition grouping and the second is a Graph coordinate pair.
Neither creates an Expr, ObjectExpr, or operation invocation.

## Domain Targets

### Surface

```text
target_expression =
  "target" "{"
  "domain" ":" domain_name
  { "," target_field ":" json_string }
  "}"
```

Domain values are the six structural keywords:

```text
asset | blueprint | class | graph | state_tree | widget
```

Every field after `domain` has a non-empty JSON string value. Domains close
the accepted field set.

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

The following is invalid because Targets are flat:

```sal
# invalid
g = target {
  domain: graph,
  owner: { asset: "/Game/BP_Door.BP_Door" },
  id: "22222222-2222-2222-2222-222222222222"
}
```

Target fields and completeness are:

| Domain | Query form | Canonical exact form and Patch |
| --- | --- | --- |
| Asset | root, or `path` with optional `type` | `path + type` |
| Blueprint | `asset`, optional `id` | `asset + id` |
| Class | `path` | `path` |
| Graph | `asset + id` or `asset + name`, optional `blueprintId` | `asset + blueprintId + id` |
| StateTree | `asset`, optional `type` | `asset + type` |
| Widget | `asset`, optional `id` | `asset + id` |

When Graph supplies both `id` and `name`, `id` selects identity and `name` is a
strict readable-state assertion. Canonical readback drops `name`.

Target paths, native types, and Guid fields canonicalize after opening. Guid
text is non-zero and uses lowercase digits with hyphens, matching
`FGuid::IsValid()`.

### Request Binding

One Query or Patch prelude binds exactly one active Target:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

query door
summary
```

Target aliases are request-local and do not survive into another request.

## References

### Local Reference

A local alias points to a binding or operation output inside one text:

```sal
print = { palette: "P_PrintString" }
add print
set print.NodeComment = "Created here"
```

Local aliases are presentation handles, not cross-request identity.

### Stable Reference

```text
stable_ref =
  [semantic_tag whitespace]
  "@" identity_segment { "/" identity_segment }

identity_segment =
  safe_bare_segment | json_string
```

```sal
@node-guid
@node-guid/pin-guid
@"owner/part"/"leaf.with.dot"
```

Identity paths are interpreted only after the exact Target and Domain are
known. Path components come from UE's native identity contract. Display names,
semantic tags, current array positions, and collection words never become
identity components.

Optional tags decorate a StableRef:

```sal
node @node-guid
pin @node-guid/pin-guid
```

They remain optional AST presentation metadata, but are excluded from identity
equality, identity hashing, native lookup, planning, and mutation.

The normalized form is:

```ts
interface StableRef {
  kind: "stable_ref";
  identityPath: [string, ...string[]];
  semanticTag?: string;
}
```

### Owner Scope

If a native local id is not globally unique in the Target, its native identity
owner precedes it:

```sal
@node-guid/pin-guid
@parameter-container-guid/property-guid
@function-graph-guid/local-variable-guid
```

The required path shape does not change merely because the current asset
happens to contain one matching local id.

### Member Reference

Member paths follow identity and remain distinct:

```sal
@node-guid/pin-guid.DefaultValue
@node-guid.NativeArray[0].Value
```

The StableRef selects the object; the member path selects current schema state
inside that object. A member path is not independent stable identity.

### Target Self

The active Target is read structurally in every Domain:

```sal
query g
target
with schema
```

The References operation also has a structural Target-self subject:

```sal
query g
references to target
```

It receives no synthetic StableRef. Native support is intentionally narrower
than the grammar: bare `target` resolves only when the exact Graph Target is a
callable Function or Macro declaration. A Graph Target's direct
`target.InterfaceGuid` member resolves when that native Interface declaration
exists. Other Graph roles and the other five Domains return
`capability.reference_unavailable` while retaining exact Target context.

### Scoped Result Reference

Results may contain several independent Targets. An unqualified StableRef is
relative to the main Target; a foreign one uses:

```sal
bp::@variable-guid
```

```ts
interface ScopedStableRef {
  kind: "scoped_stable_ref";
  target: LocalRef;
  reference: StableRef;
}
```

This is result-side normalized structure. A request may spell the active
Target redundantly, for example `g::@identity` in a request whose active alias
is `g`; the parser immediately lowers that spelling to an ordinary unscoped
StableRef. A foreign or unknown qualifier is rejected. To follow a related
Target, the caller copies it into a new request as the active Target and uses
an ordinary Target-relative StableRef.

## Object Text Statements

Object Text is an ordered list:

```ts
interface ObjectText {
  statements: Statement[];
}

type Statement = Binding | Edge | Comment;
```

Domain documents may show an Object Text excerpt without repeating the result
envelope. A real Bridge response still carries the explicit result context and
Target table defined below.

### Binding

```sal
alias = <expression>
owner.member = <expression>
```

Examples:

```sal
beginPlay = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}

beginPlay.then = pin {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "<native FEdGraphPinType text>",
  direction: out
}
```

A binding target is unique inside one Object Text. A referenced local alias
must already be bound.

### Edge

```sal
beginPlay.then -> print.execute
```

An Edge describes one ordered relationship. Domains decide endpoint validity
and meaning. In Patch, a bare Edge is state description and cannot authorize a
mutation; use a Domain operation such as `connect`, `disconnect`, `bind`, or
`unbind`.

## Query Text

```text
<target binding>

query <target alias>
<one primary operation>
[where <condition>]
[with <detail>, ...]
[order by <field> [asc|desc], ...]
[page limit <count>]
[page after <json string>]
```

Core structural operations are:

```sal
target
@identity
references to target
references to @identity.Member
palette entries ["text"]
palette @id
```

Domains add operations such as `summary`, collections, `tree`, `context`,
`exec flow`, and `data flow`. Each static Domain card closes the valid
operation and clause matrix. In particular, accepting the structural
`target_self` subject does not promise a native provider: current Target-self
reference support is limited to callable Function/Macro Graph Targets and the
direct Graph `target.InterfaceGuid` member described above.

Conditions support:

```text
= != ~= > >= < <= not and or ( )
```

Precedence is parentheses, then `not`, then `and`, then `or`. Conditions lower
to a closed tree rather than an opaque string:

```ts
type Condition =
  | { kind: "eq"; field: FieldPath; value: RequestExpr }
  | { kind: "ne"; field: FieldPath; value: RequestExpr }
  | { kind: "contains"; field: FieldPath; value: RequestExpr }
  | {
      kind: "compare";
      op: "gt" | "gte" | "lt" | "lte";
      field: FieldPath;
      value: RequestExpr;
    }
  | { kind: "not"; condition: Condition }
  | { kind: "and"; conditions: [Condition, Condition, ...Condition[]] }
  | { kind: "or"; conditions: [Condition, Condition, ...Condition[]] };

interface FieldPath {
  path: [string, ...string[]];
}
```

`~=` lowers to `contains`; the Domain defines its exact contains or fuzzy
semantics. Domains close allowed fields, operand value types, and operators.
Boolean shorthand such as `loaded` is Domain-defined sugar for an explicit
condition. Unsupported fields, operators, clauses, and clause combinations are
errors, never ignored input.

Exact object and Palette reads may request dynamic schema:

```sal
query g
@node-guid
with schema
```

### Shared `with schema` Contract

`schema` is the shared discovery expansion for one exact subject. Valid
subjects are:

- the exact active Target through `target`;
- one exact existing object selected by StableRef or a Domain-owned singular
  name operation;
- one exact object-backed value surface, such as a Class Default;
- one exact creation entry selected by its Domain Palette identity.

It is invalid on summaries, collections, ambiguous Palette searches, or any
operation that does not resolve exactly one primary subject. The ordinary
object, value, or Palette entry remains the result. Immediately after it, the
adapter emits one multiline Comment with the complete currently usable
contract. No `schema` object, schema tag, or second expression model is added.

The stable Comment sections are:

```sal
###
schema

fields:
  NodeComment: FString; read, write

query:
  exec flow from|to @identity [depth N]
    availability: available
    with: layout

operations:
  AddExecutionPin()
    availability: available
    output pin: one Pin
    invoke: invoke @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa AddExecutionPin() as next

patch:
  save
    availability: unavailable
    reason: package has no persistent owner

copy:
  query g
  @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
  with schema
###
```

The sections have these exact responsibilities:

- `fields:` lists native UE type text, read/write/reset status,
  required/default behavior, source, and known constraints;
- `query:` lists primary operations accepted when the subject is itself the
  request Target, including accepted clauses, expansions, availability, and
  copyable text;
- `operations:` lists adapter-owned target-local operations invoked through
  `invoke`, including named parameters, availability, primary outputs,
  copyable invocation, and UE source when useful;
- `patch:` lists direct Patch statements accepted when the subject is the
  request Target, with current availability and constraints;
- `copy:` contains complete request text that can be copied without inventing
  missing context.

The Comment is opaque text, not nested SAL or Markdown grammar. There is no
separate `with operations`: one exact `with schema` read must describe
everything currently readable, writable, resettable, invokable, or directly
patchable on that subject. A temporarily unavailable Query, Patch statement,
or operation remains listed with the UE-derived reason. If the adapter cannot
provide the requested exact schema, it returns a capability diagnostic rather
than silently omitting the schema.

Operation names are stable PascalCase adapter contracts grounded in UE editor
actions or native interfaces; arbitrary C++ methods are not exposed. Lookup is
local to the exact object schema, not a global operation namespace. The same
name on two object types may have different parameters, outputs, effects, and
availability.

Instance schema is live and instance-sensitive. It lists every operation
supported by that subject type and marks current availability. Schema applies
only to the primary subject and does not recursively attach schemas to returned
children.

Creation-entry schema is deliberately different from instance schema. It
describes creation fields, constraints, fixed initial facts, and only those
operations determinable for the initial created state in the current Target
context. A Palette entry id identifies the creation capability, not the future
object. It cannot fabricate future native ids. After creation, readback returns
the real native objects and identities; their instance schema may differ and
must be queried separately.

## Patch Text

```text
<canonical exact target binding>

patch <target alias> [dry run]
<binding or operation>
...
```

Core operations are:

```sal
add <binding> [to <destination>|before <anchor>|after <anchor>]
remove <object>
set <object>.<field> = <value>
reset <object>.<field>
move <Domain-defined operands>
invoke <object> <Operation>(namedArguments) [as <alias>]
save
```

Domains extend this list. Examples include Graph `connect`, Widget `wrap`,
StateTree `bind`, and Blueprint or StateTree `compile`.

Bindings for objects that do not yet exist are ordinary ObjectExpr values:

```sal
print = { palette: "P_PrintString" }
add print
```

A semantic tag may be emitted but remains erasable:

```sal
print = node { palette: "P_PrintString" }
add print
```

### Invoke Targets And Outputs

`invoke` is a statement, never an expression nested inside an ObjectExpr,
binding, or another call:

```text
invoke <object> <Operation>(named arguments) [as <outputs>]
```

The target is the active Target alias when its schema exposes the operation,
one StableRef, or an already materialized object alias. The operation name,
named arguments, availability, output roles, and effects are copied from that
exact subject's `with schema` result. Unknown operations, unavailable
operations, positional arguments, invalid arguments, unknown selectors,
duplicate aliases, or forward references invalidate the request.

For exactly one primary output, the schema permits a direct binding:

```sal
invoke @sequence-node-guid AddExecutionPin() as next
```

For multiple outputs, each selected output uses the exact adapter-provided
selector:

```sal
invoke @map-node-guid AddKeyValuePair()
  as key: newKey, value: newValue

invoke @vector-node-guid/vector-pin-guid SplitStructPin()
  as subpins.X: x, subpins.Z: z
```

SAL defines no universal `items`, `members`, or ordinal selector. A caller may
omit outputs it does not need. An omitted normalized selector is valid only
when the exact schema declares one primary output. Fixed outputs use named
roles; variable outputs use an ordered keyed role such as `subpins.X`.

An output may be newly created or an existing object made usable by the
operation. Output aliases become valid only after successful execution of the
`invoke` statement and then behave like ordinary materialized local
references. They never become stable cross-request identity. Removed objects,
reconstructed call sites, mirrored objects, disconnected Edges, and other
cascades are effects reported by preflight and mutation readback, not primary
outputs.

If preflight cannot determine the output shape or final native identity, later
statements cannot consume that output. The adapter must fail before live
mutation rather than publish a temporary or guessed id.

Patch statements execute in written order. Aliases become usable only after
their materializing statement. The whole authored batch is parsed, resolved,
validated, and planned before live mutation. `dry run` uses the same path and
stops before applying live state.

Terminal compile/save rules are Domain-defined. A terminal request is separate
from authored edits when its Domain card says so.

## Result Envelope

Every result uses one of three contexts:

```text
result exact_target
result domain_root
result unresolved_target
```

An exact result includes its canonical Target:

```sal
result exact_target
target g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
objects
call = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_CallFunction"
}
```

The Asset collection root uses:

```sal
result domain_root
target assets = target { domain: asset }
objects
...
```

An unresolved normalized result has no Target table and at least one error
diagnostic. Its first MCP text block remains only canonical Result Text:

```sal
result unresolved_target
no_objects
```

The diagnostic is a later independent MCP text block:

```sal
###
SAL diagnostics
ERROR resolution.target_not_found: ...
###
```

Once a Target opens, later operation failure retains `exact_target` and its
canonical Target.

### Related Targets And Handoffs

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

Rules:

- related Targets are canonical, flat, independent, and deduplicated;
- they never repeat the main Target;
- aliases are unique across the Target table and Object Text;
- each handoff points to one related Target alias;
- every related Target is retained by Object Text or a handoff;
- no tag or object field is used to reconstruct a Target.

`objects` begins ordered Object Text; `no_objects` represents its absence and
must be the final line of that canonical Result Text block. The Client never
appends mutation metadata or diagnostics to this block. It emits each as a
later independent MCP text block containing SAL comments. Those transport
annotations cannot fabricate `objects` or otherwise change object presence.

## Normalized Core Model

```ts
type Domain =
  | "asset"
  | "blueprint"
  | "class"
  | "graph"
  | "state_tree"
  | "widget";

interface TargetBase {
  kind: "target";
  domain: Domain;
}

interface TargetBinding<T extends Target = Target> {
  alias: string;
  target: T;
}

interface QueryRequest {
  kind: "query";
  target: TargetBinding<QueryAcceptedTarget>;
  operation: QueryOperation;
  where?: Condition;
  with?: [string, ...string[]];
  orderBy?: [OrderBy, ...OrderBy[]];
  page?: Page;
}

interface PatchRequest {
  kind: "patch";
  target: TargetBinding<CanonicalTarget>;
  dryRun: boolean;
  statements: [PatchStatement, ...PatchStatement[]];
}
```

Expression unions are closed:

```ts
type RequestExpr =
  | null
  | boolean
  | number
  | string
  | Name
  | RequestRef
  | ObjectExpr<RequestExpr>
  | RequestExpr[];

type ResultExpr =
  | null
  | boolean
  | number
  | string
  | Name
  | ResultRef
  | ObjectExpr<ResultExpr>
  | ResultExpr[];
```

Target is not an Expr. Normalized request references do not include
`ScopedStableRef`; parsed request text may use only a redundant qualifier for
its own active Target, which is lowered away. Result references may remain
scoped.

## Canonical Formatting

Canonical formatting:

- emits braces for every ordinary object;
- emits a semantic tag only by Domain presentation policy;
- emits explicit flat Targets and canonical field order;
- emits tag-free StableRefs by default, unless a readability policy adds an
  erasable tag;
- preserves statement and comment order;
- uses identifier keys and identity segments only when safe, otherwise JSON
  strings;
- never infers Domain or Target from native Class, object `type`, or tag.

Whitespace and local alias choice are not identity. Target and StableRef
canonical strings are.

## Legacy Compatibility

This section is the only place where the previous spellings are normative.
Only the direct TypeScript parser accepts them during protocol v3, and only
when its caller explicitly enables compatibility mode. MCP tools and the
default SDK facade do not enable that mode:

```sal
node(palette: "P") # legacy object call
graph(asset: bp, id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb") # legacy Target call
node@cccccccc-cccc-cccc-cccc-cccccccccccc # legacy fused kind reference
```

It lowers them before normal planning:

- `object(...)` becomes an untagged `ObjectExpr`; any other accepted legacy
  object callee becomes an erasable semantic tag, and a reserved callee is
  rejected rather than discarded;
- legacy Asset, Blueprint, Class, and Graph Target calls become one flat
  `target { domain: ... }`;
- legacy fused references become Target-relative native identity paths only
  when their shape is complete and unambiguous in the already selected active
  Domain;
- mixed or ambiguous legacy Domain requests are rejected. Every Target
  declaration must belong to the selected legacy Target's alias-dependency
  closure; unused declarations and mixtures of explicit v3 and legacy Targets
  are rejected rather than ignored.

The safe fused-reference window is closed:

| Active Domain | Accepted legacy fused reference shapes |
| --- | --- |
| Asset | none |
| Blueprint | one-component Dispatcher, Graph, Component, or Node; two-component Function-local Variable |
| Class | none |
| Graph | one-component Node, Dispatcher, or Component; two-component Pin or Function-local Variable |
| StateTree | one-component State, Node, Transition, or Object; two-component Parameter |
| Widget | one-component Widget |

Every identity component must be a complete non-zero native Guid and is
canonicalized during lowering. Under-scoped owner identities, target-self
fused references, and forms that would require a name lookup or UE-assisted
recovery are rejected with migration guidance.

The protocol v3 Bridge rejects normalized legacy Call and fused-reference
shapes; compatibility never changes Domain execution semantics. Current
formatters never emit the legacy spellings. The compatibility reader is
removed with protocol v4 unless a later release note explicitly extends the
window.
