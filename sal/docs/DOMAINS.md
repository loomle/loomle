# SAL Domains

## Definition

A Domain is SAL's adapter for one coherent UE semantic surface. It owns:

- one flat Target grammar;
- one Target-relative identity environment;
- Query, Patch, Palette, schema, and diagnostics;
- canonical object formatting and recommended erasable tags;
- native resolution, validation, planning, application, and handoffs.

There is no public resolver, locator layer, interface composition, or
capability-discovery step between Target and Domain. Routing begins and ends
with `Target.domain`.

## Protocol Domain Set

The resident public catalog has nine active Domain adapters. The final three
are deliberately Query-only in protocol v6:

| Domain | Target identity | Primary scope |
| --- | --- | --- |
| `asset` | Asset Path plus verified native Class | Asset Registry discovery and generic save |
| `blueprint` | Asset Path plus `BlueprintGuid` | Blueprint declarations, settings, SCS, compile/save |
| `class` | exact Class Path | Reflection and supported Generated Class Defaults |
| `graph` | Blueprint asset, `BlueprintGuid`, `GraphGuid` | one Graph, Nodes, Pins, Edges, Graph Palette |
| `state_tree` | Asset Path plus verified StateTree Class | authored StateTree hierarchy and bindings |
| `widget` | WidgetBlueprint asset plus `BlueprintGuid` | WidgetTree, Widgets, Slots, Widget Palette |
| `level` | source-map Asset Path plus verified native Class | read-only authored Actor and Component inspection |
| `pcg` | PCG Graph Asset Path plus verified native Class | read-only PCG Graph, Node, Pin, and Edge inspection |
| `pcg_component` | Level asset, Actor Guid, source-aware Component locator, and verified native Class | read-only Graph binding and effective Parameter inspection |

All nine Domain names are structural keywords. They cannot be semantic tags,
local aliases, or unquoted SAL Names. `level`, `pcg`, and `pcg_component` are
public Query surfaces but are absent from `PatchTarget`; their mutation and
save contracts require a later protocol/capability release.

## Target Grammar

```text
target {
  domain: <one of the nine Domain keywords>,
  <Domain-defined field>: <non-empty JSON string>,
  ...
}
```

Each Domain closes its field set:

| Domain | Accepted fields |
| --- | --- |
| Asset | no fields for collection root; otherwise `path`, optional `type` |
| Blueprint | `asset`, optional `id` for discovery |
| Class | `path` |
| Graph | `asset`, `id` or exact `name`, optional `blueprintId` for discovery |
| StateTree | `asset`, optional `type` for discovery |
| Widget | `asset`, optional `id` for discovery |
| Level | `asset`, optional `type` for discovery |
| PCG | `asset`, optional `type` for discovery |
| PCG Component | exact `asset`, `actorId`, `source`, `id`, and `type` |

Patch requires the canonical exact form:

- Asset: `path + type`
- Blueprint: `asset + id`
- Class: `path`
- Graph: `asset + blueprintId + id`
- StateTree: `asset + type`
- Widget: `asset + id`

`level`, `pcg`, and `pcg_component` are admitted for Query and canonical
Result shapes only. Patch rejects all three before adapter dispatch.
`pcg_component.source` is closed to `native`, `instance`, or `scs`.

Target fields are scalar address or verification facts. They cannot contain an
alias, ObjectExpr, array, or another Target.

Guid-valued Target fields are non-zero, canonical lowercase text with hyphens
and must satisfy UE `FGuid::IsValid()`.

## Domain Selection

Selection order is fixed:

1. parse the Target;
2. read `Target.domain`;
3. validate that Domain's closed Target fields;
4. open and canonicalize the native object;
5. interpret the request through that Domain;
6. resolve StableRefs inside that exact Target's identity environment.

No later information changes the selected Domain:

- a native UE Class may validate but never route;
- an object `type` field is data;
- a semantic tag is presentation;
- a Palette id is an opaque capability identity;
- an operation name belongs to the already selected Domain.

## One UObject, Multiple Targets

One native object can support several independent Domains:

```sal
menuBlueprint = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

menuWidgets = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The two Targets have separate operations, Palettes, identity environments, and
mutation authority. A request never gains both merely because the loaded
object is a `UWidgetBlueprint`.

A `UStateTree` similarly has an Asset Target for generic asset behavior and a
StateTree Target for authored StateTree behavior.

## Graph Is Not A Nested Domain

Graph Target identity is flat:

```sal
g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

Top-level and child/collapsed Graphs use the same Target. `GraphGuid` is scoped
by the owning Blueprint verification fields. A duplicate GraphGuid inside that
Blueprint is an identity conflict, not a reason to invent a nested Graph
Domain or a `graphs/...` path.

## Identity Environments

| Domain | One-segment StableRefs | Owner-relative StableRefs |
| --- | --- | --- |
| Asset | none | none |
| Blueprint | Variables, Dispatchers, Graphs, SCS Components, referenceable Nodes | Function local: `@GraphGuid/VarGuid` |
| Class | none currently | none |
| Graph | Nodes and declared owning-Blueprint identities allowed by Graph | Pin: `@NodeGuid/PinId`; outer function local when needed |
| StateTree | States, Editor Nodes, Transitions, valid Context descriptors | Parameter: `@ContainerGuid/PropertyGuid` |
| Widget | authored Widgets | none |

All categories sharing a path shape are audited together. A tag cannot rescue
a collision.

## Operation Ownership

| Domain | Owns | Explicit handoff |
| --- | --- | --- |
| Asset | Registry search, exact Asset read, generic save | another asset-backed Domain |
| Blueprint | settings, Variables, Dispatchers, top-level Graph lifecycle, SCS, compile/save | Graph, Widget, Class |
| Class | Reflection and supported Defaults/save | Blueprint compile |
| Graph | Graph body, Nodes, Pins, Edges, Graph Palette | Blueprint compile/save |
| StateTree | hierarchy, bindings, Palette, compile/save | only explicit related targets |
| Widget | WidgetTree, Widgets, Slots, Widget Palette | Graph events; Blueprint compile/save |

Resolving an identity does not grant mutation authority. If the desired
operation belongs to another Domain, the result returns a related canonical
Target and handoff. It never silently switches adapters.

## Cross-Domain Result

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

The handoff points to the related Target alias. It does not embed another
Target, infer it from an object, or extend the current Target's capabilities.
