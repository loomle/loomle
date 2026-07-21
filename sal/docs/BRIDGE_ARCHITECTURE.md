# SAL Bridge Architecture

## Status

This document defines the Unreal Engine Bridge for the implemented SAL SDK
contract. The Bridge exposes only `sal.query` and `sal.patch`; the former LGL
runtime and its grouped JSON model have been removed.

The Bridge migration changes no SAL Text syntax. The normative public contract
remains:

- [`LANGUAGE_CORE.md`](LANGUAGE_CORE.md) for SAL Text and normalized objects;
- [`SDK_DESIGN.md`](SDK_DESIGN.md) for the SDK and executor boundary;
- [`DIAGNOSTICS.md`](DIAGNOSTICS.md) for diagnostics;
- [`../../interfaces/`](../../interfaces/) for the injected UE interface catalog;
- [`domains/`](domains/) for UE semantics.

## Intent

The Bridge is the UE-backed `SalExecutor`. It receives schema-valid normalized
JSON, resolves it against live Editor state, executes UE-native reads and
edits, and returns schema-valid ordered Object Text.

The SDK owns SAL Text parsing, pure normalization, formatting, static
`sal.schema(module?)`, and source spans. The Bridge owns every decision that
requires UE state: target identity, available interfaces, fields, Palette,
Graph Schema behavior, Reflection, transactions, reconstruction, compilation,
save behavior, and final readback.

SAL does not replace UE's object model. Bridge code should expose UE state
through SAL structure while preserving native paths, ids, types, field names,
enum values, and value text.

## RPC Boundary

The Bridge exposes two normalized-object RPC methods:

```txt
sal.query
sal.patch
```

Both receive one envelope:

```json
{
  "object": {
    "kind": "query"
  }
}
```

Both return an `ObjectResult` directly:

```json
{
  "object": {
    "statements": []
  },
  "diagnostics": []
}
```

`sal.patch` returns `MutationResult`, which extends the same object and
diagnostic shape with execution fields. The Bridge never receives or returns
SAL Text. It does not expose a Bridge RPC for static `sal.schema`; static cards
remain SDK-owned. Exact `with schema` is an ordinary live Query.

The RPC handler reuses Loomle's existing game-thread dispatch. No SAL service
may access Editor UObjects from the pipe worker thread.

The private transport assigns every invocation an independent cancellation
token. MCP cancellation for `sal.query` sends `rpc.cancel` over the same live
connection; the pipe reader handles that control message synchronously while
ordinary work remains queued or running. Registration and disconnect
tombstones close races where cancellation arrives before the invocation
worker. Query providers check the shared state at bounded checkpoints.
`sal.patch` does not accept cooperative cancellation after dispatch because a
caller must not receive "cancelled" while an authored mutation may already be
applying. Transport request ids and cancellation tokens are process-wide
unique, and one request's domain error does not tear down other requests on the
shared connection.

## End-To-End Flow

```txt
SAL Text
  -> SDK parse and pure normalization
  -> normalized Query or Patch JSON
  -> sal.query or sal.patch
  -> Bridge envelope and schema validation
  -> recursive Target resolution
  -> interface composition
  -> one interface-owned Query handler or atomic Patch planner
  -> UE-native execution
  -> ordered ObjectText and diagnostics
  -> Bridge result validation
  -> normalized ObjectResult JSON
  -> SDK formatting
```

The Bridge repeats normalized-object validation because direct or stale RPC
callers may bypass the SDK. This does not move Text parsing into C++.

## Normalized Model

The C++ core decodes the shared schema into one typed internal model matching
`schema/sal-object.schema.json`:

- `Target`: alias plus `Call | Name`;
- `Expr`: scalar, name, reference, call, array, or inline object;
- `Query`: target, one operation, and optional clauses;
- `Patch`: target, dry-run flag, and one ordered statement list;
- `ObjectText`: one ordered list of Binding, Edge, and Comment;
- `ObjectResult`: ordinary result or mutation result.

Domain services do not hand-parse the RPC envelope or invent private request
wrappers. The codec must preserve every Patch and result statement in input
order.

The JSON Schema remains the cross-language source of truth. The first C++
implementation may use hand-written typed decoding, but structural checks stay
central and are covered by the same valid and invalid fixtures as the SDK.
Adapters receive decoded values only after the core has verified required
fields, union discriminators, reference shapes, and ordered alias safety.

## Target Resolution

`Target` has no public domain field and no domain-specific union. The Bridge
resolves `Target.value` recursively.

Examples:

```txt
asset
asset(path: "/Game/BP_Door.BP_Door")
blueprint(asset: "/Game/BP_Door.BP_Door", id: "...")
class(path: "/Game/BP_Door.BP_Door_C")
graph(asset: blueprint(...), id: "...")
```

The target resolver dispatches only by the structural root Name or Call
callee. Each constructor branch:

1. validates locator fields for that Call;
2. recursively resolves nested owner Calls;
3. loads or finds the native UE object;
4. verifies every supplied native identity;
5. records the exact owner chain and persistent asset owner;
6. returns the native object and interfaces it can compose.

Resolution never treats display text, `name`, `type`, or another convenient
field as fallback identity. A scoped id is resolved only inside the owner
established by the complete target.

Each resolver consumes only its declared locator fields and ignores descriptive
state carried by a complete returned binding. Copying full Object Text back
into a later request therefore remains valid without turning status, layout,
native type, or other readback fields into implicit assertions.

The initial resolvers are:

| Target value | Native result |
| --- | --- |
| root `asset` Name | Asset Registry collection scope |
| `asset(...)` | exact `FAssetData`, UObject when loading is required, and Package |
| `blueprint(...)` | `UBlueprint` with Asset Path and BlueprintGuid verification |
| `class(...)` | exact `UClass` |
| `graph(...)` | exact `UEdGraph` plus its resolved asset-backed owner |

GraphGuid, NodeGuid, and PinId remain owner-scoped. BlueprintGuid verifies a
Blueprint loaded by Asset Path; it is not a project-wide lookup key.

## Interface Composition

Public interface modules organize discoverable behavior; they are not target
routers. After target resolution, the Bridge derives active interfaces from
the real UObject Class, inheritance, owner, Graph Schema, and current state.

For example, UE 5.7 defines `UWidgetBlueprint` through
`UBaseWidgetBlueprint`, `UUserWidgetBlueprint`, and `UBlueprint`. One resolved
`blueprint(...)` target therefore composes Blueprint and Widget behavior. SAL
does not need a second Widget target or a public domain selector.

Each active interface contributes one closed capability surface for the
resolved target:

- Query operation handlers;
- supported `where` fields and operators;
- supported `with` details;
- order keys and pagination behavior;
- Patch statement handlers and operation schemas;
- direct Palette providers;
- exact-object dynamic schema providers.

Every interface-owned normalized operation maps to exactly one interface
handler. Explicit shared relationships defined by SAL Core, currently
`references`, route through one shared service after Target resolution and
before interface-specific dispatch. This does not create a public Reference
domain or interface module. A more specific interface may explicitly replace a
general operation, such as a WidgetBlueprint's combined `summary` and Palette.
Dispatch is explicit in the SAL module and never depends on map or module
registration order.

An interface may represent this surface as a table or as closed shared
predicates beside its handlers. In either form, clause validation, execution,
`with schema`, and diagnostic `supported` lists must derive from the same
operation definitions rather than four independent guesses.

## UE Backends

An interface is a stable agent-facing operation surface. A backend is the
UE-family-specific implementation selected after native resolution.

Graph is the important case. The public `graph` interface defines Graph,
Node, Pin, Edge, flow, Palette, and Patch behavior. A resolved Graph selects a
backend from its owner, `UEdGraphSchema`, and native Node family. The Blueprint
K2 backend may support Exec and data flow; a future Material or PCG backend may
compose a different subset without changing Target, Query, Patch, reference,
or result contracts.

Shared services may contain genuinely reusable mechanics such as:

- Asset Path and Package resolution;
- Reflection value export and import;
- ordered Object Text construction;
- alias generation and scoped reference resolution;
- factual declaration resolution, native use-site extraction, and state-bound
  reference pagination;
- transaction and dirty-state helpers;
- compile and save result handling.

A service must remain in its UE backend when it relies on K2 Action Menu,
Blueprint spawners, SCS, WidgetTree, Material expressions, PCG settings, or
another family-specific API.

## Query Pipeline

Every Query follows one path:

```txt
decode and validate Query
  -> resolve Target
  -> resolve active interfaces
  -> select operation handler
  -> validate clauses against that operation
  -> resolve scoped ids and names
  -> read live UE state
  -> build ordered ObjectText
  -> validate ObjectResult
```

Plural operations enumerate or search. Singular operations resolve exact
current names. Typed stable references resolve exact native ids inside the
target. Relationship operations use their explicit target and depth.

`with schema` is valid only where the selected interface declares it. It
describes the primary exact subject of the Query. It does not recursively add
schema for child Pins, Properties, Widgets, or other returned context.

Pagination stays in `Result.page`. A cursor is opaque to the SDK and must bind
to the operation, target, ordering, and filters that produced it.

Composition combines discovery surfaces, not unrelated mutation engines. One
authored Patch is owned by one interface planner so preflight, apply, and
rollback remain atomic. A composed target may therefore require separate
Blueprint and Widget authored Patches; terminal compile/save remains a third,
independent Blueprint request. The Bridge rejects a mixed-interface Patch
instead of splitting it into partially committed mutations.

## Ordered Object Text

All interfaces return one `ObjectText.statements` array. A shared result
builder appends Binding, Edge, and Comment statements in reading order and
enforces:

1. the result starts with an empty alias scope;
2. every owner binding precedes member bindings that use it;
3. every Edge follows both endpoint bindings;
4. every local alias and binding target is unique;
5. comments stay immediately after the statement they explain;
6. the result declares compact target or owner bindings instead of inheriting
   request aliases;
7. result bindings contain only identity and state required by this result.

There are no `GraphResult`, `PaletteResult`, `AssetResult`, parallel object
arrays, or comment arrays. Query and Patch readback use the same builder.

## Scoped Reference Resolution

One request-local resolver owns all stable, local, and member references.

For Query it resolves typed ids inside the target scope. For Patch it also
tracks each alias through ordered states:

```txt
declared -> materialized -> removed
```

Palette-backed bindings remain unmaterialized until consumed by `add`,
`insert`, `wrap`, `replace`, or the owning operation. `invoke` outputs become
materialized at that statement. Later statements resolve against the same
provisional state.

Reference resolution is separate from object mutation. Domain services ask the
resolver for a typed native subject and receive a resolution diagnostic rather
than reinterpreting JSON references independently.

## Patch Pipeline

Every Patch follows the shared Mutation Dry Run Contract:

```txt
decode and validate ordered Patch
  -> resolve Target
  -> select one interface-owned planner
  -> resolve existing objects and Palette identities
  -> advance aliases and build provisional state in statement order
  -> validate every field, operation, relationship, and native side effect
  -> build one complete plan
  -> verify live state still matches the plan base
  -> stop for dry run, or apply as one mutation
  -> read back actual current state
  -> validate MutationResult
```

Dry run and apply share decode, resolution, validation, provisional state, and
plan construction. Dry run never inserts provisional objects into returned
Object Text as if they existed.

Authored edits use one transaction and backend-specific notifications,
reconstruction, propagation, and dirtying. If a backend cannot predict a
required generated identity or the complete effects needed for safe planning,
it rejects the Patch before mutation. It does not guess or silently reduce the
operation.

Every interface builds mutation execution fields through `SalRuntime`, which
delegates the envelope to the shared `LoomleMutation::BuildMutationResult`
utility. Interfaces supply only ordered readback, diagnostics, plan, resolved
references, diff, and their native apply behavior.

Terminal compile and save follow their interface's explicit ordering and
rollback rules. They are not folded into an authored-edit transaction when the
domain document defines them as a separate request.

## Dynamic Schema

Static interface cards remain top-level Interfaces resources. Live
`with schema` uses the exact resolved target or subject and the same interface
capability definitions used for execution.

The selected schema provider returns ordinary Object Text followed by a
structured multi-line Comment describing only executable current behavior:

- accepted Query operations or exact fields;
- native type and value text;
- writable/resettable fields and constraints;
- direct Patch statements;
- available `invoke` Operations, parameters, outputs, and effects;
- Palette constructor arguments and determinable future members.

Schema discovery must not advertise an operation that the current Bridge
cannot plan and execute.

## Diagnostics

The Bridge produces `language.*` only for malformed normalized JSON or an
invalid result at the RPC boundary. After decoding:

- `capability.*` means the resolved target does not expose a language-valid
  operation or clause;
- `resolution.*` means a target, id, name, reference, or Palette entry is
  missing or ambiguous;
- `validation.*` means the resolved operation is illegal in current UE state.

All codes must exist in the shared diagnostic catalog. Diagnostics should use
normalized JSON paths and include one copyable next action when it is known.
Adapters use one shared diagnostic builder and never return legacy Bridge error
objects as SAL results.

## Code Shape

The target implementation uses SAL names throughout:

```txt
Private/Sal/
  SalModule.*
  SalModel.*
  SalJson.*
  SalDiagnostics.*
  SalObjectBuilder.*
  SalTargetResolver.*
  SalRuntime.*

Private/Sal/Asset/
Private/Sal/Blueprint/
Private/Sal/Class/
Private/Sal/Graph/
Private/Sal/Widget/
```

Request-local reference resolution, capability checks, and mutation planning
remain inside the owning interface while they depend on its provisional native
state; they do not require empty public wrapper classes. Public C++ classes and
namespaces use `FSal*` and `Loomle::Sal`.

## Legacy Migration

The migration does not maintain a compatibility adapter for the old LGL JSON
model. The old surface is not part of the public SAL SDK and preserving it
would keep two conflicting sources of truth.

Reusable UE mechanics may be extracted from legacy code only after removing
old JSON assumptions. Likely reusable areas include Asset Registry filtering,
Blueprint Graph lookup, Action Menu and spawner enumeration, pin conversion,
and validated Graph application. The following are replaced rather than
ported:

- `target.domain` dispatch and `ILglDomainAdapter`;
- `find` objects and legacy query capabilities;
- grouped Graph, Asset, and Palette result payloads;
- old binding/operation parallel arrays;
- `FLglSchemaValidator` and old request-specific JSON readers;
- public `lgl.query` and `lgl.patch` registration.

The old RPC methods are removed when `sal.query` and `sal.patch` compile and
the normalized contract tests pass. No dual-mode runtime is required.

## Implemented Slices

The implementation is organized as vertical executable slices:

1. core JSON model, validation, Target resolution, capability composition,
   ordered result building, diagnostics, and RPC cutover;
2. Asset Registry discovery and exact asset ownership;
3. Blueprint-owned Graph summary, Node and Pin reads, traversal, Palette, and
   dynamic schema;
4. Graph Patch planning and application;
5. Blueprint structure and finalization;
6. Widget composition and tree editing;
7. Class Reflection and durable Defaults;
8. factual local and project Reference queries;
9. cross-interface audit and removal of legacy LGL code.

Every slice shares central normalized-object and result validation, and builds
against UE 5.7 as one plugin module.

## Acceptance

The Bridge phase is complete when:

- `sal.query` and `sal.patch` accept the SDK's normalized schema without a
  public domain field;
- every active static interface operation is either executable for a matching
  target or absent from the active interface set;
- nested owner locators and scoped ids resolve without fallback guessing;
- all Query and Patch responses use self-contained ordered Object Text;
- exact `with schema` describes the same capabilities the Bridge executes;
- dry run and apply share one plan path;
- old `lgl.*`, `Private/Lgl`, grouped results, and old protocol validators are
  removed;
- the plugin builds cleanly against the supported UE 5.7 toolchain.
