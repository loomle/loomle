# PCG Runtime Design

## Status

This is a planned design. Nothing in this file adds a public Domain, Target,
tool, request schema, private route, runtime registry, or compatibility adapter
by documentation alone.

This document is subordinate to
[`SCENE_PCG_DOMAIN_FAMILY_DESIGN.md`](SCENE_PCG_DOMAIN_FAMILY_DESIGN.md) for
cross-owner architecture and complements
[`PCG_DOMAIN_DESIGN.md`](PCG_DOMAIN_DESIGN.md), which owns asset-backed
`UPCGGraph` authorship. If the documents disagree, the family design governs.

The runtime model has one Target-backed SAL Domain, one non-Target execution
owner, and one private World-binding service:

| Semantic owner | Public model | Lifetime | Responsibility |
| --- | --- | --- | --- |
| `pcg_component` | persistent, Level-owned SAL Target | one source-aware durable Component locator inside an Actor | Graph binding and instance Parameter override authorship, initially Query-only |
| `pcg_execution` | asynchronous execution context and retained record | one admitted native task plus bounded retention | admission, continuation/poll, cancellation request, messages, effects, cleanup observation, and captured Data View |
| private PCG World binding | Bridge service, not a SAL owner or public Target | one weak live `UWorld` incarnation plus epoch | typed-PCG World selection, source mapping, stale-ticket rejection, and runtime-loss detection |

`pcg_execution` is not a SAL Domain keyword, Target, StableRef identity
environment, or related Target. Its opaque execution id is returned and
consumed by a typed PCG execution frontend built on Loomle's shared async
execution kernel and continuation envelope.

Every SAL Query or Patch still has exactly one active Target and one active
Domain. Only `pcg_component` from this document enters those Target sets.
Cross-owner results use existing related Targets/result-only handoffs for later
requests. Generic Editor/PIE/SIE control and observation use Python + Skill;
selected UObjects may request published persistent projections through
`sal.object()`.

## Decision

The following boundaries are fixed:

1. `pcg` owns authored Graph assets. It does not own a World, Component,
   instance override, or generated output.
2. `level` owns Actor and generic Component lifecycle, persistent Actor identity,
   map and external-Actor packages, and save.
3. `pcg_component` owns the PCG-specific authored surface of an already existing
   original Component. Its publishable baseline is Query-only.
4. Explicit Python owns generic live World/PIE/SIE control and observation.
   No public `world_session` Domain, Target, StableRef, handoff, or Query is
   introduced.
5. A Bridge-private PCG World epoch registry and opaque `pcgWorldTicket` make
   typed-PCG live operations race-safe. They provide no generic World control,
   SAL identity, or authority on their own.
6. `pcg_execution` owns an execution record, not the World or the generated
   UObjects it observes.
7. Generation, cancellation, cleanup, and inspection are asynchronous World
   operations. They do not use `sal_patch` and do not inherit Patch dry-run,
   Undo, rollback, revision, or save semantics.
8. The native root is arbitrary `UPCGComponent`. `APCGVolume::PCGComponent` is
   one supported case, not the abstraction.

The typed PCG frontend is an architectural decision. It reuses the existing
Python outer async contract—`running`, `succeeded`, `failed`, or `lost`, opaque
execution id, exact continuation/poll, retention, runtime loss,
`stateMayHaveChanged`, and no automatic replay—through a shared internal
kernel. Its public MCP tool name, private transport route, typed PCG fields,
native-outcome mapping, and pagination shape are not chosen in this design.
Examples below are illustrative semantic records, not grammar or wire
contracts. PCG native work never runs through Python, and Python projection is
not accepted as a PCG World ticket.

## Why Existing Patch Is Unsuitable

SAL Patch describes one authored Target mutation with complete preflight,
transactional application, rollback behavior, and revision-shaped readback.
Native PCG execution can outlive the initiating request and can create, reuse,
or destroy World-owned Actors, Components, instances, and cached data. Native
cancellation and cleanup are cooperative and can race completion. A UObject
transaction cannot rewind the scheduler or external side effects.

Therefore:

- execution admission cannot be a Patch `invoke`;
- a request-local alias cannot become a cross-request execution id;
- an execution id cannot be represented as a Target-relative StableRef;
- status and Data View cannot be ordinary Query statements against an execution
  Target;
- no execution operation supports authored dry run or rollback;
- no execution operation performs or implies save.

## Native Source Basis

The implementation must be checked against the public PCG and PCGEditor headers
and sources in each supported engine, including:

- `PCGComponent.h` and `PCGComponent.cpp`;
- `PCGGraph.h`, `PCGGraphInstance.h`, and their implementations;
- `PCGManagedResource.h`;
- UE 5.8 `PCGManagedResourceContainer.h`;
- `PCGSubsystem.h`;
- `PCGComponentExecutionState.h`;
- `PCGGraphExecutionInspection.h`;
- partition and hierarchical-generation APIs;
- editor World and PIE subsystem APIs.

The UE 5.8 experimental MCP PCG Toolset is workflow evidence, not a dependency
or protocol specification. Its useful reference operations include:

- `UPCGToolset::ListGraphInstances`;
- `UPCGToolset::SpawnGraphInstance`;
- `UPCGToolset::GetGraphInstance`;
- `UPCGToolset::SetGraphInstanceParameters`;
- `UPCGToolset::ExecuteGraphInstance`;
- `UPCGToolset::GetNodeDataView`;
- `UPCGSpatialToolset::RunPCGInstantGraph`;
- separate SceneTools, ActorTools, and AssetTools for generic scene and save
  work.

Reading editor and experimental source does not create a build dependency.
The plugin always enables UE's built-in PCG plugin, and `LoomleBridge` plus
`LoomleBridgeTests` compile only against the public `PCG` runtime module for
this family. They never depend on `PCGEditor`.

Its boundary is deliberately not copied:

- listing and instance mutation are centered on `APCGVolume` rather than all
  `UPCGComponent` owners;
- spawning chooses an `APCGVolume` and is generic Level authorship in Loomle;
- execution is non-transactable but does not expose the complete task lifecycle
  required here;
- its Data View workflow can enable inspection as a side effect and require a
  later execution;
- generic Actor deletion and dirty-package saving live in other Toolsets;
- broad dirty-package save is not an acceptable runtime cleanup or persistence
  mechanism. Runtime results may retain an exact persistence-owner Target for
  a later independent save request, but never perform that save.

Loomle uses native PCG APIs behind version adapters and never silently falls
back to the experimental Toolset or Python.

## Native Ownership Model

For a placed PCG Volume the common chain is:

```text
World / ULevel / owning Package
`- APCGVolume
   `- UPCGComponent
      `- UPCGGraphInstance
         `- referenced UPCGGraphInterface
            `- top UPCGGraph asset
```

The same Component and GraphInstance relationship applies to any Actor that
owns a `UPCGComponent`.

`UPCGComponent::GetGraph()` returns the top `UPCGGraph`.
`UPCGComponent::GetGraphInstance()` exposes the Component-owned instance where
local Graph Parameter overrides live. `SetGraph()` follows native Graph-instance
lifecycle and can invoke `RefreshAfterGraphChanged`; a reflected write to a
similarly named member is not equivalent.

Generated output has separate physical, lifecycle, and observation facts:

- a generated UObject is physically outered to a World, Level, Actor,
  Component, or external package according to native creation;
- managed-resource objects associate generated output with a source or
  partition-local managing `UPCGComponent`;
- `UPCGSubsystem` coordinates execution and cleanup in the World;
- an execution record observes and attributes before/after facts but does not
  become the UObject owner or an independent deletion authority.

For partitioned generation, both the logical source Component and the actual
local managing Component must be reported when known. The Graph asset owns
neither generated layer.

## Capability Boundary

### Baseline authoring and observation

The first safe baseline covers:

- canonical `pcg_component` resolution and Query;
- direct and top Graph-interface readback;
- Graph Parameter descriptor, effective value, local override bit, and local
  value readback;
- explicit Python control/observation of arbitrary live non-template
  `UPCGComponent` objects, with optional `sal.object()` authored-source
  projection where provable;
- typed-PCG source-aware World discovery, normalized selector, opaque
  `pcgWorldTicket`, and liveness validation;
- managed-resource inventory only in an exact typed-PCG World/source context;
- explicit Editor versus in-process PIE/SIE routing;
- related `level`, `pcg`, and `pcg_component` Targets without authority merge.

The baseline does not expose `pcg_component` Patch. Graph binding and override
mutation remain planned extensions gated by native side-effect isolation,
transaction, Undo, persistence-owner, and UE 5.7/5.8 failure-injection tests.

### Typed asynchronous execution surface

After its typed-frontend and shared-kernel gates pass, runtime can add:

- generate admission for an exact live `UPCGComponent`;
- retained poll/status facts and bounded observation;
- supersession-guarded source-scoped cancellation request;
- Component-scoped native cleanup as a separate admitted operation;
- messages captured for one admitted operation;
- before/after managed-resource and package-dirty observations;
- inspection requested before generation;
- bounded Node/output-Pin Data View with exact stack selection;
- partition-aware effect completeness;
- Editor and supported in-process PIE routing.

### Explicit exclusions

The first design does not promise:

- generic Actor or Component creation, destruction, rename, attachment, or
  transform edits;
- automatic creation or special treatment of `APCGVolume`;
- Component Patch before the isolation gate passes;
- editing top Graph Nodes, Pins, Edges, Parameters, or layout through a
  Component;
- editing an external shared `UPCGGraphInstance` through a Component;
- mutation or direct deletion of an individual managed resource;
- cleanup narrowly scoped to output attributed exclusively to one retained
  operation record when native PCG manages it by Component;
- `ClearPCGLink`, bake, adoption, or direct generated-object deletion helpers;
- synchronous immediate cleanup;
- automatic map loading, streaming, PIE start/stop, or World switching;
- automatic Package save after Patch, generate, cancel, cleanup, or inspection;
- atomic rollback of World execution;
- concurrent inspection-enabled runs for Components sharing one Graph;
- Data View that was not requested before native generation;
- live-object authority or reusable execution authority after World binding
  expiry;
  a bounded terminal/lost record may remain readable but cannot resolve or
  control live objects;
- instant Graph execution unless separately designed and approved.

## Identity And Lifetime

The public source, private World binding, and execution record have different
temporal bounds:

| Owner | Identity anchor | Survives save/reload | Survives map switch | Survives PIE stop | Survives Editor restart |
| --- | --- | --- | --- | --- | --- |
| `pcg_component` | Level asset, Actor Guid, and source-aware native/instance/SCS locator | yes, when that durable locator persists | yes, after exact Level reload | yes | yes |
| private PCG World binding/ticket | weak `UWorld` incarnation, runtime epoch, selector, Client, and source mapping | no | no | no for PIE | no |
| `pcg_execution` | opaque record id bound to one private World epoch and admitted native task | retained record only while runtime and retention survive | retained terminal/lost record only | retained terminal/lost record only | no |

An expired `pcgWorldTicket` never rediscovers or aliases a later World. A lost
or expired execution id never aliases a later task, even if a native numeric
task id is reused. World-binding expiry removes every typed live-control use.
An already-terminal record, or the `lost` record produced for unfinished work,
remains readable only through its exact execution id until ordinary retention,
explicit release, runtime loss, or memory policy permits its bounded payload
to expire.

## `pcg_component` Target

### Canonical shape

The canonical exact shape always includes a source-aware locator. A native
Component example is:

```sal
c = target {
  domain: pcg_component,
  asset: "/Game/Maps/Forest.Forest",
  actorId: "11111111-1111-1111-1111-111111111111",
  source: "native",
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent"
}
```

An SCS Component example is:

```sal
c = target {
  domain: pcg_component,
  asset: "/Game/Maps/Forest.Forest",
  actorId: "11111111-1111-1111-1111-111111111111",
  source: "scs",
  id: "<OwnerGeneratedClassPath>#<VariableGuid>",
  type: "/Script/PCG.PCGComponent"
}
```

Canonical field order is `domain, asset, actorId, source, id, type`.

| Field | Meaning |
| --- | --- |
| `asset` | exact top-level source `UWorld` object path |
| `actorId` | serialized source `AActor::ActorGuid` |
| `source` | closed locator kind: `native`, `instance`, or `scs` |
| `id` | source-dependent durable locator payload described below |
| `type` | actual Component Class Path, which must derive from `UPCGComponent` |

Actor label, Actor UObject name, object path, Component label, array index,
selection, pointer, Graph path, and redundant Component-name fields are not
Target fields.
Additional provenance may be returned as resolution evidence but does not alter
the canonical identity shape.

`id` is interpreted only together with `source`:

- `source: native` identifies a durable native Component slot and uses its
  actor-scoped Component `FName` as `id`;
- `source: instance` identifies a serialized Actor-instance Component slot and
  also uses its actor-scoped Component `FName` as `id`;
- `source: scs` identifies the declaring `USCS_Node` and uses a qualified value
  containing the owner Generated Class Path and the node `VariableGuid`;
- a User Construction Script (`ucs`) Component has no persistent
  `pcg_component` Target and remains live-only. Python can return it as ordinary
  data, while typed PCG v1 rejects it as an execution source.

The canonical encoding and escaping of the qualified SCS `id` must preserve
both components without ambiguity. It must not fall back to the SCS variable
display name.

### Source-aware durable identity

For `native` and `instance`, this Target identifies the actor-scoped serialized
Component slot, not one live UObject incarnation. Blueprint compilation,
construction reruns, undo/redo, or native reconstruction may replace the live
Component object while retaining the same logical slot.

For `scs`, durable identity is the declaring owner Generated Class plus
`USCS_Node::VariableGuid`, then mapped to the exact instance Component owned by
the Actor. Live Component `FName` is readback evidence, not SCS identity.

For `native` and `instance`, a delete followed by recreation of the same `FName`
and Class in the same Actor is the same durable logical slot under this contract;
it is not durably distinguishable as a UObject incarnation after reload. For
SCS, replacement is governed by the declaring VariableGuid. Live-incarnation
identity stays inside one Python call or a typed PCG source-bound World ticket
and execution record; it is not a SAL StableRef.

Resolution must still fail closed when:

- the source Level is not the exact loaded source World;
- the Actor Guid is absent or duplicated;
- a `native` or `instance` locator has no unique serialized Component `FName`;
- an `scs` locator cannot resolve the exact owner Generated Class and
  `USCS_Node::VariableGuid` or cannot map that node to one instance Component;
- the resolved Component was produced only by a User Construction Script;
- the resolved object is a template, preview, partition-local, runtime-only, or
  otherwise not the persistent slot represented by the Target;
- the Class does not match or derive from `UPCGComponent`;
- the persistence owner cannot be determined.

Every statement boundary re-resolves through Level, Actor Guid, source kind,
and source-specific locator. Generated Class rename/redirect handling, SCS node
reconstruction, inherited SCS declarations, and duplicate mapping are release
gates. The adapter never retains a raw Component, `USCS_Node`, or GraphInstance
pointer as persistent identity.

### Parameter identity

Graph Parameter overrides are addressed by the persistent property-bag
descriptor Guid from the top Graph. The Guid identifies the declaration; name
is display and search text.

```sal
@22222222-2222-2222-2222-222222222222
```

The override bit and local value are Component-instance authored state attached
to that declaration. They do not alias the Graph default in `pcg`.

The owned `UPCGGraphInstance`, task ids, managed resources, and inspection
stacks are not durable StableRefs within `pcg_component`.

## Python Live World Workflow

Generic live World work belongs to Python + Skill. A short Python call may
enumerate arbitrary non-template `UPCGComponent` objects, read live Actor or
Component facts, manage PIE/SIE, or control selection/camera/streaming under the
Python fallback's permission and partial-effect rules. Each call reacquires the
World and UObjects from current native state.

`sal.object(component)` may ask the Bridge to project a selected original
Editor Component as an exact `pcg_component` view, or a uniquely proven
PIE/SIE duplicate as an `authored_source` view. Runtime-created, UCS,
partition-local, generated, preview, task, resource, and Data View objects do
not gain persistent SAL identity. The projection stores no live handle and
cannot start execution.

## Bridge-Private PCG World Binding

Typed PCG still needs race-safe live World identity. The Bridge therefore owns
a private registry keyed by native `UWorld` identity and an incarnation epoch.
It stores weak native references plus lifecycle hooks; it does not publish a
Domain, Target, StableRef, Result Text handoff, schema module, or generic World
control surface.

Typed PCG discovery/prepare accepts one canonical `pcg_component` source and an
exact selector. Illustrative selectors are:

```json
{ "worldKind": "editor" }
```

```json
{
  "worldKind": "pie",
  "playMode": "simulate",
  "pieInstance": 0
}
```

`worldKind` distinguishes Editor from in-process PIE. SIE is not a distinct
`EWorldType`; it is a PIE World context plus `playMode: simulate`, proven from
Editor play-session state. `playMode: play` selects ordinary PIE. Supported
multi-PIE routing may require additional exact net-role/client discriminators.
V1 rejects more than one matching candidate instead of guessing. Preview,
inactive, transition, Standalone, and New Process Worlds are unavailable.

After resolving the exact World and proving a unique mapping from the
persistent source Target to one live Component incarnation, the typed frontend
may return:

```json
{
  "selector": { "worldKind": "pie", "playMode": "simulate", "pieInstance": 0 },
  "pcgWorldTicket": "<opaque-short-lived-ticket>",
  "sourceReady": true
}
```

The ticket is bound to the Client/runtime, normalized selector, private World
epoch, persistent source Target, and live Component incarnation. It is a
short-lived freshness precondition for typed-PCG prepare follow-ups,
generation, cleanup, and prepared live observation. It cannot select or
control a generic World, enumerate Actors, enter SAL text, replace the source
Target, authorize an operation by itself, or survive World/Component
replacement. Every use independently revalidates client/runtime, selector,
source, permissions, and requested operation.

Python controls any required transition first and confirms it in a later
short call. Typed PCG discovery then issues a fresh ticket. Map replacement,
PIE start/stop, travel, World cleanup, Bridge shutdown, Editor restart,
play/simulate mode transition, source reconstruction, or ambiguous mapping
invalidates the old ticket immediately. Prepare accepts selector plus source
and issues the ticket. Generate, cleanup, and prepared live observation submit
selector plus ticket plus that same source and revalidate all three atomically
on the Game Thread. Poll, cancellation, inspection, and retained data use only
the admitted execution id and never rerun the selector.

## `pcg_execution` Context

`pcg_execution` is a semantic owner implemented by the typed PCG frontend on
the shared async kernel. It has no SAL Target form.

A successful admission returns an opaque execution id. That id is bound to:

- one exact Bridge-private World epoch and normalized typed-PCG selector;
- one exact live Component incarnation;
- one required canonical `pcg_component` source Target and a proven mapping
  from that authored slot to the live Component incarnation;
- one operation and admitted option snapshot;
- one native PCG task id when scheduling produced one;
- one Graph-interface and top-Graph snapshot;
- one capture policy selected before generation;
- one bounded record-retention policy.

The id is not a StableRef, UObject path, asset path, source mutation authority,
or proof that all observed resources belong exclusively to one run. The
shared kernel resolves the opaque record and exact runtime; the typed PCG
adapter then validates Client visibility, World binding, Component, operation
generation, and native task association on every PCG control request.

The record retains, within explicit bounds:

- admission, cancellation-request, native terminal, loss, and expiry facts;
- captured PCG messages;
- before/after managed-resource inventories and completeness;
- dirty-package observations and saved-state facts;
- inspection stack identity and retained Data View data when requested;
- related persistent Targets as ordinary response data;
- uncertainty when callback, World, Component, or subsystem state was lost.

Native UE 5.7 and 5.8 terminal execution status is `Completed` or `Aborted`.
The shared outer envelope remains `running`, `succeeded`, `failed`, or `lost`;
the typed PCG payload preserves native status, cancellation-request, cleanup,
and inspection facts independently. Exact native-to-public outcome mapping is
a frontend release gate. Error messages do not by themselves convert native
`Completed` into an invented native failure status.

## Cross-Owner Handoffs

Targets remain flat and independent. `handoff` is the existing result-only SAL
Result Text envelope: an adapter emits it to retain a purpose and exact related
Target for a later request. It is not a Query/Patch statement, does not perform
the named action, and never transfers mutation authority.

Every related SAL Target is retained by Object Text through a scoped reference
or by an explicit handoff; path strings and execution-record fields do not
count as Target retention.

A `pcg_component` Query may return:

- its owning `level` or exact persistence-owner Target when supported;
- the top asset-backed `pcg` Target;
- an Asset Target for a direct external Graph interface.

A Python result has no main SAL Target. Each successful `sal.object()` view is
therefore an independent validated exact Query result, not a related Target or
handoff from Python. Its canonical `level`, `pcg`, `pcg_component`, or other
published Target is resubmitted and re-resolved by a later independent SAL
request.

An execution-family response may return its source Component Target, captured
top Graph Target, affected persistence owners, normalized World selector,
ticket/execution liveness, and public stale/lost reasons as ordinary data. It
never exposes the private epoch identifier. Neither `pcgWorldTicket` nor the
execution context is returned in a Target or related-Target variant set.

Graph editing, Component configuration, Level saving, and execution remain
separate requests. A later failure never rolls back an earlier request owned by
another surface.

## Component Query

### Query-only baseline

The first publishable `pcg_component` surface is Query-only. It exposes
persistent authored Component facts, not a live generated-resource view.

Expected Query operations are:

```text
target
summary
parameters [search]
one parameter by descriptor Guid
related authored Targets
exact schema
```

`summary` returns a cohesive authored view such as:

```sal
c = component {
  asset: "/Game/Maps/Forest.Forest",
  actorId: "11111111-1111-1111-1111-111111111111",
  source: native,
  id: "PCGComponent",
  type: "/Script/PCG.PCGComponent",
  GraphInterface: {
    path: "/Game/PCG/Forest.Forest",
    type: "/Script/PCG.PCGGraph"
  },
  Graph: {
    path: "/Game/PCG/Forest.Forest",
    type: "/Script/PCG.PCGGraph"
  },
  persistenceOwner: {
    kind: level_or_external_actor,
    package: "<exact-package-or-null>",
    saveTargetAvailable: true
  }
}
```

Exact result field names are schema release gates. The important distinction is
semantic: `GraphInterface` is the direct interface assigned to the
Component-owned instance; `Graph` is the top graph returned by `GetGraph()`.
They can differ when an external `UPCGGraphInstance` participates in the chain.

The owned GraphInstance remains nested Component state. It is not an Asset
Target, independently saveable object, or writable shared-settings path.

Live task state, generated-resource inventory, inspection data, and live
Component incarnation belong to Python observation or the typed PCG
discovery/execution surface. They are intentionally absent from the persistent
Component Query baseline.

### Instance Parameter readback

Each Parameter result is keyed by the top Graph descriptor Guid and reports:

```sal
density = parameter {
  id: "22222222-2222-2222-2222-222222222222",
  name: "Density",
  type: "<native-property-bag-type>",
  overridden: true,
  localValue: 0.35,
  effectiveValue: 0.35,
  effectiveSource: component_override
}
```

The supported semantic sources are:

```text
component_override
parent_instance
graph_default
```

`localValue` is present only when the Component-owned GraphInstance has the
local override bit. `effectiveValue` is resolved through the full native Graph
interface chain. An inherited external GraphInstance value must not be labeled
as a top-Graph default.

Parameter declaration schema, name, type, order, and Guid belong to the Graph.
`pcg_component` cannot create, remove, rename, or retype a Graph Parameter.

### Planned Component mutation gate

Future authored mutation may use ordinary SAL Patch only if it satisfies the
full Patch contract. The intended closed surface is Graph-interface assignment
and exact set/reset of Component-owned Parameter overrides. It does not include
generic reflected writes to partitioning, scheduling, generation trigger,
editing mode, tracking, activation, radii, or other fields whose native setters
can schedule work or clean resources.

For an SCS-backed Component, a future writable field must be proven to
serialize as an override on this exact placed Actor instance. A Graph binding,
default, or configuration value owned by the SCS node, Component template,
Class Default Object, or Blueprint remains read-only and returns a
Blueprint/Class handoff; this Target never writes through to the declaration.

The mutation extension remains unavailable until a dedicated guard proves:

1. the exact persistent Component locator and persistence owner resolve before any
   mutation;
2. every value and source object validates before live mutation;
3. no generation, cleanup, refresh, construction rerun, or inspection capture
   is active;
4. requested authored writes can be staged before one native notification;
5. no PCG task starts inside the transaction;
6. failure restores the Component, GraphInstance values and override bits,
   package dirty state, and Undo/Redo history;
7. successful refresh or cache invalidation is reported as a post-Patch effect,
   not hidden inside authored atomicity;
8. UE 5.7 and UE 5.8 pass the same failure-injection suite;
9. internal-Level and World Partition external-Actor persistence-owner Targets
   and Result Text handoffs are exact; source-control checks occur only in the
   later independent owner save request.

`UPCGGraphInstance::UpdatePropertyOverride` modifies and broadcasts Parameter
changes. `UPCGComponent::SetGraph()` calls native Graph refresh paths that can
invalidate data, inspection, tracking, or managed resources. A UObject
transaction cannot undo a task that was already scheduled. Until the guard is
implemented, the static card and runtime schema advertise Query-only.

Even after mutation is enabled, `pcg_component` has no save authority. It
returns the exact `level` or persistence-owner handoff. No fallback saves all
dirty packages.

## Typed PCG Live Discovery And Observation

The typed PCG frontend, not SAL Query, owns the minimum live observation needed
for safe execution. Its versioned operation surface may include:

```text
discover/prepare one canonical source in an exact World selector
return normalized selector and source-bound pcgWorldTicket
read live source registration, Graph, bounds, busy, and task facts
read managed resources for that prepared source
return persistent source/Graph/Level Targets as ordinary data
return typed runtime capability schema
```

The final operation keywords and fields remain typed-frontend schema gates.
V1 prepares only a canonical persistent `pcg_component` source and rejects a
runtime-only source. Generic enumeration of arbitrary live Components remains
available through Python; a later typed PCG enumeration mode may be added only
if it remains PCG-specific and does not become a generic World handle system.

Each Component result distinguishes:

- original versus generated, runtime, preview, or partition-local state;
- source Component versus local managing Component when partitioned;
- registered, active, generating, cleaning-up, and current native task facts;
- direct Graph interface and top Graph;
- live World and object-incarnation evidence;
- the canonical persistent Component source used for preparation.

Preparation may report factual external task activity, but retained execution
records, controls, messages, and captured data remain separate typed PCG
operations. Discovery does not manufacture a Target for an execution record or
adopt native tasks that Loomle did not admit.

## Managed Resources In Live Context

Managed-resource observation is available only through an exact normalized
selector plus source-bound typed-PCG World ticket plus matching canonical
source Target, or as retained execution-family evidence. The
persistent `pcg_component` Query does not expose this live collection.

The compatibility adapter uses public accessors:

- `AreManagedResourcesAccessible`;
- `ForEachManagedResource` or `GetManagedResources` as available;
- public concrete `UPCGManagedResource` APIs needed to project associated
  Actors, Components, ISM state, or soft targets.

It does not reflect UE 5.7 `GeneratedResources` or UE 5.8
`ManagedResourceContainer` storage directly.

An illustrative resource projection is:

```text
resource {
  nativeType: "/Script/PCG.PCGManagedActors",
  sourceComponent: "<typed-runtime-source-evidence>",
  managingComponent: "<typed-runtime-component-evidence>",
  objectType: "/Script/Engine.StaticMeshActor",
  objectPathEvidence: "<soft-or-live-path>",
  preview: false,
  transient: false,
  partitionLocal: false,
  loaded: true
}
```

The spelling is illustrative. A projected resource has no independent mutation
handle or durable StableRef. Object and soft paths are evidence only.

Every inventory reports at least:

```text
accessible
complete
resourceCount
projectedObjectCount
omissionReasons
```

`complete` is false when native resources are locked, a soft target is unloaded,
a partition cell is unavailable, bounds are hit, or a resource subtype cannot
be safely projected. A missing or inaccessible object is never reported as a
deletion.

## Typed PCG Frontend On The Shared Async Kernel

### Unfrozen protocol boundary

PCG execution requires a typed frontend outside SAL Query/Patch. It reuses a
shared internal kernel extracted from the existing Python execution lifecycle,
not a second independently invented continuation system. The common outer
contract provides:

```text
running | succeeded | failed | lost
opaque executionId
exact continuation with pollAfterMs
runtime-affine polling
bounded terminal retention and expiry
stateMayHaveChanged
no automatic replay after uncertainty or loss
```

Python keeps its current `run`/`poll` lifecycle, ids, safe-entry behavior,
logs, traceback, and legacy JSON-only result branch. Its optional `sal` annex
is a separate versioned extension. PCG uses namespaced ids and typed native
payloads. Neither
frontend accepts the other's execution id, and PCG never executes through a
Python script. No public PCG tool name, private route, or complete typed JSON
schema is fixed yet. Python fast terminal results keep omitting an exposed id;
PCG fast terminal success exposes its admitted id so bounded
messages/effects/inspection can be retained and queried. The shared kernel
supports both frontend policies without changing the released lifecycle or
legacy JSON-only branch.

An illustrative semantic start record is:

```json
{
  "operation": "start",
  "kind": "pcg.generate",
  "world": {
    "selector": { "worldKind": "editor" },
    "pcgWorldTicket": "<opaque-short-lived-ticket>"
  },
  "source": {
    "kind": "persistent_component",
    "target": {
      "domain": "pcg_component",
      "asset": "/Game/Maps/Forest.Forest",
      "actorId": "11111111-1111-1111-1111-111111111111",
      "source": "native",
      "id": "PCGComponent",
      "type": "/Script/PCG.PCGComponent"
    }
  },
  "options": {}
}
```

The first execution release rejects a runtime-only Component that cannot map
to a canonical persistent `pcg_component` Target. A future live-only source
would require an explicit family-level source variant plus independently approved
authority, attribution, cleanup, and persistence-handoff rules; it is not an
implicit runtime-handle fallback.

When start does not finish inside the frontend's short response window, it
returns the common `running` envelope and an exact frontend continuation. The
agent polls that continuation without replaying start. The shared kernel routes
the opaque id to the exact originating runtime; the PCG adapter revalidates
the recorded World binding, source, operation generation, and authorization on
every typed control request.

Semantically the frontend must support:

```text
start pcg.generate
poll exact continuation
request cancellation of the exact current source operation
read retained messages
list captured stacks and read captured data
start pcg.cleanup against the exact Component source
release or expire retained context
```

Cleanup creates its own async operation record. It is not a terminal status of
an earlier generation and does not claim that generation exclusively owned the
Component's managed resources.

These names are descriptive, not reserved operation strings. Schema discovery
for the typed frontend must not masquerade as a static
`pcg_execution` Domain card.

### Source forms and authority

Admission always has two independent inputs:

- one normalized typed-PCG World selector plus source-bound
  `pcgWorldTicket`;
- one canonical persistent `pcg_component` Target matching the source bound
  into that ticket.

A persistent Component Target does not implicitly select a live World. A
Python `sal.object()` projection, runtime object path, or typed observation
token cannot replace either required input or grant mutation/save authority.
An execution id does not grant source mutation authority later.

Direct execution of partition-local implementation Components is rejected in
the first release. Callers select the logical source Component and let native
PCG route hierarchical work. Instant Graph execution is a separate potential
mode with different ownership and is outside this design.

### Generate admission

Generate preflight must:

1. verify the ticket, private World epoch, normalized selector, World, Client,
   source binding, and subsystem;
2. resolve the exact live Component and reject template or stale objects;
3. verify registration, executable Graph interface, bounds, trigger/grid
   options, and native busy state;
4. reject incompatible Component Patch, Graph edit/save, cleanup, refresh, or
   inspection activity;
5. atomically acquire the execution-side source-Component and intersecting
   Level lease together with the top-Graph shared execution read lease before
   final preflight; if any lease is unavailable, release the whole set and
   reject; when Data View capture is requested, also acquire the stricter
   inspection lease required by the capture policy;
6. while those leases are held, revalidate Graph and Component state, then
   snapshot Graph identity, native generation facts, managed resources, and
   package dirty state;
7. create and publish an internal execution record before a fast native task
   can complete;
8. call the local native generation path and record its `FPCGTaskId`;
9. attach terminal and abort observation through supported native hooks;
10. retain the source/Level and shared Graph leases until native execution is
    terminal or the World binding is proven lost, and retain the inspection lease
    until terminal capture is finalized;
11. return a typed terminal result with the admitted PCG execution id if
    completion is observed inside the short response window; otherwise return
    the common `running` envelope with the same opaque id, admission facts, and
    exact poll continuation.

The force flag and hierarchical grid selection are explicit. Semantic grid
values map through the engine-version adapter; unknown integer sizes do not
round to a nearby grid.

If native scheduling returns `InvalidPCGTaskId`, the result reports that no task
was admitted and returns no execution id. It does not fabricate a record that
looks active. If transport or formatting fails after the native scheduling
boundary, the response must preserve `stateMayHaveChanged`-style uncertainty
and enough runtime context for safe recovery without replay.

The adapter uses a local Editor execution path. It does not invoke a networked
gameplay wrapper merely because one exists.

### Poll and bounded observation

Poll is a shared-kernel continuation operation keyed by the opaque execution
id. Bounded observation and any typed progress fields belong to the PCG
frontend. They do not run through ordinary `sal_query` against a Target.

Poll reads the Bridge execution record and native terminal observations. It
does not infer complete history only from `UPCGComponent::IsGenerating()`,
because UE clears current task fields after completion.

Poll reads the thread-safe shared-kernel record outside the Game Thread. It
never sleeps or spins on the Game Thread, reschedules the operation, or changes
native execution. While work is nonterminal, it returns the same execution id,
updated elapsed facts, and the exact next continuation.

Cancelling the MCP poll request stops only that request. It does not request
PCG cancellation. Client disconnect likewise does not automatically cancel
native work.

### Supersession-guarded source cancellation

Cancellation is an explicit typed PCG frontend control. Before calling native
cancellation, the shared kernel resolves the exact record/runtime and the typed
PCG adapter verifies:

- the record is still non-terminal;
- its exact private World binding and live Component still exist;
- the Component's current operation matches the admitted operation generation;
- no newer Loomle or external task has superseded it;
- the World and subsystem remain the recorded instances.

Only then may the adapter call the native execution-source cancellation path.
UE does not cancel by `FPCGTaskId`: cancellation can cover the source
Component's affected work and, for a partitioned original, its local Components.
The execution id is a guard/correlation record, not a native task-isolation
primitive. A stale id must never cancel a successor merely because both used
the same Component.

The immediate response means cancellation was requested, not that the native
task is aborted or rolled back. Native completion can win the race.
Cancellation can retain newly created/reused resources while deleting old
unused resources; `partialEffectsPossible` and fresh Component/resource/package
readback are mandatory when observable.

### Component-scoped cleanup

Cleanup belongs to the same typed PCG frontend but is admitted as a
new Component-scoped native operation. It is not deletion of a resource set
owned by a prior execution context.

The cleanup request supplies the normalized selector, a fresh valid
source-bound `pcgWorldTicket`, and the exact source Component Target. It must
also prove that no incompatible or newer generation state would make the
cleanup scope surprising. A prior execution id may be supplied only as
readback, attribution, or expected-generation evidence; it does not narrow the
native cleanup scope to that run.

The adapter calls the task-returning `UPCGComponent::CleanupLocal` path with an
explicit `removeComponents` choice and records the returned task in a new
execution context. It does not directly release managed-resource objects,
iterate projected Actors for deletion, or call immediate/delete-all helpers.

Native cleanup acts on the source Component's current managed-resource
lifecycle and can affect resources created, reused, or updated across several
generations and partition-local managing Components. If a successor generation
exists or attribution is not safe, cleanup rejects or requires a separately
designed explicit current-state cleanup policy; it never claims precise
per-execution deletion.

Cleanup is asynchronous, non-transactional, potentially package-dirtying, and
never saves. Its own execution id is observed through the same exact poll
continuation, cancellation, message, and effect mechanisms.

### Captured messages

Messages are copied into the execution record before native transient capture
is cleared. Each message can include severity, text, reporter Node, Graph, and
stack facts only when the native source proves them.

PCG messages are execution output, not Loomle protocol diagnostics. A native
`Completed` task can contain error-severity messages. Missing reporter or stack
identity is null rather than guessed.

### Inspection is selected before generation

Data View never enables inspection as a side effect of a later read. Admission
must request bounded data capture before the native task begins. The adapter:

1. acquires the per-top-Graph inspection lease;
2. clears stale Component inspection data;
3. enables the supported native inspection path;
4. schedules generation;
5. copies or safely retains bounded data after terminal observation;
6. releases the active inspection lease while retaining only the bounded
   execution record.

An execution that did not request data capture returns an explicit unavailable
result. The suggestion may start a new captured generation, but a data read
never silently reruns generation or reads another Component's last execution.

### Execution-local Node, Pin, and stack selectors

Data View uses Graph identity captured at admission:

- Node native `FName`;
- output direction;
- output Pin native label;
- exact native execution stack when more than one exists;
- captured Graph/interface and structure evidence.

Because `pcg_execution` is not a Target, these selectors are not SAL
Target-relative StableRefs. The typed frontend may reuse the same semantic path
components as the authored `pcg` Domain, but its structured or textual selector
encoding is not frozen.

Only output Pins are valid Data View sources. A later Graph edit cannot retarget
old captured data to a new Node or Pin with coincidentally similar display text.

Loops, subgraphs, partitioning, and repeated instantiation can produce multiple
`FPCGStack` values for one Node. The adapter never chooses the first stack. It
returns bounded execution-local stack ids; more than one candidate requires an
exact selection or fails ambiguous.

### Data projection and retention

One Data View response projects a bounded retained `FPCGDataCollection` and can
include:

- tagged-data and output-Pin facts;
- concrete PCG Data Class;
- point or native element count;
- tags;
- requested metadata attributes and native types;
- bounded element values;
- native point properties only for applicable data types;
- stack and pagination evidence;
- total-known and completeness facts.

Unsupported PCG Data subclasses report native Class and bounded schema evidence
with `projected: false`; they are not cast to Point Data.

Cursors bind execution id, stack, Node, Pin, projection, capture generation,
and recorded World-binding authorization. They cannot be reused across contexts.

UE 5.7 inspection exposes raw collection access in paths where UE 5.8 provides
newer inspection helpers and GC-visible storage. The compatibility layer must:

- copy values needed for ordinary projections;
- keep any retained `UPCGData` references visible to GC;
- impose per-execution and per-runtime memory bounds;
- release retained references on explicit release, expiry, memory pressure, or
  runtime loss;
- reject oversized capture rather than retaining unbounded output.

Inspection is Editor/build capability-gated. Absence fails before generation
when capture was requested; it does not silently downgrade to uninspected work.

## Effects And Resource Attribution

### Three effect dimensions

Runtime results distinguish at least:

1. **authored source effects**: requested changes to Graph, Component, or Level
   source-of-truth fields;
2. **derived World effects**: generated data, Actors, Components, instances,
   cache state, messages, and inspection data;
3. **persistence effects**: Package dirty transitions, external-Actor package
   involvement, generated serialized resources, and saved-state observations.

Generation has no authority to edit authored Graph defaults, Graph topology,
Component Graph binding, or Component Parameter overrides. That does not imply
that it leaves every Package clean. Native managed-resource state and generated
objects can be serialized or mark Level, Actor, external-Actor, or generated
packages dirty depending on editing mode and resource type.

Persistence effects are therefore explicit even though execution never saves.
Calling all generated output “non-serialized” would be false.

### Before/after observation

Generate, cancellation, and cleanup records capture best-available before/after
facts:

```text
effects {
  complete
  managedResourceInventoryAccessible
  sourceComponent
  managingComponents
  resourcesAdded
  resourcesRemoved
  actorsCreated
  actorsDestroyed
  componentsCreated
  componentsDestroyed
  packagesBecameDirty
  packagesBecameClean
  packagesSaved
  omissions
}
```

The spelling is illustrative. Effects use native live identity while available
and Class/path/package evidence after destruction. They are an attributed World
observation, not an authored diff, Undo plan, or assertion of exclusive
execution ownership.

`complete: false` is mandatory when partition cells are unloaded, resources are
inaccessible, native types cannot be projected, output bounds are exceeded, or
the runtime disappears. Incomplete observation cannot prove that a resource was
deleted or that cleanup removed everything.

### Attribution rules

- The source Component is the logical execution and cleanup source.
- A partition-local Component may be the actual managing Component.
- A generated UObject's outer and Package remain its physical ownership facts.
- An execution id identifies the operation that observed a delta; it does not
  create exclusive ownership of every object in that delta.
- Native PCG can reuse or update resources across generations. Reappearance in
  a later record supersedes any assumption that an earlier record can clean it
  independently.
- External side effects that cannot be bounded or attributed remain unavailable
  for a public execution mode.

## Transaction, Undo, Dirty State, And Save

Every asynchronous execution-family operation is non-transactional:

- there is no predictive dry-run sandbox for arbitrary World queries,
  streaming, randomness, managed-resource reuse, or callbacks;
- native scheduling escapes the initiating RPC;
- generated Actor and Component lifecycle is not completely represented by the
  UObject transaction buffer;
- cancellation and cleanup can destroy World-owned resources;
- rollback cannot reconstruct scheduler state or external side effects.

Execution responses must not contain authored Patch claims such as
`previousRevision`, `newRevision`, `atomic`, `rolledBack`, or `applied`. They
report admission, observation, terminal/lost state, effects, completeness, and
uncertainty.

No execution-family operation saves:

- the Graph asset;
- the source Component's owning Level or external-Actor Package;
- generated Actor Packages;
- any other dirty Package.

The adapter records dirty-package transitions when observable and preserves the
distinction between `dirty` and `saved`. It never invokes a broad “save all dirty
packages” helper. Persistence remains an independent `pcg` or `level` concern.

For PIE, Package save is always forbidden. In the Editor World, ordinary Level
save fails closed while PCG-managed derived projections remain in its owned
closure or their inventory is incomplete; it cannot selectively omit objects
inside an otherwise valid package. The caller first waits for execution to
settle and requests explicit Component-scoped cleanup, then Level recomputes
the native dirty closure. Persisting generated output would require a future
explicit bake/adopt authoring contract. Execution completion itself is never a
save decision.

## Concurrency And Isolation

### Component operation coordination

One live Component can have at most one incompatible Loomle-owned operation
transition at a time: generate, cleanup, cancellation, Component mutation
notification, or refresh boundary.

The coordinator is also consulted by `level`. An admitted nonterminal
execution blocks Level mutation/removal of its source Actor/Component and Level
save for the intersecting closure; a held Level mutation/save lease blocks new
execution admission. After terminal execution, remaining managed resources or
incomplete Component inventory continue to block source
removal/reconstruction until explicit Component-scoped cleanup settles. Level
save has a narrower guard: it blocks only when a registered provider proves
that managed projections intersect the candidate Level-owned dirty closure,
or cannot determine that intersection completely. Transient or
closure-external resources do not by themselves block Level save. No adapter
cancels or cleans implicitly.

Admission also reads native state. If non-Loomle work is active, Loomle reports
the Component busy and does not adopt, wait for, or cancel the external task.

The coordinator keys an operation by private World epoch, native Component identity,
operation generation, and native task id. A later task never overwrites the
record used to validate cancellation or cleanup of an earlier task.

### Graph edit and execution exclusion

Authored Graph edits can reconstruct Nodes and Pins while any scheduled work
may still read the Graph. The `pcg` adapter and execution adapter therefore
share an exclusion mechanism:

- Graph Patch and Graph save reject while any admitted nonterminal execution
  of that Graph is active, regardless of inspection capture;
- all execution admission rejects while Graph Patch or save is active;
- Component Graph-interface mutation rejects while affected execution or
  inspection is active;
- completed retained Data View can coexist with later edits only because it
  retains snapshot identity and bounded data rather than resolving live Nodes.

The exclusion key is native top Graph/interface identity, not display name.

The UE 5.8 reference Toolset warns about concurrent inspection of Actors sharing
one Graph. Until native isolation is proven, one Editor runtime permits at most
one active inspection-enabled execution per top Graph across all live World
bindings. Ordinary non-inspected generation is not serialized by this rule.
It may run concurrently with another native-allowed generation, but each
nonterminal execution still holds a shared read lease that blocks Graph
mutation/save.

### World binding, kernel, and PCG-adapter ownership

The private PCG World registry owns:

- native World epoch creation and expiry;
- exact typed-PCG selector normalization and live World validation;
- source Target to live Component-incarnation proof;
- source-bound `pcgWorldTicket` issue and stale rejection.

It does not enumerate generic Actors for SAL, create public live handles, or
control Editor/PIE/SIE state. Python owns those workflows; `sal.object()` uses
separate published read-only projectors and never issues a PCG ticket.

The shared async kernel owns only frontend-neutral lifecycle:

- namespaced opaque execution-id allocation and exact runtime routing;
- synchronized outer `running`/`succeeded`/`failed`/`lost` transitions;
- exact continuation formatting and thread-safe poll;
- bounded terminal retention, expiry, runtime loss, uncertainty, and no replay.

The typed PCG execution adapter owns:

- PCG admission using a kernel record;
- association with one exact private World epoch and Component incarnation;
- native task and terminal observation;
- cancellation and cleanup guards;
- typed messages, effects, completeness, native outcome, and inspection data;
- PCG-specific explicit release and GC-visible retained-data teardown.

Memory pressure may evict bounded heavy PCG inspection or Data View payloads
and return a typed `unavailable` result for those projections. It must not
delete or rewrite the shared outer execution record before the kernel's
documented terminal-retention policy permits expiry.

Neither the private World registry, shared kernel, nor PCG adapter owns or prolongs a
`UWorld`, Actor, or Component merely to keep a protocol object valid. Native
identities are weak and revalidated. Only the bounded `UPCGData` objects needed
for captured Data View may be strongly retained through a GC-visible owner.

Execution ids are opaque identifiers scoped by runtime/World binding and Client
visibility policy. Possession of an id alone is not source Target authorization.
A different Client cannot cancel work merely by learning a selector. Exact
cross-Client visibility and explicit execution-delegation policy is a release
gate. An execution id is typed-frontend result data, not a Target. Result Text
`handoff` may alias only a canonical related Target and never delegates
execution authority.

## Partitioning And Hierarchical Generation

Partitioning turns one logical source Component into native work involving
`APCGPartitionActor` objects and local `UPCGComponent` instances. Local
Components are live implementation objects, not durable `pcg_component`
Targets.

Rules:

- Python may enumerate local Components as ordinary live facts; typed PCG may
  report them only as source/managing-Component evidence inside a prepared
  World binding or execution record;
- direct generate or cleanup of a partition-local Component is rejected in the
  initial surface;
- the logical source Component remains the request source;
- native source-Component APIs route work to overlapping cells and local
  managing Components;
- effects record both logical source and observed local managing Components;
- cancellation validates the exact source operation and uses native propagation,
  never blind iteration over local Components;
- unloaded cells, runtime hash work, and soft resources force honest incomplete
  inventory;
- mutating the partition flag is excluded from Component Patch because native
  setters can clean resources and change execution-source registration.

Hierarchical grid selection passes through the version adapter. Results record
the requested semantic grid, normalized native grid size, execution bounds, and
observed task/cell facts. Parent task completion does not prove an unloaded cell
was generated.

Generated Actors can have distinct packages. Preview/transient resources may be
intentionally unsaved. No execution request broad-saves source or generated
packages.

## Editor And PIE Routing

Editor and PIE Worlds are different private World incarnations even when
derived from the same map:

| Fact | Editor World | In-process PIE/SIE World |
| --- | --- | --- |
| durable Component authored Query | through `pcg_component` source locator | maps back only when provenance is proven |
| live Component observation | Python or typed-PCG prepared source | Python or typed-PCG prepared source |
| generation and cleanup | supported after exact normalized selector, matching source-bound ticket, and same canonical source Target admission | supported after exact normalized selector, matching source-bound ticket, and same canonical source Target admission |
| authored Component Patch extension | Editor source only after its gate | forbidden |
| Package save | independent `level`/Asset workflow | forbidden |
| UObject identity | Editor object incarnation | PIE duplicate incarnation |

PIE admission verifies an active supported play/simulate context, exact World
context and instance discriminator, non-transition state, in-process Bridge
reachability, Component membership in that World, and the current private
World epoch. SIE is encoded as `worldKind: pie, playMode: simulate`, not as a
third World kind.

Every operation re-resolves World and Component. Objects captured before PIE
start/stop, travel, reconstruction, teardown, or map replacement are never
reused. World-binding expiry marks unfinished execution contexts `lost`; it does not
replay them in a replacement World. Already-terminal and newly-lost records
remain read-only retained evidence until their normal execution-record expiry,
but every operation that requires a live source or World binding fails stale.

This design does not start or stop PIE or SIE. An explicitly authorized short
Python call may request the transition; a later Python call confirms native
state, after which typed PCG prepare/discovery returns a fresh source-bound
ticket.

## UE 5.7 And UE 5.8 Compatibility

The public semantics remain version-independent. A small compiled adapter owns
native drift:

| Area | UE 5.7 | UE 5.8 | Semantic adapter rule |
| --- | --- | --- | --- |
| hierarchical generation grid | primary Component overloads use `EPCGHiGenGrid` | primary overloads use native grid size values with compatibility overloads | expose semantic all/unbounded/validated size, then convert exactly |
| managed-resource storage | `GeneratedResources` array | `FPCGManagedResourceContainer`; old storage deprecated | use public iteration/access APIs only |
| cleanup task observation | `CleanupLocal` returns the task; no equivalent public cleanup-task accessor | adds cleanup-task readback support | store the returned task at admission in both versions |
| inspection | older raw collection and executed-node access | newer inspection helpers, per-node stack access, generation tracking, and GC-visible storage | version-specific safe capture, one bounded semantic result |
| registration terminology | PCG Component-oriented internals | broader execution-source terminology | do not expose private registration API names |
| terminal native status | `Completed` or `Aborted` | `Completed` or `Aborted` | preserve native status separately from public lifecycle vocabulary |
| task id | `uint64` | `uint64` | if serialized, use lossless string or another exact representation chosen by protocol |
| instant Graph | no official spatial Toolset path used here | spatial Toolset exposes instant execution | unavailable until separately designed |
| official MCP PCG Toolset | absent | experimental | evidence only; never runtime fallback |

Additional requirements:

- compile-time branches include only public headers available in that engine;
- no deprecated UE 5.8 storage member becomes the UE 5.7 abstraction;
- inspection availability is capability-probed, not inferred solely from
  version number;
- fast completion is observable on both versions before native current-task
  fields clear;
- Graph, Component, World, effects, and Data View have the same semantic result
  fields across versions;
- unsupported behavior returns an explicit capability outcome, never a silent
  Toolset/Python downgrade.

## Diagnostics And Uncertainty

Exact diagnostic codes are protocol release gates, but implementations must
distinguish these semantic classes:

- no matching live World versus stale/expired `pcgWorldTicket`;
- persistent Component source locator missing versus live source mapping stale;
- unsupported World selector/play mode or multi-PIE routing;
- missing Graph interface or invalid generation/grid option;
- Component busy, Graph edit/inspection busy, or execution superseded;
- no native task admitted versus admission outcome unknown;
- cancellation requested versus native terminal completion/abort;
- execution id unknown, retained-context expired, or runtime lost;
- resource inventory inaccessible or incomplete;
- inspection unavailable, not requested, ambiguous stack, or capture too large;
- Component mutation unavailable because the Query-only gate remains active;
- persistence owner unknown or package effects incomplete.

Once native scheduling may have happened, transport cancellation or formatter
failure cannot be reported as “not applied.” The response and logs distinguish:

```text
not admitted
admission uncertain
admitted and observable
terminal observed
runtime lost
record expired
```

`stateMayHaveChanged` follows a closed outer invariant:

- validation, resolution, or lease rejection for a newly requested generate or
  cleanup operation before any native call reports `false`;
- poll and cancellation requests against an existing execution preserve that
  record's current value; a rejected cancellation never resets an earlier
  `true` merely because no new cancellation call reached native code;
- once generate, cancellation, or cleanup may have crossed its native call
  boundary, every `running`, `succeeded`, `failed`, or `lost` response for that
  operation conservatively reports `true`, even when exact effects are empty
  or incomplete;
- poll preserves the recorded operation value and never resets it merely
  because the current poll is read-only;
- `true` forbids automatic replay and requires source/World/resource
  readback after failure or loss.

The shared outer state remains `running`, `succeeded`, `failed`, or `lost`.
Typed PCG native-outcome and effect field spelling remains unfrozen. Recovery
suggestions must never replay after uncertain admission or runtime loss.

## Required Architecture

### Protocol and Client

`pcg_component` requires its closed Target/formatter/schema branches. Initial
Query/Patch Target-admissibility sets allow it in Query and reject it in Patch
before Bridge dispatch; enabling the later Component edit guard is a
coordinated protocol capability bump. Neither `pcg_execution` nor any World
selector/ticket enters either SAL request Target set.

Those Target sets are type/admissibility sets only. After admission, the
request's single `Target.domain` selects exactly one adapter. A set never
combines Targets, activates multiple Domains, or creates joint Query/Patch
authority.

The typed PCG frontend requires:

- a versioned structured public operation schema outside the Target Domain
  catalog; it is not another SAL document grammar;
- a private transport capability and Client routing path whose names remain
  unresolved;
- a shared outer result/continuation schema compatible with the existing
  Python `running`/`succeeded`/`failed`/`lost` contract;
- exact serialization of the canonical Component Target plus the typed-PCG
  World selector and opaque ticket fields;
- opaque execution-id routing back to the runtime that issued it;
- exact polling continuation plus separately bound Data View pagination;
- explicit lost-runtime behavior with no automatic replay;
- admission uncertainty and effects-completeness fields;
- capability discovery that does not pretend `pcg_execution` is a SAL module.

The shared kernel factors execution-record lifecycle, outer status,
continuation formatting, runtime-affine poll, retention/expiry, runtime loss,
uncertainty, and no-replay behavior out of the existing Python implementation.
It does not merge frontend ids or native semantics: Python keeps script
safe-entry/logging/traceback, its released lifecycle, and its legacy JSON-only
result branch, while the optional `sal` annex remains a separately versioned
extension and PCG keeps native task observation, typed effects, leases,
cancellation, cleanup, and inspection. Kernel extraction must pass the complete
existing Python regression and packaged-acceptance suite before PCG depends on
it.

### Bridge

The Bridge needs:

- a private PCG World registry keyed by native World identity and runtime epoch;
- a shared async execution kernel keyed by namespaced opaque context id;
- Python and typed PCG frontend adapters with strict id and payload isolation;
- a per-Component operation coordinator;
- a per-Graph inspection/edit coordinator shared with `pcg`;
- native task completion and runtime teardown hooks;
- bounded message/effect/Data View retention visible to GC where required;
- retention expiry, explicit release, and memory-pressure eviction of bounded
  heavy typed payloads without deleting the outer record before its retention
  policy permits;
- exact mapping from live Components to persistent source locators;
- persistence-owner and package-dirty observation;
- version-specific runtime compatibility implementations.

### Domain adapters

`pcg_component`, Python projection, and typed PCG runtime remain independent
adapters:

- Component resolves persistent authored slots and GraphInstance override
  facts;
- Python projectors map explicitly returned UObjects only to already published
  persistent SAL views;
- the typed PCG runtime resolves exact live Worlds, source Component
  incarnations, managed resources, and tickets through its private registry;
- no adapter embeds execution contexts or World tickets in a Target or
  StableRef;
- the typed PCG frontend consumes the persistent Component resolver without
  inheriting its mutation authority.

## Implementation Slices

These slices follow the family dependency order. Persistent
`pcg_component` identity and Query must pass before any typed-runtime registry,
World ticket, async-kernel extraction, or execution frontend is implemented.
The separately versioned family `sal.object()` projection slice also lands
before typed-runtime work begins. Typed execution is the final family stage.

### Slice 0: family Target prerequisite

- consume the family Phase 0 Target variants and three-way admission rules;
- accept canonical `pcg_component` in Query and canonical Result/related
  Targets while rejecting it in Patch before Bridge adapter dispatch;
- reject `world_session`, `pcg_execution`, World selectors, tickets, and
  execution ids from every SAL Target and StableRef position;
- use Domain-specific StableRef identity validation and fail-closed Bridge
  resolution/dispatch stubs;
- add protocol round-trip and old/new Client-Bridge mismatch fixtures;
- add no async kernel, World registry, typed schema, execution frontend,
  projection envelope, effect schema, save support, or public interface card.

### Slice 1: persistent Query-only Component

- canonical Component Target with `asset, actorId, source, id, type`;
- native/instance `FName` slot resolution and documented incarnation semantics;
- SCS owner-Generated-Class plus `VariableGuid` resolution;
- UCS Components remain live-only and have no persistent source Target;
- persistent Graph/Level/Component handoffs;
- Graph binding and certified persistent Parameter readback where identity is
  proven;
- no Component Patch and no execution.

### Slice 2: Component Parameter readback and mutation research

- descriptor-Guid declaration and effective-source Query;
- inherited external GraphInstance coverage;
- mutation guard prototype and failure injection;
- internal/external persistence-owner handoff;
- remain Query-only publicly until every Patch gate passes.

### Slice 3: typed-runtime foundation and source preparation

- begin only after the separate family `sal.object()` projection slice has
  preserved ordinary Python behavior and passed its own compatibility gates;
- extract the shared async kernel from the existing Python lifecycle without
  changing ordinary Python behavior, ids, or acceptance results;
- decide and version the typed structured PCG operation schema, public tool
  surface, and private route;
- implement the private PCG World epoch/ticket registry and PCG frontend
  adapter;
- normalize typed-PCG `worldKind`/`playMode` selectors;
- support Editor World and one exact in-process PIE/SIE path;
- prove source Target to exact live Component incarnation and issue a
  source-bound opaque `pcgWorldTicket` with stale rejection;
- expose live managed-resource inventory only through typed source
  preparation;
- reuse kernel runtime affinity, outer statuses, exact continuation, poll,
  loss, retention, uncertainty, and no-replay rules;
- expose no PCG execution operation yet.

### Slice 4: generate, continuation/poll, and messages

- versioned typed PCG start/poll schema on the shared outer envelope;
- required normalized World selector, matching source-bound World ticket, and
  canonical persistent Component source; no runtime-only source fallback;
- local generation admission;
- atomic source/Level-intersection and top-Graph shared lease admission for
  every run, including non-inspected generation;
- record registration before native scheduling;
- lossless task id and terminal callback capture;
- exact continuation/poll and bounded non-Game-Thread observation;
- messages and admission uncertainty;
- Editor World first, then exact PIE parity;
- no inspection in the first sub-slice.

### Slice 5: cancellation, cleanup, and effects

- execution-id supersession guard around native source-scoped cancellation;
- Component-scoped `CleanupLocal` as a new context;
- successor-generation and stale-context rejection;
- before/after managed resources and dirty-package observations;
- source/managing Component attribution;
- incomplete-result handling;
- no direct resource deletion and no save.

### Slice 6: inspection and Data View

- capture choice before generation;
- per-top-Graph inspection lease;
- safe UE 5.7 and UE 5.8 retention;
- execution-local structured Node/Pin/stack selectors;
- exact stack ambiguity handling;
- attribute projection, pagination, and memory bounds;
- later Graph edit isolation.

### Slice 7: partition and World hardening

- hierarchical grid conversion;
- partition source/local Component aggregation;
- unloaded-cell completeness;
- Level Instance mapping gates;
- supported PIE/SIE and multi-World matrix;
- cross-World concurrency and runtime-loss tests.

Each slice may land internally only after its static catalog or request schema,
native automation, Client/Bridge parity, packaged acceptance, and both-engine
tests pass. The Target-backed `level`, `pcg`, and Query-only `pcg_component`
surfaces still ship as one coordinated nine-Domain SAL catalog release. The
Python projection annex and typed PCG frontend are separately versioned
capability releases.

## Test Requirements

### Protocol and identity

- `world_session` and `pcg_execution` are not `Target.domain` values and are
  rejected in every Query/Patch Target position;
- one request fixture never admits more than one active Target and Domain;
- `pcg_component` passes Query admission but fails Patch admission before
  Bridge dispatch in the initial capability;
- canonical Component Target accepts only
  `domain, asset, actorId, source, id, type` in canonical order;
- World selector and `pcgWorldTicket` are rejected from every SAL Target,
  StableRef, handoff, and Object Text identity position;
- typed-PCG discovery selector is never formatted as a partial Target;
- execution ids cannot enter SAL StableRefs, related Targets, Query Targets,
  Patch Targets, or any other Target input;
- the chosen typed PCG schema and shared outer envelope round-trip exactly;
- Python and PCG continuation ids are namespaced and rejected by the other
  frontend;
- public/private capability mismatch fails closed;
- task ids and cursors round-trip without numeric precision loss.
- shared outer `running`/`succeeded`/`failed`/`lost`, continuation,
  `stateMayHaveChanged`, retention, expiry, and lost-runtime fixtures pass for
  both Python and typed PCG frontends without merging their typed payloads;
- the complete ordinary Python tool, Bridge, automation, Skill, and packaged
  acceptance suite remains unchanged after shared-kernel extraction, while the
  separately versioned `sal.object()` extension passes its own compatibility
  tests.

### Component source identity

- `APCGVolume` built-in Component;
- arbitrary native Actor with one and with multiple PCG Components;
- native Component resolves through Actor-scoped unique `FName`;
- serialized instance Component resolves through Actor-scoped unique `FName`;
- Blueprint/SCS Component resolves through qualified owner Generated Class Path
  plus `USCS_Node::VariableGuid`, including inherited declaration cases;
- SCS variable rename preserves the Guid locator;
- Generated Class rename/redirect and node recreation fail or canonicalize
  according to the approved exact mapping policy;
- User Construction Script Components never canonicalize persistently;
- other runtime construction and runtime-created Components remain live-only;
- partition-local and preview Components remain live-only;
- duplicate Actor labels do not matter;
- duplicate native/instance Component `FName` values fail canonicalization;
- duplicate or ambiguous SCS node-to-instance mappings fail canonicalization;
- reconstruction invalidates the old source-bound ticket while the persistent
  source-aware Target continues to resolve;
- native/instance delete and recreate with the same `FName` is tested as logical
  slot reuse, not falsely promised as a distinguishable UObject incarnation;
- SCS replacement with a different `VariableGuid` does not alias the old
  locator;
- save/unload/reload preserves the Target when its native/instance slot or SCS
  declaration persists;
- Level Instance source versus live instance identity is explicit.

### Component Query and future mutation

- direct Graph interface versus top Graph;
- inherited Graph default;
- inherited external GraphInstance value;
- local override bit/value and effective source;
- descriptor Guid survives rename;
- removed Parameter invalidates the reference;
- live resources are absent from persistent Component Query;
- baseline exact schema advertises no Patch;
- future mutation failure starts no task and restores value, override bit,
  dirty state, and Undo when that extension is enabled;
- Graph assignment stays unavailable until refresh/cleanup isolation passes;
- external Actor persistence/source-control failure remains an explicit gate.

### World routing and live resources

- Editor discovery never returns PIE;
- PIE/SIE discovery never returns Editor World;
- SIE normalizes to `worldKind: pie, playMode: simulate`; it is never treated as
  a distinct `EWorldType`;
- transition state fails closed;
- supported native PIE instance discriminator is exact;
- unsupported Standalone/New Process/multi-client routing rejects;
- map switch, PIE stop, source reconstruction, and Editor restart expire the
  old ticket/private epoch;
- stale ticket never binds a replacement World or Component;
- no Query starts PIE, loads a map, or streams a cell;
- no SAL Query/Patch or typed PCG discovery starts/stops PIE or SIE,
  possess/ejects, changes selection/camera, or invokes Python implicitly;
- an explicitly authorized short Python start/stop request returns before the
  transition, a later Python request confirms native state, and only then does
  typed PCG discovery issue a fresh source-bound ticket;
- managed-resource access uses only public adapter APIs;
- inaccessible/unloaded/bounded inventories return incomplete rather than false
  deletion;
- managed-resource observation requires an exact selector/ticket/source triple
  or retained execution record.

### Execution lifecycle

- invalid selector/ticket, private epoch, Component, Graph, subsystem, and
  option preflight; private-epoch assertions are Bridge-internal and no epoch
  identifier enters a public result;
- runtime-only source without a canonical persistent `pcg_component` Target is
  rejected before scheduling;
- every run atomically acquires reciprocal source/Level and Graph leases;
  non-inspected generation blocks intersecting Level mutation/save and Graph
  edit/save, and those authored leases block execution admission;
- a nonterminal start returns the exact typed-frontend continuation and poll
  never replays start;
- a new generate/cleanup request rejected before its native boundary reports
  `stateMayHaveChanged: false`; poll and rejected cancellation preserve the
  existing record value; crossing any generate/cancel/cleanup native boundary
  reports `true`, and no `true` result invites replay;
- common outer failure/loss preserves conservative `stateMayHaveChanged`, while
  the typed payload preserves native `Completed`/`Aborted`, cancellation
  request, effects, and completeness facts without rewriting them;
- `InvalidPCGTaskId` returns no execution id;
- fast native completion cannot beat record registration;
- native Completed and Aborted paths are preserved independently of public
  lifecycle wording;
- error-severity PCG message does not rewrite native Completed status;
- repeated poll never reschedules;
- poll remains responsive and never blocks the Game Thread;
- transport poll cancellation does not cancel PCG;
- supersession-guarded source cancellation, partition-local propagation, and
  completion-wins race;
- stale execution id cannot cancel a successor;
- external/native busy work is observed but not adopted;
- Client disconnect does not imply native cancellation;
- World teardown produces lost context, not replay;
- terminal outer-record retention, explicit release, expiry, and
  memory-pressure eviction of heavy typed payloads without premature outer
  record deletion.

### Cleanup and persistence effects

- cleanup is admitted against a normalized selector, fresh exact ticket, and
  Component Target, not a resource list attributed exclusively to one earlier
  operation record;
- cleanup returns a new retained context when native scheduling succeeds;
- both explicit `removeComponents` choices are tested;
- active or successor generation prevents stale cleanup;
- cleanup uses the native task-returning path;
- no direct managed-resource release or projected-object deletion path;
- source and partition-local managing Components are attributed separately;
- resources reused across generations are not attributed as exclusively owned;
- package dirty transitions are observed where native behavior exposes them;
- execution never saves any dirty Package;
- preview/transient fixture may assert no dirty transition, but that assertion
  is not a universal runtime invariant;
- bounds and unloaded cells force incomplete effects.

### Inspection and Data View

- capture disabled before execution produces an explicit unavailable result;
- capture enabled before native scheduling;
- stale Component inspection data is cleared;
- exact captured Node and output Pin, missing Node/Pin, and no-data output;
- one stack and multiple loop/subgraph/partition stacks;
- ambiguous stack requires exact selection;
- selector is execution-local and never parsed as a Target StableRef;
- attribute/property projection and non-Point subclasses;
- cursor binding, result bounds, and retained-memory bounds;
- Graph edit after completion cannot retarget old data;
- simultaneous captured executions sharing one Graph reject;
- non-inspected generation is not unnecessarily serialized;
- retained UObject references survive GC only for retention lifetime and release
  on expiry/loss.

### UE 5.7 and UE 5.8

The same semantic suite runs against both official engines:

- incremental arm64 compile;
- canonical Target, typed PCG frontend, and shared-envelope fixtures;
- persistent Component Query plus typed-PCG source preparation;
- generate, continuation/poll, cancel, cleanup, and effects;
- public managed-resource projection;
- package dirty observation without save;
- inspection adapter and GC retention;
- partition, grid, and Editor/PIE routing.

Version-only tests pin older versus newer grid overloads, managed-resource
container drift, cleanup task readback, raw versus GC-visible inspection, and
capability guards.

## Acceptance Requirements

This design can leave planned status only when:

- `pcg_component` has the exact canonical Target shape defined here, including
  the source-aware locator; no live World Target is introduced;
- `pcg_execution` is absent from Target, StableRef, related-Target, Query/Patch
  admissibility, and static Domain-card sets;
- all generic Editor/PIE/SIE controls and live observations are explicit Python
  workflows followed by fresh native-state readback; `sal.object()` may return
  only Bridge-validated persistent projections;
- the typed PCG frontend has a separately approved versioned public schema and
  transport contract built on the regression-tested shared async envelope;
- shared-kernel extraction preserves the released Python lifecycle and legacy
  JSON-only result behavior; the optional `sal` annex passes its own versioned
  compatibility gates;
- arbitrary `UPCGComponent` is the native root and `APCGVolume` is only one
  owner case;
- native/instance persistent identity is explicitly an actor-scoped serialized
  `FName` slot, SCS identity is owner Generated Class Path plus VariableGuid,
  UCS is live-only, and none promises impossible live-incarnation continuity;
- private World epoch, selector normalization, source mapping, ticket issue,
  and stale-ticket rejection are exact;
- persistent Component baseline remains Query-only until the native edit guard
  proves normal Patch transaction and rollback semantics;
- live managed resources are observed only through a normalized selector,
  matching source-bound ticket, and same canonical source Target; a retained
  execution id reads only already retained evidence and never reopens live
  inventory;
- generate returns an opaque task-bound context id before fast completion can
  be lost;
- poll, cancel, cleanup, messages, inspection, and Data View are typed PCG
  frontend operations or results on the shared async lifecycle;
- cancellation is source-scoped, guarded against superseding work by the
  execution id, and reports only a request until terminal native observation;
- cleanup is Component-scoped native lifecycle, never per-execution ownership
  or direct projected-resource deletion;
- effects distinguish authored source, derived World, and persistence/dirty
  consequences with honest completeness;
- runtime may dirty Packages, reports that fact, and never performs or implies
  save;
- Data View is requested before generation, Graph-isolated, bounded, and
  stack-exact;
- partition and external-Actor attribution/persistence gaps remain explicit;
- Graph edit, Component edit, execution, and inspection exclusion tests pass;
- targeted packaged Client/Bridge acceptance passes on both UE 5.7 and UE 5.8.
