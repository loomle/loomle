# SAL

SAL is Loomle's line-oriented language for reading and changing Unreal Engine
objects. It keeps UE names, native paths, enum values, property types, and
editor behavior intact.

## MCP Calls

- `status({})` reports Client, session, and Bridge health.
- `project({})` reports project binding and candidates.
- `project({ projectId: "<id>" })` binds the session to one project.
- `sal_query({ text })` executes one Query Text.
- `sal_patch({ text })` executes one ordered Patch Text.
- `sal_schema({})` lists the nine active Domain interface cards.
- `sal_schema({ module: "graph" })` returns one static card.
- `agent_skill({})` lists resident Loomle workflow Skills.
- `agent_skill({ name: "format-unreal-blueprints" })` loads one Skill before
  performing a matching specialized workflow.
- `editor({})` returns the user's current UE context as SAL Result Text.
- `editor({ operation: "open" | "close", target })` idempotently controls one
  exact Blueprint Editor or Graph document. `target` is one bare canonical SAL
  Target expression encoded as the JSON string value.
- `python({ operation: "run", script })` is the unrestricted Unreal Python
  fallback when no structured Loomle interface covers the task. The script
  defines synchronous `run()` and returns a JSON-compatible dictionary.
- If Python returns `status: "running"`, call `python` with the exact `poll`
  continuation it supplies. Never replay the original script.

The tool-call wrapper is not SAL syntax.

## Project Binding

One Loomle MCP session operates on one UE project. Start with `status({})`. If
the session is unbound, call `project({})`, copy one returned `projectId`, and
bind it with `project({ projectId: "<id>" })`. The binding is sticky: Loomle
does not fall through to another online project merely because the selected
project is offline or its Editor restarts. `sal_schema` remains available
without an online project because the resident guide and Domain cards are
local.

## Four Separate Concepts

```text
object data       = fields in {...}
semantic tag      = optional, erasable presentation metadata
Domain Target     = target { domain: ..., ... }
stable reference  = native identity path inside one exact Target
```

An ordinary object is always a brace expression:

```sal
{ id: "node-guid", type: "/Script/BlueprintGraph.K2Node_Event" }
node { id: "node-guid", type: "/Script/BlueprintGraph.K2Node_Event" }
```

`node` is an optional semantic tag. Removing it cannot change identity, type,
Domain, validation, creation, or execution. Object fields and context carry
the information that matters.

JSON literals, the retired generic label `object`, `target`, `domain`, the nine
protocol-v6 Domain keywords (`asset`, `blueprint`, `class`, `graph`,
`state_tree`, `widget`, `level`, `pcg`, and `pcg_component`), and `tree`,
`context`, and `palette` are reserved and cannot be semantic tags or aliases.

Parentheses are reserved for true calls or explicitly defined non-object
syntax, such as an invoked operation:

```sal
invoke @node-guid Rename(displayName: "New Name")
```

## Domain Targets

Every Query or Patch declares exactly one active Target:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

query g
summary
```

`target`, `domain`, and the Domain value are structural SAL keywords, not
semantic tags. A Target is flat: every non-`domain` field is a non-empty JSON
string, and a Target cannot contain another Target or alias.

The resident catalog has nine active Domain adapters and interface cards.
Query and Patch Target sets are deliberately separate:

| Domain | Query Target | Canonical Result | Patch Target |
| --- | --- | --- | --- |
| `asset` | `domain` only, or `path` with optional `type` | `path + type` | `path + type` |
| `blueprint` | `asset`, optional `id` | `asset + id` | `asset + id` |
| `class` | `path` | `path` | `path` |
| `graph` | `asset + id` or `asset + name`; optional `blueprintId` | `asset + blueprintId + id` | `asset + blueprintId + id` |
| `state_tree` | `asset`, optional `type` | `asset + type` | `asset + type` |
| `widget` | `asset`, optional `id` | `asset + id` | `asset + id` |
| `level` | `asset`, optional `type` | `asset + type` | `asset + type` |
| `pcg` | `asset`, optional `type` | `asset + type` | `asset + type` |
| `pcg_component` | `asset + actorId + source + id + type` | same exact fields | same exact fields |

The `level`, `pcg`, and `pcg_component` cards are public Query plus authored
Patch interfaces; `pcg_component` edits are gated by its fail-closed async
edit guard. A Domain's presence in the catalog does not grant Patch
admission: every Domain's Patch surface is exactly what its static card
defines. These canonical Query and Result identities are stable across Patch
expansions.

`type` is a native-Class assertion inside a selected Domain. It never selects
or adds a Domain. The same `UWidgetBlueprint`, for example, has separate
Blueprint and Widget Targets.

Every Target Guid is non-zero canonical lowercase text with hyphens, matching
UE `FGuid::IsValid()`.

## Stable References

Contained-object references are interpreted only inside the active exact
Target:

```sal
@node-guid
@node-guid/pin-guid
@container-guid/property-guid
```

The path contains native identity components, not kind words, collection
names, display names, or array positions. A Graph Pin includes its owning
NodeGuid even when its PinId happens to be unique. A semantic tag may decorate
the same reference:

```sal
node @node-guid
pin @node-guid/pin-guid
```

The tag is ignored for identity equality and native lookup. `@id`, `node @id`,
and another allowed tag on the same identity address the same native object;
their parsed ASTs may still retain different presentation metadata.

Member paths follow identity:

```sal
@node-guid/pin-guid.DefaultValue
```

The exact Target itself is structural:

```sal
query g
target
with schema
```

A Target may also be a relationship subject, but only where the Domain exposes
confirmed native declaration identity:

```sal
query functionGraph
references to target

query interfaceFunctionGraph
references to target.InterfaceGuid
```

It has no synthetic StableRef. Bare Target-self currently resolves only for
callable Function/Macro Graph Targets; the direct `target.InterfaceGuid`
member resolves for a valid Graph Interface declaration. Other Domain or Graph
roles return `capability.reference_unavailable`.

## Query

```sal
<alias> = target { domain: <domain>, ... }

query <alias>
[<one primary operation>]
[where <condition>]
[with <detail>, ...]
[order by <field> [asc|desc], ...]
[page limit <count>]
[page after "<cursor>"]
```

The operation-less form is the shared exact-target read and normalizes to
`target`. Contained exact reads use one StableRef:

```sal
query g
@node-guid
with schema
```

Plural operations enumerate or search. Singular names are discovery only;
copy the returned stable identity for later exact access. `references` finds
factual authored use-sites. Each Domain card closes the allowed operations,
clauses, fields, expansions, and pagination rules.

## Patch

```sal
<alias> = target { domain: <domain>, ... }

patch <alias> [dry run]
<binding or operation>
<binding or operation>
```

Core operations are:

```sal
add <binding> [to <destination>|before <anchor>|after <anchor>]
remove <object>
set <object>.<field> = <value>
reset <object>.<field>
move <domain-defined operands>
invoke <object> <Operation>(namedArguments) [as <alias>]
save
```

Domain cards add operations such as Graph `connect`, Widget `wrap`, StateTree
`bind`, and Blueprint or StateTree `compile`.

Every directly created object starts with a Palette result:

```sal
query g
palette entries "Print String"
```

Copy its object fields into the Patch:

```sal
patch g
print = { palette: "P_PrintString" }
add print
```

A formatter may emit `node { palette: "P_PrintString" }`, but the tag is
erasable. The active Domain, operation, Palette identity, and destination
provide the creation semantics. Never guess a Palette id, Class, field, Pin,
or operation parameter.

Patch statements execute in written order. Dry run follows the same parse,
resolve, validate, and plan path, then stops before live application.
Only Domains whose static card defines a Patch surface are Patch Targets;
`level` admits authored Actor and Component field edits, `pcg` admits
Palette-backed Node creation, Settings edits, movement, connection edits,
removal, and a terminal `save`, and `pcg_component` admits certified scalar
edits under its async edit guard.

## Results And Handoffs

Result text begins with its Target context and an explicit Target table:

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

The three result contexts are `exact_target`, `domain_root`, and
`unresolved_target`. Exact results always return the canonical Target after it
opens, including later operation failures. Unresolved results contain no
Target and at least one error diagnostic.

The first MCP text block is always the canonical, round-trippable Result Text.
If no Object Text exists, it ends with `no_objects`, which is a strict
terminator. Mutation metadata and diagnostics use later independent text
blocks formatted as SAL comments; they are not appended to Result Text and
cannot fabricate an `objects` section.

Cross-Domain navigation adds an independent related Target and explicit
handoff:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

A related Target is never nested in an object. A reference relative to it is
qualified as `bp::@identity`; unqualified references remain relative to the
main exact Target. That scoped form belongs to results. Request text may use
only its own active Target alias as a redundant qualifier, which the parser
lowers away; a foreign or unknown qualifier is rejected.

## Schema Discovery

Schema has three layers:

1. this resident guide;
2. one static Domain card from `sal_schema`;
3. exact, live `with schema` output for a Target, object, or Palette entry.

```sal
query g
target
with schema

query g
@node-guid
with schema
```

Exact schema is authoritative for fields, constraints, direct Patch
statements, instance availability, operations, parameters, outputs, and
copyable templates. Diagnostics should point back to static schema, an exact
schema query, a fresh collection/tree query, or an explicit Target handoff.

## Compatibility Window

Only callers that opt into the direct TypeScript parser's protocol-v3
compatibility mode may submit legacy object constructors, Domain Target
constructors, or fused kind references. MCP tools and the default SDK facade do
not enable it. The reader lowers only forms that are unambiguous in the active
Domain. Under-scoped owner identities, target-self fused references, ambiguous
or mixed Domains, and any form that would require UE-assisted recovery are
rejected. The protocol-v6 Bridge rejects normalized legacy shapes and current
formatters never emit them. This reader-only migration window remains a direct
parser compatibility option; removal requires a later incompatible protocol
and release note.
