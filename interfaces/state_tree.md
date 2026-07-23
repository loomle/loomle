# state_tree

Inspect and edit the authored hierarchy of one UE `UStateTree` asset. This
interface reads `UStateTree::EditorData` directly and assumes the resident SAL
Core guide.

## Target

Bind the exact Asset Path and native Class Path:

```sal
omle = asset(
  path: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
)
```

The Asset Path is the complete target identity. StateTree has no asset-level
Guid and no `state_tree(...)` constructor. Every typed id is scoped inside this
target and cannot replace it.

## Query

Every Query starts with `query omle` and chooses at most one primary operation:

```sal
# Exact target read.
query omle

summary
tree [state@id] [depth N]
states ["text"]
nodes ["text"]
parameters ["text"]
state@id
node@id
transition@id
parameter@container-id/property-id
object@context-id
references to <exact-object-or-member>
palette entries ["text"] to <exact-destination>
palette @id to <same-exact-destination>
```

`summary` returns orientation: native Schema, Context Data, global Nodes,
top-level States, counts, compile status, and structural diagnostics. `tree`
preserves authored hierarchy and owned-object order, defaults to depth 20, and
keeps a boundary State when deeper children are truncated.

The three collections preserve authored order and use cursor pagination with a
default limit of 50 and maximum of 200. They accept optional search text and
`page`, but no `where`, `order by`, `with schema`, or `depth`. Context Data have
no collection; discover them in `summary`. Property Functions are
Binding-owned, so they are absent from `nodes`, but a known function `node@id`
is exact-readable.

Exact reads return meaningful authored fields, compact owner navigation, and
only the Property Binding arrows directly incident to the selected object.
They may append `with schema`; `summary`, `tree`, collections, and references
may not. Schema is derived from UE Reflection, the current StateTree Schema,
Property Binding rules, and the exact object or Palette destination. Context
Data are read-only.

`references` is local to the bound StateTree, accepts only cursor `page`
clauses, and returns each factual use-site once with exact member-path evidence.
It covers State links and explicit or derived Property Binding relationships.
It never loads other assets and does not support `in project`.

Query preserves malformed authored facts as diagnostics and never validates,
repairs, compiles, dirties, or saves the asset. Names are navigation text, not
identity; copy returned `state@id`, `node@id`, `transition@id`, composite
`parameter@container-id/property-id`, and `object@context-id` references.

## Palette And Object Relationships

Palette is always destination-bound. Search first, then read the exact entry
against the same destination when its schema is needed:

```sal
query omle
palette entries "Follow" to state@companion-guid.Tasks

query omle
palette @P_OmleFollowTask to state@companion-guid.Tasks
with schema
```

Copy the returned `state(...)`, `node(...)`, `transition(...)`, or
`parameter(...)` constructor into Patch Text. Do not guess Palette ids,
destinations, native fields, or member paths. Property Function candidates are
materialized by their first owning `bind`, not by `add`. Linked State entries
already contain their exact valid `LinkedSubtree`; LinkedAsset uses a separate
fixed-type entry without scanning assets. Parameter names are chosen against
the exact destination. Patch revalidates these capabilities.

Arrows use real data-flow direction. Explicit Bindings preserve UE authored
order. Automatic Context arrows are marked by adjacent comments and have no
authored record; they can be queried but not independently removed.

## Patch

Authored Patch supports Palette-backed `add`, plus `set`, `reset`, `move`,
`remove`, `bind`, and `unbind`:

```sal
patch omle

patrol = state(palette: "P_State", Name: Patrol)
add patrol to state@root-guid.Children
move state@idle-guid before patrol
set patrol.Weight = 1.5

follow = node(palette: "P_OmleFollowTask")
add follow to patrol.Tasks
bind parameter@container-guid/speed-guid -> follow.Instance.Speed
```

Every direct creation requires a Palette-backed local binding and an exact
destination. Exact `with schema` is authoritative for writable native fields,
ordered destinations, Parameter layout and overrides, Binding direction and
compatibility, and cascade behavior. A Property Function has Binding-owned
lifecycle: its owning result `bind` creates it, and removing that Binding
removes its complete owned subtree. StateTree currently advertises no
`invoke` operation.

Patch is ordered and atomic through transient native preflight followed by one
live UE transaction. `dry run` uses the same parse, resolution, validation, and
planning path without changing live authored or compiled state. Results use
ordinary Object Text plus the shared mutation plan, resolved references, diff,
and registered diagnostics.

## Compile And Save

Finalization is an independent Patch containing only `compile`, only `save`, or
`compile` followed by `save`:

```sal
patch omle
compile
save
```

Do not mix authored edits with finalization and do not place `compile` after
`save`. `save` never implies compile; save-only may persist stale compiled
data. Compile uses UE's native StateTree validation and compiler, is rejected
during PIE, and may mutate compiled state even when compiler diagnostics report
failure. A completed compile can therefore continue to an explicit `save`.

Save is external package I/O after the in-memory transaction. If it fails,
already completed edits or compile state remain in memory and dirty but
unsaved; save failure does not roll them back.

Live execution, debugger traces, breakpoints, project-wide references, and
StateTree asset creation or deletion are outside this interface.
