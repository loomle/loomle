# StateTree Domain

## Scope

The StateTree domain describes the authored state hierarchy stored by one UE
`UStateTree` asset. It covers StateTree orientation, ordered State hierarchy,
StateTree Editor Nodes, Transitions, Parameters, Context Data, Property
Bindings, Schema-driven discovery, Palette-backed creation, authored edits,
compilation, diagnostics, and Package save.

This document records the confirmed authored-language contract. The StateTree
interface card, SDK schema/executor support, and UE Bridge adapter are not
active yet; they must implement this contract without introducing another
public object model or creation path.

The first version is intentionally an authored-asset domain. It does not expose
live execution instances, Rewind Debugger traces, runtime instance data,
breakpoints, or execution control. It also does not create, rename, move,
duplicate, or delete the StateTree asset itself; those are future Asset Tools
capabilities.

## UE Source Boundary

The canonical authored source is `UStateTree::EditorData`, whose concrete type
is normally `UStateTreeEditorData`. Its important ownership shape is:

```text
UStateTree asset
├─ Schema
│  └─ Context Data descriptors
└─ EditorData
   ├─ Root Parameters
   ├─ Evaluators[]
   ├─ GlobalTasks[]
   ├─ EditorBindings[] explicit relationships
   └─ SubTrees[]
      └─ UStateTreeState
         ├─ Parameters
         ├─ EnterConditions[]
         ├─ Considerations[]
         ├─ Tasks[] or SingleTask
         ├─ Transitions[]
         │  └─ Conditions[]
         └─ Children[]
```

Schema Context Data are read-only, Schema-declared descriptors visible through
the target for runtime values that the execution owner supplies. Explicit
Property Bindings are target-owned relationships stored in `EditorBindings`.
For a Context-usage Property without an explicit Binding, UE may instead derive
an automatic Context relationship through `FindContextData()`; that derived
relationship is not stored in `EditorBindings`. A Property Function is stored
by its owning function-result Binding as an `FStateTreeEditorNode`; SAL returns
it as an ordinary `node(...)` with Binding-owned lifecycle.

`UStateTree` also stores compiled Frames, compact States, Nodes, Transitions,
default instance data, linked external data, Property Binding batches, and
runtime id-to-index maps. Those are derived compiler output, not a second
authored object model. SAL may use compiled status and native id maps for
diagnostics or later runtime navigation, but it must not return compiled
layout as editable authored objects.

Plain reads traverse `EditorData` directly. They must not create a
`FStateTreeViewModel` or call `UStateTreeEditingSubsystem::ValidateStateTree()`.
Both paths may mutate the asset by repairing links, replacing editor data,
updating linked parameters, removing invalid Bindings, or deleting content the
current Schema no longer permits. A Query never performs those repairs.

## Target And Identity

A StateTree has no persisted asset-level Guid. Its exact native identity is its
Asset Path, so the existing `asset(...)` binding is already the complete
StateTree target:

```sal
omle = asset(
  path: "/Game/Omle/ST_OmleLocalBehavior.ST_OmleLocalBehavior",
  type: "/Script/StateTreeModule.StateTree"
)

query omle
summary
```

The adapter loads the exact Path, verifies the resolved native Class, and then
composes StateTree capabilities onto that Asset target. SAL does not add a
`state_tree(...)` wrapper, an asset Guid, or a public `domain` selector. Asset
discovery may return `state_tree` in the Asset's `domains` capability hints,
but the hint never overrides the loaded Class.

The following native identities are scoped to the resolved StateTree asset:

| Authored concept | Native identity | Public handling |
| --- | --- | --- |
| StateTree asset | Asset Path | exact `asset(...)` target |
| State | `UStateTreeState::ID` | `state@id` |
| StateTree Editor Node | `FStateTreeEditorNode::ID` | `node@id` |
| Transition | `FStateTreeTransition::ID` | `transition@id` |
| Parameter | container Guid plus `FPropertyBagPropertyDesc::ID` | `parameter@container-id/property-id` |
| Schema Context Data | valid, unique `FStateTreeExternalDataDesc::ID` | read-only `object@id` Binding source |
| Property Function | `FStateTreeEditorNode::ID` | exact `node@id`; owning Binding defines lifecycle |
| Explicit Property Binding | no id | target-owned mutable relationship |
| Automatic Context relationship | no id and no authored record | derived read-only data flow |

State, Node, and Transition names are not unique. Display paths such as
`UStateTreeState::GetPath()` are navigation text rather than identity. Exact
cross-request access therefore uses the Asset target plus the object's
canonical stable reference. It never falls back to a display name after an id
lookup fails. Parameter identity is deliberately composite because linked
State Parameter bags may reuse a property Guid under different containers.
The `/` in `parameter@container-id/property-id` is the canonical text encoding
of those two native identity components; it is not a global synthetic id.

A Schema Context descriptor becomes `object(...)` only when its ID is valid and
unique within the resolved target. A malformed Schema may still return a
descriptor with an invalid or duplicate ID because UE descriptor validity only
requires a native `Struct`. SAL preserves that fact in comments and diagnostics
but does not invent a stable reference for it.

Asset duplication regenerates State, Node, Transition, Parameter-container,
Property Function, and valid Color ids while remapping internal references.
Those authored structural references are consequently invalid in the duplicate
even when the visible hierarchy is identical. Schema Context IDs belong to the
Schema declaration and may remain the same, but `object@id` is still scoped to
the newly bound duplicate target rather than acting as a global address.

## Public Object Model

The primary StateTree object shapes are deliberately small:

| Object shape | UE source | Meaning |
| --- | --- | --- |
| `state(...)` | `UStateTreeState` | one authored State and its hierarchy placement |
| `node(...)` | `FStateTreeEditorNode` | one Evaluator, Task, Condition, Consideration, or Property Function |
| `transition(...)` | `FStateTreeTransition` | one ordered Transition owned by a State |
| `parameter(...)` | one Property Bag descriptor and value | one ordered root or State Parameter |
| `object(...)` | `FStateTreeExternalDataDesc` | one valid, uniquely identified read-only Schema Context Data slot |

`node(...)` is shared with the Graph domain only as a small SAL structural
constructor. It does not claim that `FStateTreeEditorNode` is a
`UEdGraphNode`, and it does not grant Pins, Edges, Graph Schema, `with pins`,
or Graph Patch operations. The exact target, ownership path, native `type`, and
`with schema` result determine the object's actual capabilities.

StateTree does not add separate `task(...)`, `condition(...)`,
`evaluator(...)`, or `consideration(...)` constructors. Those would translate
one UE storage shape into a parallel SAL role type system. The native owner
relationship already states the role:

- `Evaluators`
- `GlobalTasks`
- `EnterConditions`
- `Tasks` or `SingleTask`
- `Considerations`
- Transition `Conditions`

The actual selected Node type remains the complete native UScriptStruct or
Blueprint Class Path in `type`. For Blueprint-authored StateTree Nodes, UE's
wrapper struct and object instance are persistence details; `type` identifies
the Blueprint Class the user actually selected.

One `FStateTreeEditorNode` internally contains Node template data,
Instance/InstanceObject data, optional ExecutionRuntimeData/Object data,
ExpressionIndent, and ExpressionOperand. These remain parts of one public
`node(...)` object. Deterministic internal ids such as
`FStateTreeEditorNode::GetNodeID()` are Binding implementation details rather
than additional public objects.

Property Functions use the same `node(...)` shape because UE stores them as
StateTree Editor Nodes. They remain owned by their function-result Binding: they
are exact-readable and schema-discoverable through `node@id`, but cannot be
added, removed, or moved independently. Creating the first outer Binding
creates the function Node; removing that Binding removes its complete function
subtree.

Context Data use the literal generic `object(...)` shape because they are
stable, exact-readable binding sources when their native ID is valid and unique,
but do not justify a fifth StateTree-specific role word. Their exact reference
is literally `object@id`. State, Node, Transition, and Parameter retain their
concise canonical kinds and must never also resolve through `object@id`.

StateTree adds no `task(...)`, `condition(...)`, `evaluator(...)`,
`consideration(...)`, `property_function(...)`, `context_data(...)`,
`event(...)`, `delegate(...)`, `dispatcher(...)`, `listener(...)`, or
`binding(...)` constructor. Required Events, State Links, Delegate endpoints,
Node template data, Node instance data, and compiled runtime data likewise do
not become independent lifecycle objects merely because UE stores a struct for
them.

## Canonical Object Text

StateTree Object Text is one ordered statement sequence. Member binding paths
express ownership and the sequence of statements expresses authored order:

```sal
omle = asset(
  path: "/Game/Omle/ST_OmleLocalBehavior.ST_OmleLocalBehavior",
  type: "/Script/StateTreeModule.StateTree"
)

# Schema Context Data
actorContext = object(
  id: "actor-context-guid",
  type: "/Script/StateTreeModule.StateTreeExternalDataDesc",
  Name: Actor,
  Struct: "/Script/Engine.Actor",
  Requirement: Required
)

omle.GlobalTasks.observe = node(
  id: "global-task-guid",
  type: "/Script/ProjectOdyssey.OmleObserveTask"
)

omle.RootParameters.moveSpeed = parameter(
  id: "root-parameters-guid/move-speed-guid",
  type: "<schema-returned native Property Bag type>"
)

root = state(
  id: "root-guid",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: Root,
  Type: State,
  SelectionBehavior: TrySelectChildrenInOrder,
  bEnabled: true
)

root.SafetyRecovery = state(
  id: "safety-guid",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: SafetyRecovery,
  Type: State
)

root.ExternalActionControl = state(
  id: "external-guid",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: ExternalActionControl,
  Type: State
)

root.ExternalActionControl.EnterConditions.externalControl = node(
  id: "condition-guid",
  type: "/Script/ProjectOdyssey.OmleExternalControlCondition"
)

root.Companion = state(
  id: "companion-guid",
  type: "/Script/StateTreeEditorModule.StateTreeState",
  Name: Companion,
  Type: Group
)

root.Companion.Tasks.follow = node(
  id: "follow-task-guid",
  type: "/Script/ProjectOdyssey.OmleFollowTask"
)

root.Companion.Transitions.onFailure = transition(
  id: "transition-guid",
  type: "/Script/StateTreeEditorModule.StateTreeTransition",
  Trigger: OnStateFailed,
  State: {
    Name: SafetyRecovery,
    ID: state@safety-guid,
    LinkType: GotoState,
    Fallback: None
  },
  Priority: High,
  bTransitionEnabled: true
)

object@actor-context-guid -> node@follow-task-guid.Instance.Actor
parameter@root-parameters-guid/move-speed-guid -> node@follow-task-guid.Instance.Speed
```

The example native Node Class Paths and compact Parameter type are
illustrative; adapters return exact schema-owned native text and never
synthesize it from display labels. Exact Parameter reads also return its
meaningful native descriptor fields, value, metadata, and owner context.

Top-level State order follows `UStateTreeEditorData::SubTrees`. Child State
order follows `UStateTreeState::Children`. The adapter must also preserve the
order of Evaluators, Global Tasks, Enter Conditions, Tasks, Considerations,
Transitions, Transition Conditions, and Property Bag descriptors. These orders
can change State selection, task execution, equal-priority Transition choice,
Binding visibility, or expression evaluation.

Condition and Consideration expressions remain UE's ordered flat Nodes plus
native `ExpressionOperand` and `ExpressionIndent`. SAL must not translate them
into an expression AST. Similarly, hierarchy comes from `SubTrees` and
`Children`; `Parent` and UObject Outer are redundant implementation state and
are not independently emitted.

Document-local aliases are readability aids. The adapter sanitizes and
uniquifies them without treating the alias as a State or Node name. Later
requests bind the exact Asset again and use `state@id`, `node@id`,
`transition@id`, `parameter@container-id/property-id`, or `object@id`.

## Native Fields

`id` and `type` retain their shared SAL structural meaning. All remaining
authored fields keep native UE names and values. State examples include:

- `Name`, `Description`, `Tag`, `ColorRef`
- `Type`, `SelectionBehavior`, `TasksCompletion`
- `LinkedSubtree`, `LinkedAsset`
- `bHasCustomTickRate`, `CustomTickRate`
- `Parameters`
- required-enter-event and prerequisite fields
- `Weight`, `bEnabled`

Lowercase `type` is the shared native object/type text field. Uppercase `Type`
is UE's own `UStateTreeState::Type` field; preserving both spellings avoids a
SAL translation of the native State enum.

Transition examples include:

- `Trigger`, `RequiredEvent`, `State`, `Priority`
- `bDelayTransition`, `DelayDuration`, `DelayRandomVariance`
- `bTransitionEnabled`

A Required Event remains an authored field of its owner. A State stores
`bHasRequiredEventToEnter` and `RequiredEventToEnter`; a Transition stores
`RequiredEvent`, which participates in execution only while `Trigger` is
`OnEvent`. Both descriptors preserve the native `Tag`, `PayloadStruct`, and
`bConsumeEventOnSelect` fields. The transient `FStateTreeEvent` that UE builds
for Property Binding is not emitted as another object, and its deterministic
owner-derived Struct ID is not public identity.

Delegate Dispatcher and Listener members are authored endpoint surfaces, but
their internal values are compiled-only data. The runtime Guid and integer
Listener id are generated during compilation. Object Text preserves the
authored relationship as an arrow between the native owner/member paths and
excludes those compiled tokens.

Node text preserves reflected authored values from the selected native Node,
Instance, and Execution Runtime Data surfaces. Exact `with schema` identifies
the native owning surface, Property type, read/write/reset availability,
Property usage, Binding support, and any edit condition. SAL does not rename
input fields, translate native enums, or maintain a StateTree-specific type
system.

Parameter text combines one ordered Property Bag descriptor with its current
value and metadata. Its `id` is the same `container-id/property-id` pair used
by its stable reference. Exact `with schema` supplies the native member paths
for name, type, value, metadata, override state, and every allowed edit; SAL
does not freeze those paths into a second Parameter schema.

Context Data `object(...)` text maps `id` to a valid, unique
`FStateTreeExternalDataDesc::ID` and preserves the native `Name`, `Struct`, and
`Requirement` fields. `Handle` is compiled/runtime indexing state and is not
authored text. Context Data descriptors are read-only. A member after
`object@id` resolves against the descriptor's `Struct` Binding surface, not
against the descriptor metadata. Invalid or duplicate descriptor IDs remain
visible as adjacent diagnostic comments rather than false `object@id` values.

An Object Text arrow renders actual data-flow direction. It may represent an
explicit authored Property Binding or UE's derived automatic Context
resolution; an adjacent comment identifies the latter. The arrow remains a
relationship statement, not a `binding(...)` object. For a native UE output
Binding, the adapter reverses the storage-oriented path pair as needed so the
SAL arrow still points from producer to consumer.

`State` on a Transition preserves the native `FStateTreeStateLink` fields. For
a concrete Goto State, its native `ID` value is rendered as a stable
`state@id` relationship while `Name`, `LinkType`, and `Fallback` remain beside
it. UE special targets such as Succeeded, Failed, Next State, and Next
Selectable State retain their native `LinkType` semantics rather than
receiving artificial State objects.

Fields derived solely from compiled layout, transient breakpoints,
`bExpanded`, cached State handles, Property copy batches, and runtime indices
are excluded from authored Object Text. A malformed source asset is reported
as found; Query does not discard invalid Children, Nodes, links, or Bindings in
order to make the result look valid.

## Summary

StateTree orientation uses the shared `summary` operation:

```sal
query omle
summary
```

Summary returns:

1. the compact exact Asset binding;
2. the native StateTree Schema Class Path;
3. every valid, uniquely identified Schema Context Data slot as a compact
   `object(...)` binding, in Schema order;
4. every Evaluator and Global Task as compact `node(...)` bindings, in authored
   order;
5. every top-level State as a compact `state(...)` binding, in authored order;
6. compact comments containing authored counts;
7. compile orientation derived from the current editor-data hash,
   `LastCompiledEditorDataHash`, and `IsReadyToRun()`;
8. structural diagnostics that can be derived from current authored and
   compiled status without mutating, validating, or compiling the asset.

Useful counts include all States, Evaluators, Global Tasks, Tasks, Conditions,
Considerations, Transitions, Parameters, Property Functions, Property
Bindings, automatic Context relationships, and Context Data. They are comments
rather than a new Summary object or synthetic StateTree fields. Global Nodes
appear before the top-level States so the agent sees StateTree-wide execution
context before the hierarchy.
Summary does not expand the complete hierarchy, owned State Nodes,
Transitions, Parameters, or Property Bindings.

An out-of-date compiled hash is reported as stale. Summary never compiles,
links, validates, repairs, or saves the StateTree to obtain a cleaner status.

## Query

The confirmed StateTree Query surface is:

```sal
# Exact target read; append `with schema` when needed.
query omle

query omle
summary

query omle
tree [state@<id>] [depth <N>]

query omle
states ["text"]

query omle
nodes ["text"]

query omle
parameters ["text"]

query omle
state@<id>

query omle
node@<id>

query omle
transition@<id>

query omle
parameter@<container-id>/<property-id>

query omle
object@<context-id>

query omle
references to state@<id>[.<member>]

query omle
references to transition@<id>[.<member>]

query omle
references to node@<id>[.<member>]

query omle
references to parameter@<container-id>/<property-id>[.<member>]

query omle
references to object@<context-id>[.<member>]

query omle
palette entries ["text"] to <destination>

query omle
palette @<id> to <destination>
```

StateTree defines no `find`, `exec flow`, `data flow`, `with pins`, independent
Transition collection search, or exact-name operation. State and Node display
names may be duplicated, so plural text search is discovery and canonical
stable references are exact access.

The bare `query omle` form reads the exact target's own meaningful fields. It
may use `with schema`; `summary` remains the separate orientation operation.

### Tree

`tree` is the primary structural read. Without a State selector it starts at
all top-level States; with `state@id` it starts at that State. `depth` counts
State hierarchy edges only. It does not count owned Nodes, Transitions, or
Parameter entries as additional tree levels.

The default depth is 20. A shallower explicit depth is useful for very broad
StateTrees. If State depth is truncated, the boundary State remains present and
an adjacent comment reports that deeper Children exist. The result never
returns an invalid partial statement.

Tree returns full State identity and compact owned Node and Transition
bindings sufficient for exact follow-up Queries. It does not expand every
reflected Node property or every Binding by default. This keeps the normal
orientation path small while preserving all authored order.

Tree contains only the State hierarchy and each returned State's compact owned
Nodes and Transitions. StateTree-wide Evaluators and Global Tasks belong to
`summary`, so a tree subtree read never repeats them.

### Collections And Exact Objects

`states ["text"]` searches current State names, descriptions, tags, visible
paths, and native State type text. `nodes ["text"]` searches all directly
authored Evaluators, Global Tasks, Enter Conditions, Tasks, Considerations, and
Transition Conditions by current display name, selected native type, owning
State, and role path. Embedded Property Functions remain part of Binding state
and do not become ordinary Node collection results; the owning Binding returns
their `node(...)` text and `node@id` remains exact-readable.

`parameters ["text"]` searches root and State Parameter names, native Property
Bag types, metadata, and visible owner paths. Each result includes the complete
container/property identity and preserves descriptor order inside its
container.

Collection results use shared bounded cursor pagination with a default page of
50. Result order follows authored document order, not relevance, unless an
explicit supported `order by` is later defined by the domain.

`state@id`, `node@id`, `transition@id`, and
`parameter@container-id/property-id` are exact within the bound Asset. A valid,
unique Schema Context descriptor additionally supports `object@context-id`.
Exact reads return the object's full meaningful authored fields and enough
owner context to copy its reference into a later Patch. Context Data objects
are read-only. A Property Function `node@id` is exact but retains its outer
Binding-owned lifecycle.

### References

StateTree local references are factual relationships inside the resolved
Asset. They include:

- Transition and linked-State links to `state@id`;
- State links embedded in native Node or Instance fields;
- explicit Property Bindings whose source or target belongs to a Node,
  Parameter, Context Data, Required Event, or Delegate endpoint;
- automatic Context relationships currently derived by UE for unoverridden
  Context-usage Properties;
- outer and nested Property Function Bindings.

An automatic Context relationship is returned in the same data-flow direction
as an explicit Binding, with an adjacent comment identifying it as automatic
and derived. This keeps references factual without pretending that the asset
contains a removable Binding record.

Required Event member references use the stable owner rather than UE's derived
Event Struct ID. Delegate references use the stable owner plus the reflected
Dispatcher or Listener member path. Examples include:

```sal
references to state@state-guid.RequiredEventToEnter.Payload
references to transition@transition-guid.RequiredEvent.Payload.Value
references to node@producer-guid.Instance.OnFinished
references to transition@transition-guid.DelegateListener
```

References return stored Property Bindings even when their endpoint is
currently inactive, with an adjacent diagnostic explaining that state. Runtime
Event Tag matching is not an authored reference: two Event descriptors using
the same Gameplay Tag do not reference one another.

The initial domain does not support `in project`. UE's StateTree Asset Registry
tag exposes Schema identity but not a zero-load authored-reference index.
Loading every StateTree asset to simulate project scope would repeat the
unbounded-load failure that the shared reference design explicitly forbids.
Project scope requires a dedicated index before it can be claimed complete.

## Schema Discovery

`with schema` applies to the exact StateTree target, State, Node, Transition,
Parameter, Context Data object, or destination-bound Palette Entry. The adapter
derives discoverable capability from:

- current `UStateTreeSchema` virtual capability methods;
- explicit non-mutating `UStateTreeEditorSchema` capability hooks;
- UE Reflection on the concrete State, Node struct/Class, Instance data, and
  Transition fields;
- Property usage and edit-condition metadata;
- StateTree Property Binding visibility and compatibility;
- the exact Palette candidate and intended owner relationship.

Schema must report dynamic restrictions such as allowed State types and
selection behavior, whether Evaluators, Enter Conditions, Considerations,
multiple Tasks, global Parameters, or task completion are supported, and which
native Node types the current Schema permits. It must not infer these facts
from reflected fields alone because the authoritative rules are virtual
methods.

Arbitrary custom `UStateTreeEditorSchema::Validate()` logic is not a
discoverable schema surface: the method mutates and reports no enumerable rule
set. Rules exposed only there are checked on the transient preflight copy and
returned as diagnostics rather than presented as complete schema guidance.

The target itself also owns behavior fields such as `GlobalTasksCompletion`,
Schema and Editor Schema instances, Root Parameters, Evaluators, and Global
Tasks. Bare `query omle` is its exact read; `with schema` reports those fields,
supported Query operations, native creation destinations, and direct Patch
statements. `summary` is the compact discovery surface for the read-only
Context Data objects declared by the current Schema; it remains orientation
and is never treated as a writable target read.

Exact object schema also reports copyable native destination forms, allowed
lifecycle operations, bindable `Node` and `Instance` member surfaces, member
direction and native type compatibility, indexed member paths, and Parameter
inheritance or override constraints. `ExecutionRuntimeData` is not a Property
Binding surface. Context Data schema is read-only. Property Function Node
schema reports its Binding-owned creation and deletion cascades rather than
advertising independent `add`, `remove`, or `move`.

State and Transition schema distinguish the writable Required Event descriptor
from its conditional read-only Binding surface. The descriptor contains
`PayloadStruct`; the runtime `FStateTreeEvent` surface contains bindable native
members such as `Tag`, `Payload.*`, and `Origin`. Schema reports the current
Payload member layout, source visibility, and the activation condition
`bHasRequiredEventToEnter == true` or `Trigger == OnEvent`.

Exact schema marks reflected `FStateTreeDelegateDispatcher` members as Binding
sources and `FStateTreeDelegateListener` members as Binding targets. Transition
schema marks its `FStateTreeTransitionDelegateListener` member as a Binding
target. These members do not support `set` or `reset`. Parameter Delegate
endpoints are advertised only when the current Schema and compiler
configuration actually support them; UE 5.7 disables Root Parameter Dispatcher
compilation by default.

As in every SAL domain, schema guidance is returned in adjacent comments around
ordinary Object Text. StateTree introduces no schema-result object.

## Palette

Every StateTree Palette query includes the exact destination it is meant to
serve:

```sal
query omle
palette entries "Follow" to state@companion-guid.Tasks

query omle
palette @P_OmleFollowTask to state@companion-guid.Tasks
with schema
```

The destination is part of candidate discovery, not information deferred to a
later Patch. It lets the adapter apply Schema, role, cardinality, type,
visibility, and ordering rules before returning a candidate. An exact Palette
entry is still revalidated against the same destination when consumed.

Palette returns ordinary `state(...)`, `node(...)`, `transition(...)`, or
`parameter(...)` creation bindings containing a stable creation-capability
`palette` id. It does not ask the Agent to infer a constructor from a native
Class, editor label, or owner role. Context Data are Schema-owned and
read-only, so Palette never offers them.

Node discovery uses UE's `FStateTreeNodeClassCache` and revalidates every
candidate against the current Schema. It must filter hidden, abstract,
deprecated, and disallowed native structs and Blueprint Classes, as well as
role capabilities such as Evaluators, Conditions, Considerations, and
multiple Tasks.

A destination-bound search returns ordinary copyable creation bindings:

```sal
follow = node(palette: "P_OmleFollowTask")
```

The later Patch repeats the same exact destination, and `add` revalidates it
before materialization. Query-result aliases are local to their returned
document; the agent copies the returned constructor call into the Patch and may
choose a new alias.

State and Transition entries wrap UE-native construction behavior but keep
their `type` as the native Class or Struct path. State `Type`, Transition
`Trigger`, target, and other required arguments remain named native fields
returned by the exact entry's schema.

A Property Function entry is a destination-bound `node(...)` candidate. It is
consumed by the first owning
`bind function.Instance.<schema-output-member> -> target`, not by `add`; that
Binding materializes and owns the function Node. The member is the exact single
Output property returned by the Palette Entry schema, not a fixed `Result`
name. Parameter entries are consumed by ordinary
`add ... to <Parameters destination>`.

Blueprint StateTree Node discovery must remain bounded. The adapter first
filters and pages Asset Registry-backed candidates, then resolves the Classes
needed for the returned page or exact entry. It must not synchronously load
every Blueprint Node asset merely to answer an unbounded Palette query.

## Patch

StateTree reuses Core lifecycle operations:

- `add`
- `remove`
- `set`
- `reset`
- `move`
- `invoke`
- `save`

StateTree additionally defines the relationship operations `bind` and
`unbind`. They edit Property Bindings but do not create a Binding object.
`compile` is a StateTree terminal statement following the already established
Blueprint terminal model.

### Add

Every directly added object uses a local Palette-backed alias and an exact
destination. There is no bare StateTree `add`, including for top-level States:

```sal
patch omle

newRoot = state(palette: "P_State", Name: Root)
add newRoot to omle.SubTrees

follow = node(palette: "P_OmleFollowTask")
add follow to state@companion-guid.Tasks

onFailure = transition(palette: "P_Transition", Trigger: OnStateFailed)
add onFailure to state@companion-guid.Transitions

speed = parameter(palette: "P_FloatParameter")
add speed to state@companion-guid.Parameters
```

The Parameter binding above is abbreviated. A real Palette result and copied
Patch binding include every schema-required native field; the agent must not
assume that `palette` alone is sufficient.

Destinations are native authored roles exposed by exact schema: target
`SubTrees`, `Evaluators`, `GlobalTasks`, and `RootParameters`; State
`Children`, `EnterConditions`, `Tasks` or `SingleTask`, `Considerations`,
`Transitions`, and State `Parameters`; and Transition `Conditions`. The exact
schema supplies the copyable spelling. Schema cardinality decides whether a
Task role uses `Tasks` or `SingleTask`; the adapter reports corrupt source state
if both contain authored Nodes rather than silently hiding one. An object's
native type never implies a missing destination.

`before` and `after` may place a new object relative to a sibling in the same
ordered destination. Cross-owner placement must use an exact compatible `to`
destination and is validated before mutation.

### Set And Reset

`set` and `reset` use exact schema-approved native fields:

```sal
set state@companion-guid.Name = Companion
set state@companion-guid.Weight = 1.5
set transition@transition-guid.State = {
  Name: SafetyRecovery,
  ID: state@safety-guid,
  LinkType: GotoState,
  Fallback: None
}
set node@follow-task-guid.Instance.AcceptanceRadius = 150.0
reset node@follow-task-guid.Instance.AcceptanceRadius

set state@companion-guid.bHasRequiredEventToEnter = true
set state@companion-guid.RequiredEventToEnter.PayloadStruct = <native-struct-type>
set transition@transition-guid.Trigger = OnEvent
set transition@transition-guid.RequiredEvent.Tag = <native-gameplay-tag>

set parameter@container-guid/property-guid.<schema-name-member> = MoveSpeed
set parameter@container-guid/property-guid.<schema-type-member> = <native-type>
set parameter@container-guid/property-guid.<schema-value-member> = 600.0
reset parameter@container-guid/property-guid.<schema-value-member>
```

The exact Node field path comes from `with schema`; the example `Instance`
partition is not assumed for every Node type. Node `type` is read-only after
materialization. Replacing a Node type requires a schema-discovered compound
Operation because UE must recreate Node, Instance, execution data, id, and
Bindings coherently.

Changing State `Type`, Linked State targets, linked assets, or Parameters must
follow UE's native semantic setter path and report every planned cascade. A
plain reflected write is insufficient when UE clears Tasks, changes selection
behavior, or rebuilds linked Parameter layout.

Required Event descriptor fields use ordinary `set` and `reset`; the derived
`Payload.*` and `Origin` Binding surface is read-only. An ordered Patch may
activate and configure the Event before binding from it. Changing the activation
field or `PayloadStruct` preserves the descriptor itself but can make existing
Event-source Bindings inactive or structurally invalid. The mutation result
must report that consequence rather than silently deleting or hiding it.

The Parameter member tokens above are schema placeholders, not SAL-renamed
fields. The agent copies the native member paths returned by exact `with
schema`. On an inherited fixed-layout Parameter, setting its value creates or
updates the local override and resetting it removes that override. Rename,
type change, removal, and reordering are rejected unless the current container
owns an editable layout.

### Move

State hierarchy and authored collection order use Core `move`:

```sal
move state@idle-guid before state@follow-guid
move state@catch-up-guid after state@safety-guid
move state@pickup-guid to state@companion-guid.Children
move parameter@container-guid/a-guid before parameter@container-guid/b-guid
```

Moving a State keeps its native id. The adapter rejects moving a State into
itself or its descendants and rejects overlapping multi-object plans that
would move both a parent and one of its included descendants. Reordering Nodes
or Transitions is available only where the exact object's schema declares a
compatible ordered destination. Parameter `before` and `after` references must
belong to the same editable container.

Order affects which Binding sources are visible. Preflight therefore validates
the post-move document and reports every Binding that would become invalid.
No live move may silently lose a Binding that was absent from its mutation
plan.

### Remove And Invoke

`remove node@id` deletes one independently owned Evaluator, Task, Condition, or
Consideration. `remove transition@id` deletes one owned Transition.
`remove parameter@container-id/property-id` is available only for an editable
local Parameter layout.
`remove state@id` removes that State's complete authored subtree, including all
descendant States, owned Nodes, and Transitions. Preflight and the mutation
result must enumerate that cascade before live application. Removal cleans
Bindings whose source or target no longer exists. A Transition outside the
removed subtree that points at a removed State is not automatically deleted;
UE preserves the invalid link for diagnosis, and the mutation result reports
it.

A Property Function Node is not independently removable. Its lifecycle belongs
to the outer Binding described below. Context Data objects are read-only and do
not support field or lifecycle mutation.

Specialized subordinate actions use `invoke` only after the exact Operation is
designed and exposed by the subject's schema. StateTree does not add equivalent
domain verbs alongside Core `add`, `remove`, or `move`. If a future duplicate
Operation is exposed, it must reproduce UE's deep-copy and identity-remapping
semantics without the system Clipboard; its name, arguments, and result are
not defined here.

## Property Bindings And Parameters

### Parameters

A Parameter is one authored, ordered Property Bag entry exposed as
`parameter(...)`. Its exact identity is the pair of its Parameter-container
Guid and Property descriptor Guid:

```sal
query omle
parameters "speed"

query omle
parameter@container-guid/property-guid
with schema
```

Collection results retain their owner container and descriptor order. Exact
readback uses the same composite text in `id`:

```sal
speed = parameter(
  id: "container-guid/property-guid",
  type: "<schema-returned native type>"
)
```

This is the compact identity form. An exact read continues with the meaningful
native fields returned by the active Property Bag schema and UE Reflection
rather than a parallel SAL Parameter type system. Editable local layouts
support Palette-backed `add`,
schema-approved rename and type/value/metadata edits, same-container reorder,
and `remove`. Linked fixed layouts retain the inherited descriptor identity;
only value override `set` and `reset` are local edits.

### Binding Endpoints

The authoritative store for explicit authored Bindings is
`FStateTreeEditorPropertyBindings::PropertyBindings`. A Binding has no stable
id and never becomes `binding(...)` or `binding@id`. UE may additionally derive
an automatic relationship for an unoverridden Context-usage Property through
`UStateTreeEditorData::FindContextData()`. Object Text uses the same ordinary
arrow in actual data-flow direction for both:

```sal
parameter@container-guid/speed-guid -> node@task-guid.Instance.AcceptanceRadius

# automatic Context
object@actor-context-guid -> node@task-guid.Instance.Actor

node@producer-guid.Instance.Points[0].Value -> node@consumer-guid.Instance.Targets[1].Value
parameter@container-guid/points-guid[0].X -> node@consumer-guid.Instance.TargetX
node@task-guid.Instance.<schema-output-member> -> parameter@container-guid/result-guid
```

The adjacent comment is sufficient to distinguish the derived relationship;
StateTree adds no `automatic` keyword, relationship constructor, or normalized
object. An automatic Context arrow is queryable but has no authored record or
independent lifecycle.

An endpoint is one stable owner plus an ordered native member path. Array
segments use non-negative `[N]` indexes and normalize as numeric path segments;
the adapter never stores only a localized display path. Bindable StateTree Node
surfaces are `node@id.Node...` and `node@id.Instance...` as reported by exact
schema. `ExecutionRuntimeData` is runtime state and cannot be bound.

Those two Node prefixes map to different native struct identities:

- `node@N.Instance.X` uses `FStateTreeEditorNode::ID` as the native Struct ID
  and encodes `X` after removing the public `Instance` prefix;
- `node@N.Node.X` uses `FStateTreeEditorNode::GetNodeID()` and encodes `X`
  after removing the public `Node` prefix;
- `parameter@C/P...` uses container `C` as the native Struct ID and descriptor
  `P` as the implicit first Property path segment before any suffix;
- in `parameter@C/P[0].X`, index `0` is the `ArrayIndex` of that implicit
  descriptor segment, followed by member `X`.

Native path segments also carry current Property Guid, polymorphic instance
type, and access information. Parse and preflight reconstruct and validate
those facts from the exact current value and schema through UE's native
Property Binding path resolution. They are never guessed from display names or
dropped from execution state. If the public member path cannot reconstruct one
unique native path, the adapter rejects it and returns schema guidance; this
document does not silently add another path syntax.

`object@context-id` selects one valid, uniquely identified Schema Context Data
slot. Its following member path is resolved against the descriptor's native
`Struct`. The actual Context value is supplied by the runtime execution owner
and is not stored in the StateTree asset.

### Required Events

`FStateTreeEventDesc` is authored inside a State or Transition. When active, UE
also presents a temporary `FStateTreeEvent` as a Property Binding source. SAL
keeps both views under the native owner field instead of exposing UE's
deterministic internal Event Struct ID:

```sal
state@state-guid.RequiredEventToEnter.Payload.Request ->
  node@task-guid.Instance.Request

transition@transition-guid.RequiredEvent.Payload.Value ->
  node@condition-guid.Instance.Value
```

In `set` and `reset`, `RequiredEventToEnter` or `RequiredEvent` resolves to the
authored descriptor. In a Binding-source position, its native runtime members
resolve against `FStateTreeEvent`. `PayloadStruct` is descriptor-only;
`Payload.*` and `Origin` are source-only. Exact `with schema` makes that
operation-dependent distinction explicit, so SAL does not need another Event
reference form.

A State Event exists as a Binding source only while
`bHasRequiredEventToEnter` is true. It follows UE's State execution-path
visibility and may feed later accessible Nodes. A Transition Event exists only
while `Trigger == OnEvent` and may feed Conditions owned by that same
Transition. A new `bind` rejects an inactive or inaccessible Event source.

The descriptor is valid when at least one of `Tag` or `PayloadStruct` is set.
Compilation also verifies that an On Event Transition is compatible with the
Required Event of its target State. These are native compiler constraints, not
additional SAL validation rules.

UE preserves hidden descriptor values when an Event is disabled or a
Transition changes Trigger. A raw Property Binding using the derived Event ID
may also remain in malformed or not-yet-validated EditorData. Query scans and
returns that authored fact without invoking validation, maps the internal ID
back to the owner-derived member path, and annotates the relationship as
inactive or invalid. Native validation or compile may subsequently remove an
Event Binding whose source no longer exists.

### Delegates

StateTree Delegates are native Property Binding endpoints, not authored
objects. A Dispatcher is a source and a Listener is a target:

```sal
node@producer-guid.Instance.OnFinished ->
  node@consumer-guid.Instance.Listener

node@producer-guid.Instance.OnFinished ->
  transition@transition-guid.DelegateListener
```

The exact endpoint may live on a Node's `Node` or `Instance` surface, or on a
schema-approved Parameter surface. `with schema` supplies the reflected path,
direction, accessibility, and current compiler support. Dispatcher and
Listener endpoints cannot be bound across StateTree assets.

The authored relationship remains one ordinary entry in `EditorBindings`.
Compilation assigns or reuses an internal Dispatcher Guid and generates
Listener ids; those tokens are derived runtime data and never become SAL ids or
fields. A Transition Listener is only an empty editor-side Binding target;
compiled Transition data stores the resolved Dispatcher token directly.

For a Transition, a new Delegate Binding is valid only when the final ordered
Patch state has `Trigger == OnDelegate`:

```sal
patch omle
set transition@transition-guid.Trigger = OnDelegate
bind node@producer-guid.Instance.OnFinished ->
  transition@transition-guid.DelegateListener
```

Changing the Trigger away from `OnDelegate` does not remove the authored
Binding. UE keeps it dormant and ignores it during compilation; changing back
reactivates it. Query and `references` therefore preserve the arrow and add an
adjacent comment:

```sal
node@producer-guid.Instance.OnFinished ->
  transition@transition-guid.DelegateListener
# inactive: Transition.Trigger is not OnDelegate
```

An inactive relationship can still be removed by an exact `unbind`. An active
On Delegate Transition without a Dispatcher Binding is a compiler error.

### Bind And Unbind

Patch uses explicit relationship operations:

```sal
patch omle
bind parameter@container-guid/speed-guid -> node@task-guid.Instance.AcceptanceRadius
unbind parameter@container-guid/old-threshold-guid -> node@guard-guid.Instance.Threshold

bind state@state-guid.RequiredEventToEnter.Payload.Target ->
  node@task-guid.Instance.Target

bind node@producer-guid.Instance.OnFinished ->
  transition@transition-guid.DelegateListener
```

`bind` and `unbind` use the same real data-flow direction as returned arrows.
The adapter resolves member identity, visibility, direction, and native type
compatibility, then maps the pair to UE's Source Path, Target Path, and output
flag. For an ordinary input Binding, the native Target Path is the arrow's
right endpoint. For a UE output Binding, the native Target Path is the arrow's
left endpoint because UE stores that copy direction inversely. That native
Target Path is the unique replacement key. A `bind` that replaces an existing
relationship must expose that replacement and every cascade in preflight and
mutation results. `unbind` names the complete existing pair; a source mismatch
is an error rather than permission to remove whichever Binding currently
reaches the target.

Binding an explicit source to a Context-usage Property creates or replaces its
authored override and suppresses automatic Context resolution for that target.
`unbind` accepts only an explicit authored Binding. If removing that override
causes UE to derive an automatic Context relationship again, preflight and the
mutation result report the restored data flow. Attempting to `unbind` an
automatic arrow is an error because there is no authored relationship to
remove.

Delegate Bindings use the same target-owned replacement rule. One Dispatcher
may feed multiple Listeners, while one Listener has at most one Dispatcher.
Replacing a Listener source is an ordinary reported Binding replacement. A
Required Event Binding uses the same operation but additionally validates its
conditional Event source and execution-path visibility.

An exact State, Node, Transition, Parameter, Context object, or member
`references` query discovers all matching explicit and automatic uses. Exact
object reads interleave relevant arrows with ordinary Object Text and annotate
automatic Context or inactive Event/Delegate arrows adjacently; there is no
`with bindings` expansion.

### Property Functions

A Property Function is an `FStateTreeEditorNode` embedded in and owned by its
function-result Binding. It uses ordinary `node(...)` text and may be queried
exactly with `node@id`, but it is not returned by the ordinary `nodes`
collection and has no independent lifecycle operation:

```sal
clamp = node(
  id: "function-guid",
  type: "<native Property Function struct>"
)
parameter@container-guid/min-guid -> clamp.Instance.Min
clamp.Instance.<schema-output-member> -> node@task-guid.Instance.AcceptanceRadius
```

Creation starts from a destination-bound Palette entry. Its schema identifies
the function's single native Output property; SAL never assumes that property
is named `Result`. The first owning result `bind` consumes the local creation
binding and materializes the embedded Node. It must precede bindings to that
function's inputs in the same ordered Patch:

```sal
patch omle

clamp = node(palette: "P_ClampPropertyFunction")
bind clamp.Instance.<schema-output-member> -> node@task-guid.Instance.AcceptanceRadius
bind parameter@container-guid/min-guid -> clamp.Instance.Min
```

`add clamp`, `remove node@function-guid`, and moving the function independently
are invalid. Unbinding an ordinary input removes only that relationship. If an
input is itself the owning result Binding of a nested Property Function, its
nested subtree is removed. Unbinding the outer function-result Binding deletes
the complete owned Property Function subtree,
including nested functions and their input Bindings, and reports that cascade:

```sal
unbind node@function-guid.Instance.<schema-output-member> -> node@task-guid.Instance.AcceptanceRadius
```

## Normalized Contract

StateTree reuses the shared `Target`, `Query`, `Patch`, `ObjectText`, `Call`,
`StableRef`, `MemberRef`, result, and diagnostic shapes. It adds no domain
request or result wrapper.

- bare target read uses the shared `{kind: "target"}` Query operation;
- collections add `states`, `nodes`, and `parameters` operation kinds;
- exact operations use `state`, `node`, `transition`, `parameter`, and the
  literal fallback kind `object`;
- `parameter@container/property` normalizes as
  `{kind: "parameter", id: "container/property"}`; the adapter parses and
  validates both native Guid components inside the bound Asset;
- StateTree Palette operations carry their exact destination Member Reference;
- indexed member paths normalize each `[N]` as a numeric path segment;
- `bind` and `unbind` normalize to an operation kind plus exact `from` and
  `to` References in data-flow order.

The StateTree-owned normalized additions are:

```ts
interface StateTreePaletteEntriesOperation {
  kind: "palette_entries";
  text?: string;
  to: Ref;
}

interface StateTreePaletteIdOperation {
  kind: "palette";
  id: string;
  to: Ref;
}

interface BindOperation {
  kind: "bind";
  from: Ref;
  to: Ref;
}

interface UnbindOperation {
  kind: "unbind";
  from: Ref;
  to: Ref;
}
```

Constructor calls remain generic Calls. `state`, `node`, `transition`,
`parameter`, and `object` are adapter-declared callees, not new expression
types. Explicit Binding and automatic Context arrows remain ordinary ordered
Edges in Object Text; their StateTree meaning comes from the resolved target,
endpoint schema, and adjacent comments. Automatic status adds no normalized
relationship type.

## Compile And Save

Compilation is explicit and uses the same independent Patch target as authored
editing:

```sal
patch omle
compile
```

Compile followed by save is valid:

```sal
patch omle
compile
save
```

No ordinary authored edit implicitly compiles. Core `save` may persist stale
EditorData without compiling, but the result warns that compiled data is out
of date. `save` never means compile.

The Bridge calls `UStateTreeEditingSubsystem::CompileStateTree()`. Native
compilation first validates the StateTree. On failure UE clears invalid old
compiled data and resets `LastCompiledEditorDataHash`; this is a real mutation,
not a read-only check. Compiler messages preserve severity and map their
available context back to `state@id`, `node@id`, `transition@id`, or
`parameter@container-id/property-id` when UE provides that authored context.

Validation before compile may remove Required Event Bindings whose conditional
Event source no longer exists. A dormant Transition Delegate Binding remains
structurally valid, is ignored while `Trigger` is not `OnDelegate`, and is not
removed merely by compilation. Compile generates Delegate runtime tokens but
never exposes them as authored output. An `OnDelegate` Transition without a
valid Dispatcher Binding fails compilation.

As in the Blueprint terminal contract, compiler errors are resulting authored
asset state rather than failure to execute the statement. A completed compile
may therefore continue to an explicit following `save`, including when UE has
cleared invalid compiled data. Only inability to resolve or execute the
compiler stops the terminal sequence before save.

Compile is rejected during PIE when the native StateTree editor would reject
it. Dry-run compile executes against the transient preflight copy and cannot
alter live compiled data.

## Mutation Planning, Transactions, And Dry Run

StateTree uses the shared mutation result and dry-run contract:

1. resolve the exact Asset Path and current revision;
2. resolve every stable or composite Parameter reference, Context object, and
   destination-bound Palette capability;
3. duplicate the complete StateTree into transient ownership;
4. map live ids to the duplicated structural locations, because UE asset
   duplication regenerates authored ids;
5. execute the same ordered lifecycle, Parameter, `bind`, `unbind`, and
   Property Function edit adapter against the transient copy;
6. run StateTree validation on the copy to discover repairs, and run compile
   only when the ordered Patch requests it;
7. classify every repair as an immediate native edit consequence, an explicit
   compile consequence, or a dormant authored diagnostic, then compare every
   applied cascade with the mutation plan;
8. stop for dry run, or execute one live top-level transaction;
9. roll back the whole transaction and restore prior dirty state on failure;
10. mark the Package dirty and notify an already-open ViewModel after success.

`ValidateStateTree()` is a mutating repair pass. An unplanned removal, id
change, link rewrite, or Binding cleanup is not silently accepted just because
UE validation performed it. The adapter must either include the effect in the
preflight result or reject and roll back. Validation on the transient copy does
not authorize an ordinary live `set` to erase latent authored data that UE's
native property edit preserves. When the Patch explicitly includes `compile`,
the validation performed by that native compile path and its reported cleanup
are part of the terminal operation.

One SAL Patch produces one Undo step. The Bridge must not drive
`FStateTreeViewModel` selection-oriented commands as its primary edit API,
because they alter user selection and open separate transactions. A thin
StateTree edit adapter should use public UE types and APIs, reproduce the
native initialization and notification sequence, then ask an existing
ViewModel only to refresh.

## Diagnostics

StateTree diagnostics use the shared registered diagnostic model. Important
diagnostic conditions include:

- target Asset Path missing or resolving to a non-StateTree Class;
- State, Node, Transition, Context object, or composite Parameter reference
  missing or ambiguous in corrupt source;
- Schema Context descriptor with an invalid or duplicate ID;
- stale Palette capability, destination mismatch, or Schema-disallowed
  placement;
- unsupported State type, selection behavior, or owner collection;
- invalid move hierarchy or order-dependent Binding loss;
- read-only Context Data mutation;
- invisible, incorrectly directed, indexed-path-invalid, or type-incompatible
  Binding endpoint;
- inactive or inaccessible Required Event Binding source, invalid Event
  descriptor, or Event Binding invalidated by a Payload type change;
- Delegate source/target direction mismatch, cross-asset Delegate endpoint,
  dormant Transition Delegate Binding, or active On Delegate Transition with no
  Dispatcher Binding;
- attempt to `unbind` a derived automatic Context relationship, or failure to
  report automatic Context data flow restored after removing an override;
- attempt to add, remove, or move a Binding-owned Property Function directly;
- replacement Binding, Property Function subtree, or linked Parameter override
  cascade omitted from the mutation plan;
- malformed State Link, linked Asset, Event, delegate listener, or Parameter
  layout;
- compiler error or warning tied to the nearest stable authored object;
- unplanned validation cascade;
- compile attempted during PIE;
- save failure after a completed compile.

Diagnostics must preserve malformed authored content in Query results and
guide an Agent to an exact follow-up object, schema, or Patch. They must not
convert a failed exact reference into a display-name search.

## Runtime And Debugger Boundary

Runtime StateTree execution is one-to-many with its asset and may be hosted by
`UStateTreeComponent`, Behavior Tree, Mass, or a custom execution context.
Scanning Components would therefore create a false runtime model.

Schema Context Data descriptors are Schema-declared and target-visible, but
their actual values belong to one runtime execution owner. `object@context-id`
therefore never implies a live Actor, Component, subsystem, or execution
instance lookup.

Ordinary Query must never enable Trace automatically. Trace startup changes
channels and recording state, and late recording may be incomplete. Runtime
recording, timeline reads, breakpoints, events, and execution control require a
separate future design; none are implied by the authored domain.

## Asset Creation Boundary

UE creates a StateTree asset through `UStateTreeFactory`, which requires a
native Schema Class, creates the matching EditorData and EditorSchema Classes,
adds a Root State, and compiles the new asset. The current Asset domain has no
Factory-backed creation contract.

StateTree Patch therefore starts from an existing exact Asset. A future generic
Asset creation design may expose the StateTree Factory through Asset Palette,
but this domain must not hide Asset creation inside `patch state_tree`, guess a
Schema, or invent a second creation path.

## Implementation Sequence

Implementation should proceed in this order:

1. extend the shared parser and normalized contract for exact target read,
   indexed member paths, StateTree operations, and `bind`/`unbind`;
2. register the static StateTree interface card and domain capability;
3. implement exact Asset resolution, target schema, and read-only
   `EditorData` traversal;
4. implement Summary globals, Tree, collections, exact references, local
   references, and destination-bound Palette reads;
5. implement Parameters, Context Data, Binding arrows, and exact schema;
6. implement the thin native edit adapter, Property Function ownership, and
   transient preflight path;
7. implement compile, diagnostics, and Core save composition;
8. add focused parser/formatter, contract, Bridge, dry-run, transaction,
   compile, and regression tests against UE 5.7;
9. audit token cost and an Omle `ST_OmleLocalBehavior` authoring workflow.

The first acceptance workflow should be able to inspect and author the ordered
`SafetyRecovery`, `ExternalActionControl`, and `Companion` hierarchy, add exact
Schema-allowed Nodes and Transitions through Palette, verify the result with
exact queries, create and bind Parameters and one Property Function, compile
with object-linked diagnostics, and save without any implicit runtime or
project-wide asset scan.
