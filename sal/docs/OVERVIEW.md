# SAL Overview

SAL is Loomle's compact language for agents to inspect and modify Unreal Engine
state. It preserves UE semantics rather than translating UE into a parallel
object model.

## Model

SAL separates four concerns:

```text
ordinary object data = {...}
presentation         = optional erasable semantic tag
execution scope      = one explicit Domain Target
identity             = native path relative to that Target
```

```sal
{ id: "N", type: "/Script/..." }
node { id: "N", type: "/Script/..." }

g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

@N
@N/P
```

Removing `node` cannot change anything executable. The active Domain comes
only from `Target.domain`; object `type` validates native state inside that
Domain; stable identity comes only from native identity path components.

Parentheses are reserved for true calls and explicitly defined non-object
syntax:

```sal
invoke @N Rename(displayName: "New Name")
```

## Object Expressions

Curly braces are SAL's universal object expression:

```sal
{
  id: "ordinary-data",
  "key with space": true,
  nested: {
    values: [1, 2, null]
  }
}
```

The JSON-compatible subset round-trips without loss. This includes preserving
`-0`; non-finite values are not JSON numbers and are rejected. SAL does not
require rewriting arbitrary JSON when there is no reason to express it as SAL.

A Domain may emit a semantic tag to improve reading:

```sal
pin {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "<FEdGraphPinType native text>",
  direction: in
}
```

Tags are neither types nor constructors. Reserved Core and Domain keywords
cannot be tags; this includes the retired generic label `object` and `tree`,
`context`, and `palette`, whose exact operation forms would otherwise be
ambiguous with tagged StableRefs.

## Domains And Targets

The nine Domains are:

- `asset`
- `blueprint`
- `class`
- `graph`
- `state_tree`
- `widget`
- `level` (Query-only)
- `pcg` (Query-only)
- `pcg_component` (Query-only)

Every request has exactly one Target and exactly one Domain. Targets are flat,
closed records whose non-`domain` values are non-empty JSON strings. They do
not contain aliases, arrays, object expressions, or nested Targets.

```sal
menuWidgets = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

menuBlueprint = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

These are different SAL Targets even though both open the same native
`UWidgetBlueprint`. The native Class does not compose the two capability
surfaces.

## Stable References

A StableRef is a native identity path interpreted inside one exact Target:

```sal
@node-guid
@node-guid/pin-guid
@parameter-container-guid/property-guid
```

The Domain fixes which native lookup sets and owner rules apply. Identity
paths contain no object-kind prefix and no collection labels such as `nodes`
or `pins`.

A Pin includes its owning NodeGuid even if its PinId currently looks globally
unique. A StateTree Parameter includes its owning container Guid. This keeps
identity stable when unrelated objects are copied.

Optional tags may decorate references:

```sal
node @node-guid
pin @node-guid/pin-guid
```

They do not participate in lookup. Member paths are separate from identity:

```sal
@node-guid/pin-guid.DefaultValue
```

## Query And Patch

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query g
@node-guid
with schema
```

```sal
patch g dry run
print = { palette: "P_PrintString" }
add print
connect @begin-node-guid/then-pin-guid -> print.execute
```

The Domain owns its Query operations, Patch operations, Palette, schema,
validation, planning, and native execution. Core owns the shared statement,
expression, Target, result, and diagnostic grammar.

## Results

Results carry explicit Target context:

```sal
result exact_target
target g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
objects
beginPlay = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
```

The result contexts are:

- `exact_target`: the Target opened and canonicalized;
- `domain_root`: the Query-only Asset collection root;
- `unresolved_target`: no Target opened and an error diagnostic is present.

The first MCP text block is canonical, round-trippable Result Text. It contains
the Target context, Target table, handoffs, and either `objects` plus real
Object Text or a terminal `no_objects`. Mutation metadata and diagnostics are
later independent SAL-comment text blocks. They never extend the first block
or manufacture object presence.

Cross-Domain work is explicit:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

Related Targets are independent entries, never nested object fields. A foreign
reference is qualified by that Target alias, for example
`bp::@variable-guid`. Normalized requests contain only unscoped StableRefs;
request text may use its own active Target alias as a redundant qualifier,
which the parser lowers away, but it rejects foreign or unknown qualifiers.

## Design Boundary

SAL deliberately does not:

- invent persistent ids when UE has none;
- route by object tag, native Class, Palette prefix, or operation name;
- retry a failed identity by display name;
- combine Domains because one UObject exposes several editor surfaces;
- hide a cross-Domain transition behind a resolver-like public concept;
- represent a Target as an ordinary object expression.

Legacy constructor and fused-reference syntax is accepted only when a caller
explicitly enables compatibility on the direct TypeScript parser during
protocol v3. MCP tools and the default SDK facade remain strict. Lowering is
limited to shapes that are complete and unambiguous in the active Domain;
unused Target declarations, explicit-v3/legacy Target mixtures, under-scoped
owner identities, target-self fused references, and forms that would require
UE-assisted recovery are rejected. Only `object(...)` may become an untagged
object. The protocol v4 Bridge rejects normalized legacy shapes and current
formatters emit only the explicit model above. Protocol v4 explicitly extends
the reader-only migration window because its Editor transport addition does
not change SAL syntax; removal requires a later incompatible protocol and
release note.
