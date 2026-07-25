# Blueprint Domain

## Scope

Blueprint Domain exposes authored `UBlueprint` state outside individual Graph
bodies:

- Class Settings;
- Variables and Dispatchers;
- top-level Graph lifecycle;
- SimpleConstructionScript Components;
- Interface and override implementation;
- Blueprint compile and Package save.

Nodes, Pins, Edges, signatures, and Graph-local Timeline operations belong to
Graph Domain. Effective generated `UClass` Reflection and Defaults belong to
Class Domain.

## UE Boundary

Important native ownership includes:

- `BlueprintGuid`, `BlueprintType`, `ParentClass`, and Class Settings;
- `ImplementedInterfaces`;
- `NewVariables`;
- `FunctionGraphs`, `MacroGraphs`, `UbergraphPages`;
- `DelegateSignatureGraphs`;
- Interface-owned Graphs;
- `SimpleConstructionScript`;
- `Timelines`.

Public Blueprint objects are Blueprint state, Variable, Dispatcher, Graph, and
SCS Component. There is no generic Member object. Timeline Template state is
presented through its owning Graph Node rather than a separate Timeline object.

## Target

Discovery may use only the exact Asset Path:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door"
}

query door
summary
```

Canonical exact readback and every Patch require `BlueprintGuid`:

```sal
door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The Path loads; `id` verifies. A mismatch fails instead of continuing by Path.
Asset move changes Path but not BlueprintGuid; duplication creates a new Guid.

## Identity Environment

| Object | StableRef |
| --- | --- |
| Variable or Dispatcher | `@VarGuid` |
| top-level or child Graph | `@GraphGuid` |
| SCS Component | `@USCS_Node.VariableGuid` |
| Blueprint-scoped referenceable Node | `@NodeGuid` |
| function-local Variable | `@TopLevelFunctionGraphGuid/VarGuid` |

All one-segment categories are audited together. If a Variable and Graph share
the same Guid text, `@Guid` is an identity conflict. An optional semantic tag
cannot disambiguate it.

Names are readable/searchable state, never Patch identity.

## Blueprint State

The Target table carries identity. Requested authored state is ordinary data:

```sal
blueprintState = {
  type: BPTYPE_Normal,
  Status: BS_Dirty,
  ParentClass: "/Script/Engine.Actor",
  BlueprintNamespace: "Game.Doors",
  BlueprintCategory: "Doors",
  ImplementedInterfaces: [
    {
      Interface: "/Script/MyGame.Damageable",
      Graphs: [@damageable-graph-guid]
    }
  ]
}
```

`type` preserves native `EBlueprintType`; `Status` preserves transient
`EBlueprintStatus` and is read-only. Native Class Settings retain exact field
names and enum values.

Class Settings authored source is distinct from compiled Class Reflection.
`GeneratedClass` is navigation evidence, not another field to edit in place.

### Options And Namespaces

Fields include native Blueprint and Class Options such as:

- `bRunConstructionScriptOnDrag`;
- `BlueprintDisplayName`, `BlueprintDescription`;
- `BlueprintNamespace`, `BlueprintCategory`, `HideCategories`;
- `bGenerateConstClass`, `bGenerateAbstractClass`, `bDeprecate`;
- `ShouldCookPropertyGuidsValue`, `CompileMode`;
- `ImportedNamespaces`.

Exact schema reports instance availability and editability. Namespace edits
must update UE's Namespace Registry and open editor import context. Effective
default Namespaces are derived and reported in comments, not fabricated as an
authored field.

### Implemented Interfaces

`ImplementedInterfaces` preserves `FBPInterfaceDescription` order and native
shape. Its Graph list uses Target-relative Graph StableRefs. It is read-only as
one aggregate field because its owned Graphs require native compound
operations.

Inherited effective interfaces belong to Class Reflection.

## Variables

```sal
door.Health = {
  id: "33333333-3333-3333-3333-333333333333",
  type: "<FEdGraphPinType native text>",
  FriendlyName: "Health",
  Category: "Stats",
  PropertyFlags: "<EPropertyFlags native text>",
  RepNotifyFunc: None,
  MetaDataArray: "<native array text>"
}
```

The binding member is `VarName`; `id` is `VarGuid`; `type` is native
`FEdGraphPinType` text. Only `NewVariables` entries are Variable objects.
Inherited, native, local, generated, and multicast delegate declarations are
not duplicated.

`FBPVariableDescription::DefaultValue` is compiler staging text, not the
effective Class Default. It is returned only when native staging text actually
exists and is not edited as the durable default. Effective values use Class
Domain after compile.

## Dispatchers

A Dispatcher is one semantic object backed by a multicast-delegate Variable
description and same-named Delegate Signature Graph:

```sal
door.OnOpened = {
  id: "44444444-4444-4444-4444-444444444444",
  type: "<multicast-delegate FEdGraphPinType native text>",
  Category: "Events"
}
```

Exact Dispatcher readback returns a compact Graph object and a related Graph
Target for signature editing. It does not add a `graph` field to Dispatcher.
Missing, duplicated, or mismatched backing halves are inconsistent Blueprint
state and are never repaired by name guessing.

## Graphs

A Blueprint-owned Graph object is compact data:

```sal
openDoor = {
  id: "22222222-2222-2222-2222-222222222222",
  name: OpenDoor,
  type: GT_Function,
  Schema: "/Script/BlueprintGraph.EdGraphSchema_K2",
  bEditable: true
}
# owner: UBlueprint::FunctionGraphs
```

`type` is native `EGraphType`; it does not encode lifecycle role. Function,
Dispatcher Signature, Interface, Override, and other Graphs may all report
`GT_Function`. Native ownership and flags supply their role.

Blueprint Domain owns direct top-level Graph lifecycle. Graph body operations
use a separate Graph Target:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related openDoorGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
handoff edit_graph to openDoorGraph
```

Child/collapsed Graphs have the same flat Graph Target form. They are not a
nested Domain.

## Components

SCS Component hierarchy uses member binding paths:

```sal
door.Root = {
  id: "55555555-5555-5555-5555-555555555555",
  type: "/Script/Engine.SceneComponent"
}

Root.Mesh = {
  id: "66666666-6666-6666-6666-666666666666",
  type: "/Script/Engine.StaticMeshComponent",
  StaticMesh: "/Game/Meshes/SM_Door.SM_Door"
}
```

`id` is `USCS_Node::VariableGuid`; `type` is the Component Class Path; binding
name is `InternalVariableName`. Component Template properties are flattened
with exact native names.

Binding paths express parent-child structure and statement order preserves
sibling read order. Native/inherited/instance Components and Child Actor
subtrees are outside local SCS ownership.

Relationship-derived parent fields are read-only. Structural changes use
Component lifecycle operations.

## Timeline Boundary

UE stores a Timeline as a `UK2Node_Timeline` plus matching
`UTimelineTemplate`. Public identity is the Graph Node:

```sal
openTimeline = node {
  id: "77777777-7777-7777-7777-777777777777",
  type: "/Script/BlueprintGraph.K2Node_Timeline",
  TimelineName: OpenDoor,
  TimelineLength: 1.0,
  LengthMode: TL_TimelineLength,
  bLoop: false,
  FloatTracks: [
    {
      TrackName: Alpha,
      bIsExternalCurve: false,
      CurveFloat: {
        FloatCurve: {
          Keys: [
            {
              Time: 0.0,
              Value: 0.0,
              InterpMode: RCIM_Linear
            }
          ]
        }
      }
    }
  ]
}
```

`node` is an erasable tag. The NodeGuid is identity; TimelineGuid and Key
handles are not. Tracks and Keys are nested native state selected by Track
name, channel, and time through schema-defined Graph operations.

Timeline read and mutation use the exact owning Graph Target. Blueprint Domain
only supplies backing ownership support and finalization.

## Query

```sal
target
summary
variables ["text"]
dispatchers ["text"]
graphs ["text"]
components ["text"]
variable <name>
dispatcher <name>
graph <name>
component <name>
@identity
references to <exact-subject> [in project]
palette entries ["text"]
palette @id
```

`summary` returns compact Blueprint state plus collection counts.

Collections preserve authored order, support optional fuzzy search, exact
filters on `name`, `id`, and `type`, ordering by those fields, and cursor
pagination. Exact-name operations discover identity; exact StableRef reads
return complete compact state and may use `with schema`.

The exact Target itself uses:

```sal
query door
target
with schema
```

When an exact Graph object is selected inside Blueprint Domain, schema covers
Blueprint-owned Graph lifecycle. Its related Graph Target covers Node/Pin/Edge
work.

### References

Local references scan exactly the bound Blueprint. `in project` uses the
bounded project provider.

Providers preserve native owner and scope:

- member Variables and Dispatchers use VarGuid;
- locals use Function Graph scope plus local VarGuid;
- Components use SCS identity;
- callable Function/Macro and Interface declarations use native declaration
  identity;
- Custom Events use their Node identity.

Multiple native facts on one object return member-path candidates. Names and
titles are never fallback identity.

## Palette And Creation

Blueprint Palette returns ordinary ObjectExpr fields:

```sal
Variable = {
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
}

Dispatcher = { palette: "P_BlueprintDispatcher" }
FunctionGraph = { palette: "P_FunctionGraph" }
Component = { palette: "P_StaticMeshComponent" }
```

The active Domain, Palette identity, binding path, and destination provide
creation meaning. Tags do not.

```sal
patch door

door.Health = {
  palette: "P_BlueprintVariable",
  type: "<FEdGraphPinType native text>"
}
add door.Health

door.OnOpened = { palette: "P_BlueprintDispatcher" }
add door.OnOpened

OpenDoor = { palette: "P_FunctionGraph" }
add OpenDoor

Mesh = { palette: "P_StaticMeshComponent" }
add Mesh to @root-component-guid
```

Palette revalidates native capabilities and exact names during `add`. It never
silently chooses a suffix or repairs a child-Blueprint collision.

## Patch Model

One Patch belongs only to Blueprint Domain. It cannot contain Widget-tree or
Graph-body edits. Cross-Domain work uses following requests.

Existing objects use StableRefs:

```sal
set door.BlueprintDescription = "Interactive door"
reset door.BlueprintDescription

set @variable-guid.Category = Stats
move @variable-guid before @anchor-variable-guid
remove @variable-guid

set @component-guid.StaticMesh =
  "/Game/Meshes/SM_Door.SM_Door"
move @component-guid to @parent-component-guid
remove @component-guid
```

Exact schema is authoritative for writable fields, reset behavior, native
constraints, lifecycle, and operations.

### Class Settings

Ordinary field edits use native notifications and Blueprint modification.
They do not imply full compile.

`ParentClass` uses ordinary `set` but follows the complete native reparent
workflow:

```sal
set door.ParentClass = "/Script/Engine.Actor"
```

Preflight validates Blueprint family, cycles, deprecation, native reparent
rules, Interface and function conformance, SCS implications, default rebasing,
and loaded dependents. Apply reconstructs required Nodes and performs the native
full compile required by reparenting. Compiler messages are resulting state.

There is no `reset ParentClass` and no separate reparent verb.

### Interface Operations

```sal
invoke door ImplementInterface(
  Interface: "/Script/MyGame.Damageable"
)

invoke door RemoveInterface(
  Interface: "/Script/MyGame.Damageable",
  bPreserveFunctions: true
)
```

The exact native Interface Class Path is required. Preflight validates
Blueprint support, inherited/direct conflicts, function-name conflicts, and
the complete generated surface.

Removal requires explicit preservation choice. Native removal/promote/convert
effects, including Graphs and Event Nodes, are planned. Loaded children are
updated; unloaded children are reported for deferred native refresh rather
than loaded without bound.

Override/interface function implementation is:

```sal
invoke door ImplementFunction(
  function: "/Script/Engine.Actor:ReceiveAnyDamage"
)
```

It may return an Event Node or Function Graph according to UE behavior.

### Graph Lifecycle

Direct Graph add follows the exact Palette's native ownership path. Existing
Graph rename, reorder, and removal use:

```sal
set @graph-guid.name = OpenDoorInternal
move @graph-guid before @anchor-graph-guid
remove @graph-guid
```

Rename updates dependent references. Reorder is available only for native
collections with a supported authored order. Remove honors Graph ownership,
schema permission, usage conflicts, and native cascades.

Interface, Dispatcher Signature, Construction Script, and nested Graph
lifecycle remains owned by their native compound behavior rather than direct
top-level add/remove.

### Variable Lifecycle

Variable creation calls `AddMemberVariable` and returns UE-generated VarGuid.
Field edits route through native rename/type paths:

```sal
set @variable-guid.VarName = MaxHealth
set @variable-guid.type = "<FEdGraphPinType native text>"
set @variable-guid.Category = Stats
reset @variable-guid.RepNotifyFunc
```

Type changes report Node reconstruction, broken incompatible Edges, replication
changes, and loaded child effects. Rename with active RepNotify requires an
explicit preceding reset rather than a hidden confirmation loss.

Move uses `NewVariables` authored order. Remove deletes native getter/setter
Nodes and incident Edges as UE does. Potential silent child declaration rename
is a preflight conflict.

### Dispatcher Lifecycle

Dispatcher add, rename, move, and remove update its Variable/Signature Graph
pair atomically. Type is read-only. Removal conflicts while resolvable usage
Nodes would remain invalid; callers remove those through their Graph Targets
first.

Signature parameter editing belongs to the related Graph Target.

### Component Lifecycle

Component add uses the exact Component Palette capability and native SCS path.
Bare add uses Actor context; explicit `to @parent-guid` requires compatible
same-Blueprint Scene Components.

```sal
set @mesh-guid.name = DoorMesh
set @mesh-guid.StaticMesh = "/Game/Meshes/SM_Door.SM_Door"
move @trigger-guid to @root-guid
invoke @mesh-guid MakeNewSceneRoot()
invoke @mesh-guid Duplicate() as copy
remove @mesh-guid
```

Move uses native reparent validation and preview Actor transform conversion.
There is no generic Component class conversion or sibling reorder.

Remove follows native child promotion and reports Graph reference effects.
Default Scene Root replacement uses `MakeNewSceneRoot`.

### Timeline Operations

Timeline creation uses Graph Palette. Exact Timeline Node schema owns fields,
Tracks, Keys, Curve ownership, duplicate, and removal:

```sal
invoke @timeline-node-guid AddFloatTrack(
  TrackName: Alpha
) as alpha

invoke @timeline-node-guid AddKey(
  TrackName: Alpha,
  Time: 1.0,
  Value: 1.0
)

invoke @timeline-node-guid UseExternalCurve(
  TrackName: Alpha,
  Curve: "/Game/Curves/C_Alpha.C_Alpha"
)
```

These statements execute only in the related Graph Target. Track names are
unique; Key identity uses Track, channel when required, and time. External
Curve assets are referenced but never mutated through Timeline.

## Compile And Save

Explicit finalization is an independent Blueprint Patch:

```sal
patch door
compile
save
```

Valid forms are compile, save, or compile followed by save. Bindings and
authored edits cannot share the request. `compile` may occur once and only as
the first statement; `save` may occur once and must be last. `save` then
`compile`, repeated terminal statements, `set`, `add`, Graph/Widget operations,
or arbitrary `invoke` in the same request are invalid. A Graph Target is never
a compilation unit.

Compile:

- targets the whole Blueprint;
- uses `FKismetEditorUtilities::CompileBlueprint` with the native registered
  compiler and synchronous Full Compile;
- always includes `EBlueprintCompileOptions::SkipSave`, so global
  Save-on-Compile cannot create an implicit disk write;
- rejects direct `BPTYPE_MacroLibrary` compilation, matching the Blueprint
  Editor capability; Macro Libraries still participate through UE dependency
  compilation;
- outside PIE follows normal editor capability; during PIE or simulation
  follows `FBlueprintEditor::InEditingMode`,
  `CanAlwaysRecompileWhilePlayingInEditor()`, and configured disallowed base
  Classes; an allowed PIE compile includes
  `IncludeCDOInReferenceReplacement`;
- preserves Package dirty state;
- returns final native Status and ordered tokenized compiler messages.

Compile does not pre-call `RefreshAllNodes`, mark the Blueprint structurally
modified, or dirty the Package. UE may reconstruct and reinstate as part of its
compiler, but SAL does not turn compile into a silent authored-source repair.
A dirty Package remains dirty and a clean Package is not dirtied merely by
compile.

An explicit compile runs even when current Status is `BS_UpToDate`; it is the
editor Compile action and may be used to obtain fresh compiler messages.
Completed compilation with `BS_Error` is still an executed terminal action,
not failure to invoke the compiler, and an explicit following save may still
run.

Save resolves the Blueprint's one owning Package and uses the Core dirty-only,
non-interactive Source Control-aware path. During PIE it follows
`Editor.AllowSavingAssetsDuringPIE`. For `compile` followed by `save`, both
terminal statements and current save availability are validated before compile
starts. Thus a known disabled PIE save rejects the whole sequence before
compilation. A later checkout, read-only, or disk failure can still occur after
compile; that yields `applied: true, isError: true` and never claims compile
rolled back. Saving a clean Package is successful `already clean` and does not
rewrite it.

Compiler messages remain factual ordered comments in the first canonical
Result Text block. Native token sources are mapped to returned Graph, Node, and
Pin identity when possible; related Graph Targets are retained when needed:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
objects
# compile: BS_Error; 1 errors; 0 warnings
# error @aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa: <UE compiler message>
```

Compiler errors are resulting Blueprint state, not failure to execute compile.
A following explicit save may still run. Structured validation or execution
failures use the later independent diagnostics block. Save failure is external
I/O and does not roll back completed compile state.

Dry run resolves the exact Blueprint, validates the complete sequence,
compiler/type/PIE availability, owning Package, current dirty state, and PIE
save policy, then stops before compilation, checkout, or disk write:

```sal
patch door dry run
compile
save
```

It may report `would compile: full` and `would save: <package>`, but cannot
predict final `EBlueprintStatus`, compiler counts, tokenized messages, Source
Control outcome, or final writability. It must not fabricate compiler messages
or present a guessed `BS_UpToDate`/`BS_Error` result as current fact.

## Dry Run And Transactions

Authored dry run duplicates the complete owning Blueprint, including SCS,
generated Class state, Graphs, Timeline Templates, and internal Curves. It
executes the same ordered native edit functions, reports aliases instead of
transient ids, and discards the sandbox.

Live apply uses one private top-level transaction. A later native failure closes
and explicitly undoes it; transaction cancellation is not rollback.

All determinable cross-object effects participate in the plan. Unknown or
unbounded native repair behavior fails closed.

## Handoffs

- Graph body/signature/Timeline work: Graph Target;
- WidgetTree work: Widget Target;
- generated Reflection and Defaults: Class Target.

Every handoff uses a canonical independent related Target. Blueprint Domain
never composes these Domains into one request.
