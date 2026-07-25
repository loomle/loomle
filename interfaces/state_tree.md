# state_tree

Inspect and edit the authored hierarchy of one UE `UStateTree` asset.

## Target

Discovery may omit `type`; canonical exact Queries and every Patch use:

```sal
behavior = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Behavior.ST_Behavior",
  type: "/Script/StateTreeModule.StateTree"
}
```

Asset Path plus verified native Class is complete StateTree Target identity.

## Identity

| Object | StableRef |
| --- | --- |
| State, Editor Node, Transition, valid unique Context descriptor | `@Guid` |
| Parameter | `@ContainerGuid/PropertyGuid` |

All one-segment categories share one Target-relative identity environment.
Names and semantic tags do not disambiguate collisions.

## Query

```sal
# Exact Target read.
query behavior
target

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

`summary` returns Schema, Context Data, global Nodes, top-level States, counts,
compile status, and structural diagnostics. `tree` preserves hierarchy and
owned-object order and defaults to depth 20.

Collections preserve authored order and use cursor pagination. Exact reads may
use `with schema` and return only directly incident explicit Property Bindings
and derived automatic Context relationships. StateTree references are local;
`in project` is not supported.

## Objects And Palette

All returned state is ordinary object data. Tags such as `state` or `node`, when
allowed and emitted, are optional presentation:

```sal
follow = {
  id: "cccccccc-cccc-cccc-cccc-cccccccccccc",
  type: "/Script/MyGame.FollowTask"
}
```

Palette is destination-bound:

```sal
query behavior
palette entries "Follow" to @companion-guid.Tasks

query behavior
palette @P_FollowTask to @companion-guid.Tasks
with schema
```

Copy the returned fields into an ordinary object binding. The active Domain,
Palette id, and exact destination provide creation meaning.

## Patch

```sal
patch behavior [dry run]

patrol = { palette: "P_State", Name: Patrol }
add patrol to @root-guid.Children

follow = { palette: "P_FollowTask" }
add follow to patrol.Tasks

move @idle-guid before patrol
set patrol.Weight = 1.5
remove @transition-guid

bind @container-guid/speed-guid ->
  @follow-task-guid.Instance.Speed
unbind @container-guid/old-guid ->
  @guard-guid.Instance.Threshold
```

Creation always requires a Palette-backed binding and exact native
destination. Exact schema controls writable fields, lifecycle, Parameter
layout and overrides, Binding direction and compatibility, and cascade
behavior.

A Property Function has Binding-owned lifecycle. Its result `bind` creates it;
removing that outer Binding removes the complete owned function subtree.
Context Data are read-only. StateTree currently exposes no `invoke`.

## Compile And Save

Finalization is an independent terminal Patch:

```sal
patch behavior
compile
save
```

Valid terminal forms are `compile`, `save`, or `compile` followed by `save`.
Do not mix authored edits with finalization. Save never implies compile. Save
failure does not roll back already completed in-memory edits or compile state.
