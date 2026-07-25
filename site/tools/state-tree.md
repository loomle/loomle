---
layout: default
title: StateTree
parent: Interfaces
nav_order: 5
description: Inspect and edit authored StateTree hierarchy, bindings, and compile state.
permalink: /tools/state-tree.html
---

# StateTree

The StateTree interface reads and edits the authored contents of one UE
`UStateTree` asset through `UStateTree::EditorData`.

## Target

Bind the exact Asset Path and native Class Path:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}
```

Asset Path plus verified native Class is the complete StateTree Target.
`target`, `domain`, and `state_tree` are structural SAL keywords, not an object
expression or semantic tag. Contained identities are scoped inside this Target.

## Query

After the Target binding, a StateTree Query selects one of:

```sal
query omle

summary
tree [@state-guid] [depth N]
states ["text"]
nodes ["text"]
parameters ["text"]
@identity
references to <exact-object-or-member>
palette entries ["text"] to <exact-destination>
palette @id to <same-exact-destination>
```

The operation-less query shown at the top of the directory is the exact target
read.

Summary reports native Schema, Context Data, global Nodes, top-level States,
counts, compile status, and structural diagnostics. Tree preserves authored
hierarchy and owned-object order and defaults to depth 20.

Collection queries preserve authored order and use cursor pagination. Exact
reads return meaningful authored fields, owner navigation, and only the
Property Binding arrows incident to the selected object. Exact reads may
request dynamic schema.

## References and Relationships

State links and Property Bindings remain native relationships. Explicit
Bindings use authored data-flow direction and order. Automatic Context arrows
are returned with adjacent comments because they have no independent authored
record.

StateTree reference queries search only the bound StateTree and do not support
project-wide scope. They report each factual use-site with exact member-path
evidence.

Context Data are read-only. Property Functions are Binding-owned and are not
independent top-level lifecycle objects.

## Destination-bound Palette

Palette search always names the exact destination:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}

query omle
palette entries "Follow" to @companion-guid.Tasks
```

Inspect the selected entry against the same destination:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}

query omle
palette @P_OmleFollowTask to @companion-guid.Tasks
with schema
```

The returned `objects` section contains ordinary ObjectExpr fragments such as:

```sal
{ palette: "P_State", Name: Patrol }
{ palette: "P_OmleFollowTask" }
```

An allowed formatter may add an erasable tag such as `state {...}` or
`node {...}`. The tag does not choose lifecycle or type. Do not guess Palette
ids, destinations, fields, or member paths.

## Patch

Authored Patch supports:

```sal
add
set
reset
move
remove
bind
unbind
```

For example:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}

patch omle dry run

patrol = { palette: "P_State", Name: Patrol }
add patrol to @root-guid.Children
move @idle-guid before patrol

follow = { palette: "P_OmleFollowTask" }
add follow to patrol.Tasks
bind @container-guid/speed-guid -> follow.Instance.Speed
```

Every direct creation requires a Palette-backed local binding and an exact
destination. Exact schema defines writable native fields, ordered
destinations, Parameter layout, Binding compatibility, and cascade behavior.

StateTree currently advertises no invocation operation.

Dry run and apply share the same parse, resolution, validation, and planning
path. Authored mutation is atomic through transient native preflight followed
by one live UE transaction.

## Compile and Save

Finalization is a separate terminal Patch:

```sal
omle = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Omle.ST_Omle",
  type: "/Script/StateTreeModule.StateTree"
}

patch omle
compile
save
```

Valid terminal forms are compile-only, save-only, or compile followed by save.
Do not mix authored edits with finalization and do not compile after save.

Compile uses UE's native StateTree validation and compiler and is unavailable
during PIE. Save persists the owning Package but never implies compile.

Live execution, debugger traces, breakpoints, project-wide references, and
StateTree asset creation or deletion are outside the current interface.
