# Blueprint Adapter

> Legacy LGL implementation record. This document describes the existing
> pre-SAL Bridge and is not the SAL interface contract.

## Intent

The Blueprint adapter executes LGL object queries and patches against Unreal
Blueprint assets. It handles both Blueprint class targets and graph targets
whose `target.domain` is `blueprint`; it is not a generic graph adapter.

The adapter must preserve UE semantics. It should use UE APIs, Blueprint graph
schemas, action databases, node spawners, transactions, and reconstruction
paths as the source of truth. Existing Loomle Blueprint tool code may be used as
reference material, but production LGL adapter code should not call old public
inspect or edit tool handlers.

## Boundary

The TypeScript SDK accepts LGL text. The UE bridge accepts normalized LGL Object
JSON.

```txt
LGL text
  -> TypeScript parser/normalizer
  -> Query or Patch object JSON
  -> lgl.query / lgl.patch
  -> Blueprint adapter
  -> UE Blueprint APIs
```

The Blueprint adapter receives decoded `Query` and `Patch` objects from the
bridge core. It does not parse LGL text.

## Responsibilities

The adapter owns Blueprint-specific semantics:

- resolve Blueprint assets
- resolve Blueprint graph references
- read Blueprint graph nodes, pins, defaults, edges, and layout metadata
- query Blueprint palette/action entries
- resolve stable palette entry ids for node creation
- validate node, field, pin, direction, and link references
- validate graph-state-dependent operations such as `insert`
- apply graph edits through UE editor transactions
- mark assets dirty and trigger reconstruction when UE requires it
- map UE failures into LGL diagnostics

Shared graph behavior should live in reusable services when it can also serve
Material, PCG, Niagara, Control Rig, or WidgetBlueprint later.

## Query

First supported query scope is graph readback for a `GraphTarget` whose
`target.domain` is `blueprint`:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

```txt
find nodes where name = branch
with pins, defaults
```

Expected result shape:

- compact `graph` object
- stable node ids from `NodeGuid` when available
- readable aliases that are unique within the returned snippet
- normalized output-to-input edges
- pins only when requested
- defaults only when requested
- layout metadata through `at` and non-zero `size` fields when requested

Ambiguous readable node matches must return `ambiguous_node` with candidates
rather than guessing.

## Palette

Palette is a graph editing concept exposed through LGL query and patch objects,
not a separate SDK method.

Discovery happens through query:

```txt
find palette entry "Print String"
```

Patch-time creation should use stable palette entry ids:

```txt
PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")
```

Context-sensitive entries must carry enough identity to reconstruct the UE
action context.

## Patch

Patch support should follow the mutation dry-run contract: parse, resolve,
validate, and plan through the same path for dry run and real mutation, then
stop before applying when dry run is requested.

Normal node creation must go through UE palette/action spawners. LGL should not
invent Blueprint node instances directly.

Initial graph patch operations should map to UE graph edits through Blueprint
graph services when the patch target is a `GraphTarget` whose `target.domain`
is `blueprint`:

- `add`
- `connect`
- `disconnect`
- `insert`
- `remove`
- `set`
- `move`

Blueprint class/member patch operations use the schema's Blueprint patch ops
instead of graph patch ops.

`reconstruct` exists in the schema as a graph maintenance operation, but it is
not part of the first patch milestone unless a supported edit proves it needs a
manual escape hatch.

Reconstruction should usually be automatic adapter maintenance after supported
operations. A manual reconstruction operation should remain an escape hatch, not
the normal editing path.

## Layout

Layout readback starts with UE model data:

- `UEdGraphNode::NodePosX`
- `UEdGraphNode::NodePosY`
- comment node width and height when relevant

Pin anchors require measured editor geometry and should not be estimated in
asset readback.

Patch layout mutation starts with node movement:

```txt
move delay to (320, 0)
move print by (240, 0)
```

Comment boxes and reroute nodes need separate design before they become part of
the stable Blueprint adapter contract.
