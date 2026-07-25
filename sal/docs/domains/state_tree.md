# StateTree Domain

## Scope

StateTree Domain exposes authored `UStateTree::EditorData`:

- Schema and read-only Context Data descriptors;
- ordered State hierarchy;
- Evaluators, Tasks, Conditions, Considerations, and Property Functions;
- Transitions and Required Events;
- Parameters;
- explicit Property Bindings and derived automatic Context relationships;
- destination-bound Palette;
- authored mutation, compile, and save.

It does not expose live execution instances, Rewind Debugger traces,
breakpoints, runtime instance data, or StateTree asset creation/deletion.

Plain Query traverses EditorData directly. It must not instantiate a
`FStateTreeViewModel` or call mutating validation helpers merely to make source
look valid.

## Target

StateTree has no persisted asset-level Guid:

```sal
behavior = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Behavior.ST_Behavior",
  type: "/Script/StateTreeModule.StateTree"
}
```

Discovery Query may omit `type`; canonical exact readback and every Patch
include the verified native Class.

The same native asset can have an independent Asset Target. Native Class does
not compose Asset and StateTree capabilities.

## UE Ownership

```text
UStateTree
├─ Schema
│  └─ Context Data descriptors
└─ EditorData
   ├─ Root Parameters
   ├─ Evaluators[]
   ├─ GlobalTasks[]
   ├─ EditorBindings[]
   └─ SubTrees[]
      └─ State
         ├─ Parameters
         ├─ EnterConditions[]
         ├─ Considerations[]
         ├─ Tasks[] or SingleTask
         ├─ Transitions[]
         │  └─ Conditions[]
         └─ Children[]
```

Compiled frames, compact states/nodes, runtime handles, copy batches, and
instance layouts are derived compiler output and are not editable authored
objects.

## Identity Environment

| Authored concept | Native identity path |
| --- | --- |
| State | `@UStateTreeState.ID` |
| Editor Node, including Property Function | `@FStateTreeEditorNode.ID` |
| Transition | `@FStateTreeTransition.ID` |
| valid unique Context descriptor | `@FStateTreeExternalDataDesc.ID` |
| Parameter | `@ContainerGuid/PropertyGuid` |

All one-segment categories share one combined identity audit. Equal Guid text
across a State and Node is a conflict even if an optional tag would look
different.

Parameter identity is owner-relative because Property Guids may be reused in
different containers. Names, display paths, roles, semantic tags, and array
positions are not identity.

A malformed Context descriptor with invalid or duplicate ID remains readable
as ordinary data with diagnostics but receives no StableRef.

Asset duplication regenerates authored structural ids and remaps internal
references. StableRefs from the source asset do not address the duplicate.

## Object Model

StateTree objects are ordinary brace expressions. Native owner relationship and
fields carry meaning:

```sal
root = {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: Root,
  Type: State,
  SelectionBehavior: TrySelectChildrenInOrder,
  bEnabled: true
}

root.Companion = {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: Companion,
  Type: Group
}

root.Companion.Tasks.follow = node {
  id: "cccccccc-cccc-cccc-cccc-cccccccccccc",
  type: "/Script/MyGame.FollowTask"
}
```

`node` is an optional erasable presentation tag. The same data without it has
identical behavior. StateTree does not create separate task, condition,
evaluator, consideration, context-data, event, delegate, or binding object
kinds.

Owner paths express role:

- `Evaluators`
- `GlobalTasks`
- `EnterConditions`
- `Tasks` or `SingleTask`
- `Considerations`
- `Transitions`
- Transition `Conditions`

The native selected struct or Blueprint Class remains `type`.

### State And Transition Fields

State fields preserve native names such as:

- `Name`, `Description`, `Tag`, `ColorRef`;
- `Type`, `SelectionBehavior`, `TasksCompletion`;
- `LinkedSubtree`, `LinkedAsset`;
- tick, event, prerequisite, weight, and enabled fields.

Lowercase `type` is the common native object type. Uppercase `Type` is UE's
`UStateTreeState::Type`.

Transition fields preserve `Trigger`, `RequiredEvent`, `State`, `Priority`,
delay fields, and `bTransitionEnabled`.

A concrete State link keeps the native relationship as a StableRef field:

```sal
{
  Name: SafetyRecovery,
  ID: @dddddddd-dddd-dddd-dddd-dddddddddddd,
  LinkType: GotoState,
  Fallback: None
}
```

Special native link kinds remain native enum semantics and do not create fake
State objects.

### Parameters

```sal
speed = {
  id: "eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee/ffffffff-ffff-ffff-ffff-ffffffffffff",
  type: "FloatProperty",
  Name: Speed,
  Value: 600.0,
  MetaData: [
    { Key: ClampMin, Value: "0" }
  ]
}
# owner: @bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb
# bFixedLayout: true
# value source: local override
```

The `id` data mirrors the StableRef path, but the StableRef itself is
`@container-guid/property-guid`. Descriptor fields and metadata retain native
names and order.

### Context Data

Context descriptors preserve `id`, `type`, `Name`, `Struct`, and
`Requirement`. Runtime `Handle` and value are not authored state. Context Data
are read-only; member binding schema comes from the descriptor's native
`Struct`.

## Ordering

Object Text preserves:

- top-level and child State order;
- Evaluator and Global Task order;
- Conditions, Tasks, Considerations, and Transition order;
- Transition Condition order;
- Parameter descriptor order;
- explicit Property Binding order.

These orders affect execution. Condition expressions remain UE's flat ordered
Nodes plus `ExpressionOperand` and `ExpressionIndent`; SAL does not translate
them into an AST.

## Query

```sal
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

`target with schema` reads the exact StateTree surface. `summary` returns
Schema, Context Data, global Nodes, top-level States, counts, compiled hash
orientation, and structural diagnostics without repair or compile.

`tree` defaults to depth 20. Depth counts State hierarchy edges only. A
truncated boundary State remains present with a comment.

Collections preserve authored order, use cursor pagination, and accept only
optional search text and page clauses. Page limit defaults to 50 and is capped
at 200. Collections accept no `where`, `order by`, `with schema`, or `depth`
clause. Property Functions are Binding-owned, so they are absent from `nodes`
but remain exact-readable.

Exact StableRef reads return meaningful authored fields, owner context, and
only directly incident relationships. They may use `with schema`.

Object and schema readback are fail-complete. Oversized exact values, excessive
reflected fields, or relationship-analysis budget failure return
`validation.result_too_large`, never partial state.

An exact Parameter native value is limited to 1 MiB. An exact Node surface is
limited to 2,048 reflected fields and 1 MiB of native value text across its
Node, Instance, and Execution Runtime Data surfaces. Crossing either bound
fails the complete Query; fields and values are never truncated.

StateTree references are local to the bound Target. `in project` is rejected
until a complete bounded index exists.

## Property Bindings

Explicit relationships live in
`FStateTreeEditorPropertyBindings::PropertyBindings`. Automatic Context
relationships are derived by UE for eligible unoverridden inputs.

Both render as Edges in actual data-flow direction:

```sal
@container-guid/speed-guid ->
  @follow-task-guid.Instance.AcceptanceRadius

@actor-context-guid ->
  @follow-task-guid.Instance.Actor
# automatic Context
```

There is no Binding object or Binding StableRef.

For ordinary explicit input Bindings, native TargetPath is the unique
replacement slot and renders `SourcePath -> TargetPath`. For effective Output
Bindings, native TargetPath is the logical producer and the rendered arrow is
reversed; several records may fan out from it.

The stored `bIsOutputBinding` bit is evidence, not authority. Query classifies
direction from the resolved root Property usage without mutating source. A
mismatch is diagnosed with native index and path evidence.

Endpoint member paths preserve native owner, Property Guid, polymorphic
instance type, and array index. Public examples include:

```sal
@node-guid.Instance.Points[0].Value
@container-guid/property-guid[0].X
```

`ExecutionRuntimeData` is not bindable.

Corrupt native paths that cannot map to one canonical owner/member path remain
diagnostic evidence; no false Edge is emitted.

### Required Events

Required Event descriptors are authored fields on State or Transition. Their
runtime Binding surface stays under that owner:

```sal
@state-guid.RequiredEventToEnter.Payload.Request ->
  @task-guid.Instance.Request

@transition-guid.RequiredEvent.Payload.Value ->
  @condition-guid.Instance.Value
```

State Event source requires `bHasRequiredEventToEnter`. Transition Event source
requires `Trigger == OnEvent`. Inactive stored Bindings remain visible with
diagnostics. Query never repairs them.

### Delegates

Dispatchers are Binding sources and Listeners are targets:

```sal
@producer-guid.Instance.OnFinished ->
  @consumer-guid.Instance.Listener

@producer-guid.Instance.OnFinished ->
  @transition-guid.DelegateListener
```

Runtime dispatcher/listener tokens are compiled data and never SAL identity.
A Transition Listener is active only while `Trigger == OnDelegate`; dormant
authored relationships remain readable and removable.

## Schema

Exact schema derives from:

- current `UStateTreeSchema` capabilities;
- non-mutating editor-schema capability hooks;
- native Reflection;
- Property usage and edit conditions;
- Binding visibility and compatibility;
- exact Palette candidate and destination.

It reports writable fields, lifecycle, destinations, native member paths,
Binding direction/types, Parameter layout and override rules, and Property
Function ownership. It does not claim arbitrary mutating `Validate()` logic is
statically enumerable.

Context Data schema is read-only. Property Function schema reports
Binding-owned lifecycle rather than independent add/remove/move.

Exact schema discovery is limited to 2,048 reflected fields and 1 MiB of schema
text across the requested surface. Crossing either bound returns
`validation.result_too_large` rather than an apparently complete schema with
omitted capabilities.

## Palette

Palette is always destination-bound:

```sal
query behavior
palette entries "Follow" to @companion-guid.Tasks

query behavior
palette @P_FollowTask to @companion-guid.Tasks
with schema
```

Results contain ordinary creation fields:

```sal
follow = { palette: "P_FollowTask" }
```

The exact destination participates in candidate discovery and revalidation.
Native type never implies a missing destination.

State and Transition candidates preserve UE-native defaults and constraints.
Linked State candidates fix an exact valid linked target. Linked Asset does
not scan assets. Parameter candidates choose a deterministic unique name
inside the destination bag.

Property Function Palette entries are consumed by their first result `bind`,
not `add`.

Blueprint StateTree Node discovery remains bounded: filter/page Asset Registry
candidates first, then load only Classes required for the returned page or
exact entry.

## Patch

StateTree supports:

- `add`
- `remove`
- `set`
- `reset`
- `move`
- `bind`
- `unbind`
- terminal `compile`
- terminal `save`

It currently exposes no `invoke`.

### Add

Every direct creation uses a Palette binding and exact destination:

```sal
patch behavior

newRoot = { palette: "P_State", Name: Root }
add newRoot to behavior.SubTrees

follow = { palette: "P_FollowTask" }
add follow to @companion-guid.Tasks

onFailure = { palette: "P_Transition", Trigger: OnStateFailed }
add onFailure to @companion-guid.Transitions

speed = { palette: "P_FloatParameter" }
add speed to @companion-guid.Parameters
```

Destinations are exact native roles returned by schema. `before` and `after`
may place a new object relative to a sibling in the same ordered destination.

### Set And Reset

```sal
set @companion-guid.Name = Companion
set @transition-guid.State = {
  Name: SafetyRecovery,
  ID: @safety-guid,
  LinkType: GotoState,
  Fallback: None
}
set @follow-task-guid.Instance.AcceptanceRadius = 150.0
reset @follow-task-guid.Instance.AcceptanceRadius
```

Changing State type, link, linked asset, or Parameters follows UE semantic
setters and reports cascades. Required Event activation and Payload changes may
make existing Bindings inactive or invalid without silently deleting them.

Editable local Parameter layouts may rename/change type/value/metadata.
Inherited fixed layouts allow only local value override `set` and `reset`.

### Move And Remove

```sal
move @idle-guid before @follow-guid
move @pickup-guid to @companion-guid.Children
move @container-guid/a-guid before @container-guid/b-guid

remove @transition-guid
remove @state-guid
```

Moving a State preserves its id, rejects cycles, and validates post-move
Binding visibility. State removal deletes its authored subtree and cleans
Bindings whose endpoints disappear. External Transition links to the removed
State remain invalid authored facts for diagnosis.

A Property Function is not independently removable or movable.

### Bind And Unbind

```sal
bind @container-guid/speed-guid ->
  @task-guid.Instance.AcceptanceRadius

unbind @container-guid/old-guid ->
  @guard-guid.Instance.Threshold
```

Operations use the same data-flow direction as readback. The adapter resolves
native direction, visibility, owner, path, and type compatibility.

`unbind` names the complete existing pair. Automatic Context arrows cannot be
unbound because no authored record exists. Removing an explicit override may
restore an automatic Context relationship, which the plan reports.

Node-targeted Binding changes call native `OnBindingChanged` and capture every
authored cascade. Unplanned changes fail closed.

### Property Functions

```sal
clamp = { palette: "P_ClampPropertyFunction" }
bind clamp.Instance.<schema-output-member> ->
  @task-guid.Instance.AcceptanceRadius
bind @container-guid/min-guid -> clamp.Instance.Min
```

The first result Binding materializes and owns the function Node. Unbinding
that outer result removes the complete nested Property Function subtree.

## Compile And Save

Finalization is independent:

```sal
patch behavior
compile
save
```

Valid forms are compile, save, or compile followed by save. Authored edits
cannot share the request. Save does not imply compile.

Compile uses `UStateTreeEditingSubsystem::CompileStateTree()`. Validation may
repair or remove invalid authored relationships. On native compile failure UE
clears invalid old compiled data and resets `LastCompiledEditorDataHash`; these
are planned native effects. Compiler errors are resulting state and may still
be followed by explicit save.

A save-only request may persist stale EditorData. Its result warns that
compiled data is out of date; `save` never performs an implicit compile.

Compile is rejected during PIE when native StateTree editing rejects it.
Save is external I/O; failure leaves completed in-memory edits or compile state
dirty and unsaved.

## Dry Run And Transactions

Preflight duplicates the complete StateTree, maps regenerated copy ids back to
source structural locations, executes the same ordered edit functions,
captures validation/compile cascades, and compares them with the plan.

Dry run stops before live apply. Live authored mutation and compile use one
top-level transaction. Save occurs afterward. An unplanned repair, id change,
link rewrite, or Binding removal fails closed.

## Adapter Boundary

The StateTree adapter owns native hierarchy, identity, Schema, Property Bag,
Binding, validation, compile, and save semantics. Core owns only structural
Target/ObjectExpr/StableRef/Query/Patch/result syntax.

No native Class, role name, semantic tag, or Palette prefix can switch the
request to another Domain.

### UE 5.7 Win64 ABI Compatibility

UE 5.7 omits `UE_API` from
`FStateTreeEditorPropertyBindings::AddBindingInternal`. An external Win64
plugin that materializes that vtable therefore cannot link against an
Installed Build even when it never calls the member directly.

The compatibility unit supplies the exact UE 5.7 member definition: append an
`FStateTreePropertyPathBinding` from the source and target paths with
`bIsOutputBinding = false`, then return the appended record through the base
binding pointer. Ordinary Bindings still use `AddBinding`; effective output
Bindings still use exported `AddOutputBinding`.

This is Windows-only ABI completion, not another Binding model. It must remain
source-identical to the supported UE implementation and be removed once the
supported engine exports the member. Acceptance requires the plugin to link
against the UE 5.7.4 Win64 Installed Build and the complete StateTree mutation
Automation category to pass.
