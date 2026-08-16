# PCG Domain

## Status

PCG is not part of the latest externally released Loomle interface catalog;
that product catalog still contains six Domains. This document is the planned
design and branch implementation contract for the authored, asset-backed SAL
Domain named `pcg`.

This document is one part of the Scene/PCG Domain family. The family-level
ownership rules are defined in `SCENE_PCG_DOMAIN_FAMILY_DESIGN.md`; persistent
Level authoring is defined in `LEVEL_DOMAIN_DESIGN.md`; component configuration
and runtime execution are defined in `PCG_RUNTIME_DOMAIN_DESIGN.md`. If a local
operation appears possible through more than one surface, those ownership
documents decide which adapter owns it.

> Review status: the Target, identity, and implemented read-only Query shape are
> the frozen `pcg` contribution to the coordinated nine-Domain Query-only
> release. Exact Palette encodings, certified Settings fields, mutation/save
> effects, and execution protocol naming remain gates only for later capability
> releases.

> Current implementation slice: the branch contains a nine-Domain Query-only
> release candidate on Client-Bridge protocol v6, including the static `pcg`
> card and offline schema. Its implemented surface remains limited to Target
> open/canonicalization, `target`, `summary`, `nodes`, exact Node and Pin reads,
> exact schema, persisted layout, and incident Edge projection. `context`,
> `data flow`, Parameters, Palette, mutation, and execution remain deferred.

> Current validation snapshot: the branch-local Query-only RC passes the PCG
> Automation group 7/7 on both UE 5.7 and UE 5.8, the related Level and family
> Phase 0 suites, both engine builds, and packaged end-to-end acceptance. The
> protocol-v6 cards and nine-Domain catalog are formed in the branch but have
> not been externally published.

The next public milestone externally publishes that certified Query surface
together with Query-only `level` and `pcg_component`. `pcg` is admitted only by
`QueryTarget` and is rejected as a `PatchTarget` before Bridge dispatch.
Authored mutation, save, execution, and the broader PCG related-Target surface
are not gates for this first card.

The baseline boundary and syntax direction are confirmed for design work:

- PCG uses the existing `sal_query`, `sal_patch`, and `sal_schema` tools;
- the Domain owns authored, asset-backed `UPCGGraph` state;
- Node identity uses its serialized UObject `FName`;
- Pin identity uses Node, native input/output direction, and Pin label;
- the existing Query and Patch statement grammar is reused;
- Settings preserve the native `SettingsInterface` ownership shape and expose
  mutation only through an exact Class-and-member capability allowlist;
- baseline `connect` accepts only a directly compatible connection into an
  unoccupied input; filter/conversion insertion is a capability-gated later
  extension;
- Graph Parameter identity is reserved as its native descriptor Guid, while
  Parameter Query is deferred beyond the current read-only slice and every
  Graph-default Parameter mutation in `pcg` is deferred to a future multi-Target
  migration contract outside ordinary SAL Query/Patch; this does not define the
  separate `pcg_component` override boundary;
- package persistence is a separate terminal-only `save` Patch.

This planned document is not the public card. The branch RC now contains
`interfaces/pcg.md`, offline schema registration, the nine-Domain static
catalog entry, and `PatchTarget` exclusion, but none is externally published.
The branch-local UE 5.7/5.8 Query, related Level/Phase 0, build, and packaged
end-to-end gates have passed. Final release-archive validation, Windows
validation, and coordinated promotion remain publication gates. Palette
encodings, writable schema, effect shapes, and persistence results gate only
their later capabilities.

## Decision

`pcg` is an authored-graph Domain, not a mirror of UE 5.8's experimental PCG
MCP Toolset.

The official Toolset establishes useful agent workflows: discover schemas,
create Nodes, connect them through native compatibility rules, separate Graph
defaults from instance overrides, execute Graph instances, and inspect Node
data. Loomle adopts that workflow evidence while retaining SAL's stronger
contract for exact Targets, native StableRefs, ordered atomic Patch, dry run,
canonical Result Text, and unified diagnostics.

The two surfaces are complementary:

| Surface | Primary responsibility |
| --- | --- |
| UE 5.8 PCG Toolset | broad PCG operations, World instances, execution, Data View, spline, and instant Graph workflows |
| Loomle `pcg` | deterministic authored Graph query and certified mutation with native identity, schema, preflight, rollback, and readback |

Loomle does not depend on, wrap, discover, or silently fall back to the
experimental Toolset. The mature baseline adapter is built on the public PCG
runtime API. Optional Palette discovery may use public editor-only APIs, but a
private `UPCGEditorGraph` facade is not a cross-version foundation and is never
left attached merely to make a headless operation work. A structured SAL
failure never retries through another MCP server.

## Intent

The full planned Domain should let an agent inspect an existing PCG Graph,
discover exact Node creation and field contracts, plan a topology change
against real PCG native semantics, apply the complete edit atomically, and
reopen the saved asset using the returned native identities.

The first useful workflow is:

1. discover an exact PCG Graph Target through Asset search;
2. inspect its summary, Nodes, Pins, Edges, and read-only Graph Parameters;
3. query exact Node or Palette schema;
4. submit one dry-run authored Patch;
5. inspect all direct and cascade effects;
6. submit the live Patch;
7. save in a separate terminal request;
8. unload and reopen the Graph using the same Target and StableRefs.

The first public milestone exposes only steps 1 and 2 for the currently
certified Target, summary, Node, Pin, schema, layout, and Edge surface.
Parameters, `context`, `data flow`, Palette, PCG-owned related Target/handoff
projection, Patch, dry run, transaction, Undo, save, `sal.object()`, and
execution remain unavailable.

The Domain is valuable only if this path is materially safer and more
deterministic than a series of independent PCG tool calls. Its surface is
capability-gated: the coordinated Scene/PCG catalog release may expose a small
certified `pcg` authored-state subset while returning an exact unavailable
diagnostic for an uncertified Settings Class, conversion hook, or cross-Target
cascade. Breadth never weakens the reliability contract of the certified
subset. This subset is not permission to publish `pcg` independently of the
family's coordinated nine-Domain catalog gate.

## Capability Boundary

### In scope

The authored `pcg` Domain owns:

- one top-level asset-backed `UPCGGraph` Target;
- the Graph's default Input and Output Nodes;
- authored Nodes and their native Settings interface;
- current input and output Pins;
- Edges as relationships between exact Pins;
- persistent Node position, title, comment, and exact-schema writable fields;
- read-only Graph User Parameters stored in `FInstancedPropertyBag`;
- certified native and preconfigured Node Palette actions;
- independent subgraph-asset references and handoffs;
- dry-run planning, live transaction, Undo, readback, and Graph package save
  for the certified Target-authored subset.

### Out of scope for v1

The first Domain does not own:

- PCG Graph asset creation, rename, move, duplicate, or deletion;
- `UPCGGraphInstance`, `UPCGComponent`, `APCGVolume`, or arbitrary World actors;
- instance Parameter overrides;
- generation, cancellation, runtime scheduling, partitioning, or hierarchical
  generation state;
- execution messages, inspection stacks, loop iterations, or Data View;
- spline drawing or Editor viewport interaction;
- PCG Instant Graph execution;
- mutation of an external shared Settings asset through a Node;
- mutation of an external Settings-instance wrapper, including `bEnabled`,
  until a detached effective-Settings shadow proves dry-run isolation;
- Graph Parameter create, value edit, rename, type change, or removal;
- UE 5.8 Parameter metadata or hierarchy editing;
- embedded Graph objects owned inside another `UPCGGraph`;
- Graph Comment Box lifecycle in the first implementation slice.

World/component configuration and execution are deliberately separate. The
level-owned `pcg_component` Target covers authored configuration on any
supported original `UPCGComponent`; the typed PCG frontend on the shared async
kernel covers generation, cleanup, cancellation, status, and inspection. Neither is
silently entered from this Graph Target, and neither is restricted to
`APCGVolume`.

## UE Source Basis

The primary native sources are:

- `Engine/Plugins/PCG/Source/PCG/Public/PCGGraph.h`;
- `Engine/Plugins/PCG/Source/PCG/Public/PCGNode.h`;
- `Engine/Plugins/PCG/Source/PCG/Public/PCGPin.h`;
- `Engine/Plugins/PCG/Source/PCG/Public/PCGSettings.h`;
- `Engine/Plugins/PCG/Source/PCG/Private/PCGGraph.cpp`;
- `Engine/Plugins/PCG/Source/PCG/Private/PCGNode.cpp`;
- `Engine/Plugins/PCG/Source/PCG/Private/PCGPin.cpp`;
- `Engine/Plugins/PCG/Source/PCGEditor/Private/Schema/PCGEditorGraphSchema.cpp`;
- the PCG Editor Graph action and Details View implementations.

UE 5.8's reference MCP implementation is:

- `Engine/Plugins/Experimental/ModelContextProtocol`;
- `Engine/Plugins/Experimental/Toolsets/PCGToolset`.

The official PCG Toolset covers Graph creation and structure, Graph
Parameters, Node discovery and schema, native and subgraph Node creation,
connection conversion, PCG Volume instances and overrides, execution, Data
View, Comment Boxes, spline drawing, and instant Graph execution. It is useful
as a workflow inventory, not as Loomle's protocol or ownership model.

Important source facts that constrain this design are:

- `UPCGGraph` has no persistent Graph Guid suitable for a top-level Target;
- `UPCGNode` has no persistent Node Guid;
- PCG Editor `UEdGraphNode::NodeGuid` belongs to a rebuilt Editor facade and is
  not restored from the runtime `UPCGNode`;
- `UPCGNode` is serialized under its owning Graph with a unique UObject name;
- `UPCGPin` has no Guid, and native lookup selects input or output direction
  before looking up `FPCGPinProperties::Label`;
- input and output Pins may have the same Label;
- Pin allowed and current types are structured `FPCGDataTypeIdentifier` values:
  each preserves one or more `FPCGDataTypeBaseId` records plus the identifier's
  `CustomSubtype`, rather than only a legacy `EPCGDataType` mask or display name;
- an Edge has no independent persistent identity;
- a Graph Parameter descriptor has a persistent `FGuid`;
- a Node's `SettingsInterface` pointer proves reachability, not mutation
  ownership; ownership of the interface and its effective Settings must be
  classified independently by walking each UObject's Outer chain to the exact
  selected `UPCGGraph`;
- Settings changes can synchronously reconstruct Pins, propagate dynamic
  types, and remove incompatible Edges;
- native adaptive connection may insert one filter Node or one or more
  conversion Nodes;
- `UPCGGraph::AddLabeledEdge()` does not return ordinary success: its Boolean
  reports whether other Edges were broken;
- UE 5.8 embedded subgraphs are child UObjects owned by their parent Graph and
  are not independent package-save units.
- Graph Parameter notifications refresh loaded `UPCGGraphInstance` override
  bags and may dirty packages outside the Graph asset;
- ordinary Graph-change notifications can reach compiler caches, Graph
  instances, editors, and loaded PCG Components, whose refresh/cleanup work is
  derived World state rather than Target-authored Graph state;
- PCG `PreSave` may copy editor-facade extra nodes, comments, edited-document
  state, or cached Pins back into the Graph depending on engine version.

## Target

An initial Query may discover a Graph from its exact Asset Path and may omit
`type`:

```sal
g = target {
  domain: pcg,
  asset: "/Game/PCG/PCG_Forest.PCG_Forest"
}
```

After opening the object, Loomle returns the actual native Class Path. Every
canonical exact Query and every Patch uses:

```sal
g = target {
  domain: pcg,
  asset: "/Game/PCG/PCG_Forest.PCG_Forest",
  type: "/Script/PCG.PCGGraph"
}
```

`asset` is the exact top-level object path. `type` verifies the loaded native
Class and never selects the Domain. A valid subclass of `UPCGGraph` returns its
actual Class Path rather than being rewritten to the base Class.

"Asset-backed" is the Target ownership rule. It is not the same concept as
`UPCGGraph::bIsStandaloneGraph`, which controls native execution behavior. A
normal PCG Graph asset is not rejected merely because that execution flag is
false.

The v1 resolver rejects:

- a `UPCGGraphInstance`;
- a Graph reached only through a Component or runtime instance;
- an embedded Graph whose outer is another `UPCGGraph`;
- a non-PCG asset with a caller-supplied PCG `type` assertion;
- duplicate or ambiguous asset objects.

An independently saved subgraph asset opens as its own ordinary `pcg` Target.
A subgraph Node in the current Graph may return that related Target; it does
not recursively grant mutation authority over the referenced Graph.

PCG Graph asset creation remains outside this Domain, matching the current
Asset and StateTree boundaries. Until a structured asset-creation interface
exists, the UE 5.8 PCG Toolset or explicit Unreal Python fallback may create
the asset before Loomle opens it.

## Native Object Model

The Domain preserves these ownership relationships:

```text
UPCGGraph
|- DefaultInputNode: UPCGNode
|- DefaultOutputNode: UPCGNode
|- Nodes[]: UPCGNode
|  |- SettingsInterface -> UPCGSettingsInterface
|  |- InputPins[]: UPCGPin
|  |- OutputPins[]: UPCGPin
|  `- incident UPCGEdge relationships
`- UserParameters: FInstancedPropertyBag
```

The arrow marks `SettingsInterface` as a native UObject reference, not an
ownership assertion. The adapter classifies that object and the effective
`UPCGSettings` returned by `GetSettings()` separately from their actual Outer
chains.

A PCG Node is not identified by its behavior Class. Most behavior is defined
by the concrete `UPCGSettings` returned by the Node's Settings interface. A
specialized Node subclass, such as a subgraph Node, still returns its actual
Node Class separately from its Settings Class.

The Domain never fabricates a Guid for a Node, Pin, Settings object, or Edge.

## Identity

The identity contract is:

| Object | StableRef identity path |
| --- | --- |
| Node | `[Node.GetFName()]` |
| input Pin | `[Node.GetFName(), "in", Pin.Properties.Label]` |
| output Pin | `[Node.GetFName(), "out", Pin.Properties.Label]` |
| Graph Parameter | `[PropertyBag descriptor ID]` |
| Edge | no StableRef; exact source and destination Pins |

Examples:

```sal
@SurfaceSampler_0
@SurfaceSampler_0/in/Surface
@SurfaceSampler_0/out/Out
@11111111-1111-1111-1111-111111111111
```

### Node identity

`UPCGNode::GetFName()` is the serialized, Graph-local UObject identity. It is
not `NodeTitle`, a localized default title, Settings Class, Editor facade
`NodeGuid`, or Settings execution/cache UID.

Node creation lets UE allocate a unique UObject name. The caller supplies a
request-local binding alias, not a desired Node name. Live readback returns the
actual native `FName`.

The release gate must prove that this identity survives:

- package save, unload, and reload;
- Graph close and reopen;
- Undo and Redo;
- Node title changes;
- asset duplication, where the new Target scopes otherwise equal Node names;
- Node duplication or copy/paste, where the new Node receives a distinct name.

The resolver follows native `FName` equality and always returns UE's canonical
spelling. The formatter never substitutes a display title.

### Pin identity

PCG first chooses `GetInputPin(Label)` or `GetOutputPin(Label)`. The canonical
`in` and `out` identity segments encode that native direction requirement.
They are not semantic tags or optional collection decoration.

Pin Labels may contain spaces, dots, slashes, arrows, or other characters that
are not safe bare identity segments. Existing StableRef JSON-string segments
preserve them:

```sal
@ShapeSampler_0/in/"Bounding Shape"
@AttributeNode_0/out/"A.B"
@RouteNode_0/out/"A/B"
```

Without quotes, `@AttributeNode_0/out/A.B` means Pin identity `A` followed by
member `.B`; it does not mean the Pin Label `A.B`.

An empty Label or a duplicate Label within the same Node and direction cannot
produce an injective StableRef. Such native state may be returned as ordinary
adjacent data with a diagnostic, but it cannot be an exact mutation subject.
A synthesized local result key does not repair a native identity collision.

Pins are structural snapshot identities. A Settings edit or dynamic type
propagation may remove a Pin or create a new one with the same Label. After
every native operation, the adapter re-resolves later request references from
the current Graph state instead of retaining stale `UPCGPin*` pointers.

### Parameter identity and collision policy

A Graph Parameter uses its `FPropertyBagPropertyDesc::ID`. Rename preserves
that Guid. Parameter display name and authored order are not identity. This
identity contract is reserved now even though Parameter collection and exact
Parameter Query are deferred beyond the current slice.

Node names and Parameter Guids both occupy the one-segment identity shape.
Once Parameter Query is activated, the Domain must audit all one-segment
categories together. If a Node name and a Parameter Guid decode to the same
identity text, resolution fails closed with `resolution.identity_conflict`.
`node @x` and `parameter @x` tags cannot disambiguate the collision.

Slice 1 does not enumerate or resolve Parameters, so its one-segment lowerer
resolves Nodes only. The Parameter-only diagnostic and cross-category collision
audit are release gates for the later slice that first activates Parameter
collection or exact Parameter Query; the adapter must add both atomically and
must not publish Parameter identity before that audit exists.

### Bridge identity validation

The normalized StableRef schema accepts a non-empty string `identityPath`.
Domain-owned identity lowerers, rather than one global Guid precondition,
validate and resolve that structured path:

- the shared layer validates only a non-empty string path;
- Blueprint, Graph, StateTree, and Widget retain their current Guid rules;
- the Slice 1 PCG lowerer resolves a one-segment path as an exact Node `FName`;
- it resolves a three-segment path only when segment two is exactly `in` or
  `out`, then resolves the Node followed by the Pin's exact directional Label;
- it rejects every other arity, direction, empty segment, missing object, or
  ambiguous Label before the Query adapter projects a result.

The PCG lowerer consumes the segment array directly. It never joins segments
with `/`, splits a Pin Label on punctuation, or round-trips through a fused
legacy identifier. A quoted Label such as `"A/B"` therefore remains one exact
third segment. The one-segment Parameter branch remains reserved by the same
identity contract but is not enabled as a Query subject in this slice.

This is a correction to Domain ownership, not a global relaxation of existing
Guid identities. PCG does not participate in legacy fused-reference lowering.

## Query

The current internal read-only Query surface is:

```sal
target
summary
nodes ["text"]
@identity
```

Exact Target, Node, and Pin reads may use `with schema`. `nodes` and exact Node
or Pin reads may use `with layout`. Exact Node and Pin projections include
their incident Edges as relationships; incident Edge projection is not a
separate traversal operation.

The following planned Query forms remain deferred and are neither accepted nor
advertised by this slice:

```sal
parameters ["text"]
@guid
context @identity [depth N]
data flow from|to @identity [depth N]
palette entries ["text"] [from|to @node/in-or-out/label]
palette @id
```

All Patch mutation and all PCG execution are deferred as well. Execution stays
on the separate typed async frontend rather than becoming a Graph Query form.

PCG does not expose `exec flow`; authored PCG connections are data
relationships even when a Pin represents an execution dependency.

The exact Target operation reads Graph-level authored properties and schema:

```sal
query g
target
with schema
```

Collections use bounded cursor pagination. Their final searchable fields,
filters, and ordering keys must eventually be closed by the static interface
card rather than inherited accidentally from Blueprint Graph. At minimum, Node
search must consider native object name, computed title, authored
title/comment, actual Node Class, and Settings Class without treating any of
them as identity.

`summary` returns a compact Graph description, counts, default Input/Output
Pin schemas, and structural diagnostics. The current slice does not project
Parameter descriptors, values, or Parameter-derived counts through `summary`.
It does not claim a persistent compile status.

Current exact Node, Pin, and Target reads may use `with schema`. Collections
and summary do not.

When later enabled, `context` and `data flow` accept only a StableRef, not a
member path, and remain limited to applicable Node and Pin subjects.

### Layout

`with layout` is available on Node-bearing operations. PCG v1 returns only the
persisted integer Node position as `at: [x, y]`. It does not promise the Graph
Domain's live Slate `visualBounds`, Pin geometry, placement anchors, or focus
state.

### Node and Pin Result Text

A compact exact result has this shape:

```sal
surface = node {
  id: "SurfaceSampler_0",
  type: "/Script/PCG.PCGNode",
  title: "Surface Sampler",
  titleOverride: null,
  NodeComment: "",
  SettingsInterface: {
    type: "/Script/PCG.PCGSurfaceSamplerSettings",
    bEnabled: true,
    Seed: 42
  },
  at: [320, 0]
}

surface.in.Surface = pin {
  id: "Surface",
  direction: in,
  allowedTypes: {
    ids: [{ struct: "/Script/PCG.PCGDataTypeInfoSpatial" }],
    customSubtype: -1
  },
  currentTypes: {
    ids: [{ struct: "/Script/PCG.PCGDataTypeInfoPoint" }],
    customSubtype: -1
  },
  typeDisplay: "Point",
  allowsMultipleConnections: false,
  required: true
}

surface.out.Out = pin {
  id: "Out",
  direction: out,
  allowedTypes: {
    ids: [{ struct: "/Script/PCG.PCGDataTypeInfoPoint" }],
    customSubtype: -1
  },
  currentTypes: {
    ids: [{ struct: "/Script/PCG.PCGDataTypeInfoPoint" }],
    customSubtype: -1
  },
  typeDisplay: "Point",
  allowsMultipleConnections: true,
  required: false
}
```

The shown Classes and type entries are illustrative; exact results use the
target engine's actual values. A specialized Node returns its actual Class.
`title` is the computed presentation title, while `titleOverride` is the raw
authored override and may be absent. Exact schema mutates only the authored
override. `SettingsInterface.type` is the actual Settings interface Class and
is never substituted for `node.type`.

Pin `id` is the exact native Label within the returned direction. PCG type
information is not compressed into a single legacy enum or display string.
Both `allowedTypes` and `currentTypes` preserve the native
`FPCGDataTypeIdentifier` shape: `ids` is the ordered composition of
`FPCGDataTypeBaseId` records, each carrying the exact `UScriptStruct` Class Path
for an `FPCGDataTypeInfo` subtype, and `customSubtype` is the identifier's exact
integer value (`-1` means no custom subtype). This keeps the declared allowed
union distinct from the dynamically resolved current union. `typeDisplay` is
derived, non-authoritative presentation text.

Local result member keys are presentation handles. A safe, non-conflicting Pin
Label may be used directly. Textually unsafe Labels or collisions with another
result member key receive deterministic safe keys while `id` retains the exact
Label:

```sal
surface.in.pin_1 = pin {
  id: "Bounding Shape",
  direction: in
}
```

That local key is not StableRef identity. The exact reference remains:

```sal
@SurfaceSampler_0/in/"Bounding Shape"
```

### Edges

Edges are emitted as relationships between local Pin bindings:

```sal
surface.out.Out -> transform.in.In
```

In the current slice, an exact Node returns Edges incident to any of its Pins,
and an exact Pin returns Edges incident to that Pin. The adapter includes the
compact opposite endpoint bindings needed to make each relationship exact;
this does not recursively expand into `context` or `data flow`. There is no
Edge tag, object, Guid, index, or StableRef. In a later mutation slice, exact
Edge mutation always names both endpoint Pins.

## Settings Interface

`SettingsInterface` is not a convenient configuration dictionary invented by
SAL. It is the native `UPCGNode::SettingsInterface` reference surface. Pointer
reachability from a Node does not by itself grant mutation ownership.

The adapter classifies each Settings-interface UObject and the effective
`UPCGSettings` returned by `GetSettings()` independently. An object is
Graph-owned only when walking its actual `GetOuter()` chain reaches the exact
selected top-level `UPCGGraph`; otherwise it is external. Direct ownership by
the Node is the common shape, but is not the rule. Sharing the Graph package,
appearing below `SettingsInterface` in Result Text, or being reachable through
a property is not a substitute for the Outer-chain proof.

### Graph-owned Settings

For an ordinary native Node, the interface object is itself a concrete
`UPCGSettings`, and its usual Outer chain passes through the Node to the Graph:

```sal
SettingsInterface: {
  type: "/Script/PCG.PCGSurfaceSamplerSettings",
  isInstance: false,
  ownership: owned,
  interfaceOwnership: owned,
  effectiveOwnership: owned,
  bEnabled: true,
  Seed: 42
}
```

The current exact Node schema lists the bounded readable member paths and
native property types on that concrete instance, and reports `operations:
none`. A later authored slice may add writable, resettable, and unavailable
sets. Writable does not follow automatically from `EditAnywhere`: the
capability registry is keyed by exact engine version, exact Settings Class, and
exact member path, and records the certified native lifecycle and Graph
ownership boundary. Settings Class selects behavior, Pin declarations, dynamic
type propagation, and execution element; it does not become the Node's UObject
Class.

### External Settings and Settings instances

A Node may instead reference a Graph-owned `UPCGSettingsInstance` wrapper whose
effective Settings object is external:

```sal
SettingsInterface: {
  type: "/Script/PCG.PCGSettingsInstance",
  isInstance: true,
  ownership: external,
  interfaceOwnership: owned,
  effectiveOwnership: external,
  bEnabled: true,
  Settings: "/Game/PCG/Settings/PS_Forest.PS_Forest"
}
```

Every object whose Outer chain does not reach the selected Graph is external
and always read-only through this Target, including an external interface
object or the effective Settings referenced by a wrapper. This remains true if
the object happens to share a package with the Graph. When a Graph-owned wrapper
delegates to external effective Settings, the entire wrapper is also read-only
in the baseline capability, including `bEnabled`, the `Settings` pointer, and
every `Settings.*` descendant.

`interfaceOwnership` and `effectiveOwnership` report the two independent
Outer-chain classifications. The aggregate `ownership` field is the mutation
boundary: it is `owned` only for a non-instance interface whose interface and
effective Settings are both Graph-owned; otherwise it is `external` and the
whole surface is read-only.

The current slice returns an external object's native Class and object path as
read-only data when needed by an exact Node result; it does not create a related
Target or handoff. A later related-Target capability may return an asset-backed
external object as a canonical Asset Target using Asset Domain `path + type`
and retain it through an explicit handoff. A native object-path string alone is
not a Target reference and never grants external mutation authority.

This stricter rule is required for dry-run isolation. Duplicating the Graph
duplicates the wrapper but can leave its effective Settings pointer attached
to a live external object. Wrapper lifecycle and `SetEnabled()` can subscribe
to, invalidate, or call methods on that effective Settings object even without
writing `Settings.*`. A later capability may expose a wrapper-owned field only
after the sandbox remaps the effective Settings to a detached transient shadow
and proves that delegates, serialized external state, and package dirty flags
remain unchanged.

"Effective Settings" in PCG means the authored Settings source returned by
`GetSettings()`. It does not mean the final runtime value after Parameter-pin
overrides. PCG v1 does not report an authored field as a resolved execution
value.

### Settings mutation lifecycle

Settings edits are native semantic operations, even when represented by SAL
`set` or `reset`. An uncertified Class/member returns capability unavailable
before simulation. The adapter must:

- call `Modify()` on the correct owned objects;
- respect `CanEditChange` and native Details customization restrictions;
- use a dedicated method such as `SetEnabled()` where the native Class
  requires it;
- run the correct pre/post property-change lifecycle;
- allow PCG to rebuild and update Pins;
- capture recursive dynamic Pin propagation and removed Edges;
- re-resolve later references and local members against the new state.

Any member path that crosses from a Target-owned UObject into an external
UObject or package is an ownership barrier even when reflection makes the path
look writable. Native plugin callbacks that cannot prove Target-authored
isolation are not admitted to the capability registry.

Transient debugging and inspection flags are not authored v1 fields.

## Palette

This section retains the later Palette contract; none of it is enabled by the
current read-only slice.

Every directly created Node starts from a Palette result. A caller never
guesses a Settings Class, Node name, Pin, or Palette id. Parameter type
capabilities may be discoverable as read-only schema data, but Parameter
creation is not a baseline Patch operation.

The PCG Palette is derived from native PCG Editor actions rather than a second
hardcoded taxonomy. Required v1 origins are:

- native `UPCGSettings` Classes;
- native preconfigured Settings variants;
- independent PCG subgraph assets.

Settings assets, Blueprint elements, named reroutes, comments, load-asset
actions, and other native PCG Editor actions may be added only when their
ownership and creation effects meet the same contract.

Opaque Palette identity must be based on non-localized native facts. It must
not use `DefaultNodeTitle` as its unique key. A Palette result records origin,
native Settings or action identity, preconfiguration, engine compatibility,
and any discovery Pin context needed to replay it. The id is a whitespace-free
opaque token compatible with the existing `palette @id` grammar; contextual
from/to information is encoded or registered in that token rather than added
as an unparsed side channel.

Settings-asset actions must explicitly distinguish `copy` from `instance`.
UE Editor modifier-key behavior is interactive UI state and cannot select SAL
semantics.

### Contextual Palette

PCG reuses the existing Pin-context syntax:

```sal
query g
palette entries "Transform" from @SurfaceSampler_0/out/Out
```

Compatibility is computed through the target engine's native PCG type
registry. A candidate may report:

- direct compatibility;
- compatibility requiring a native filter;
- compatibility requiring conversion;
- incompatibility;
- provisional compatibility when dynamic Pins may differ after creation.

An exact opaque Palette id retains its discovery context and re-resolves that
context during replay. Exact Palette schema supplies copyable creation and Pin
member templates.

### Node creation

The caller provides only a local binding alias and Palette result fields:

```sal
patch g dry run
sample = { palette: "<opaque-node-entry>" }
add sample
```

UE creates the Node, Settings interface, and default Pins. Raw Pin creation is
invalid. Dry run retains `sample` as a request-local alias and never exposes a
transient sandbox UObject name. Live readback returns the actual Node StableRef.

Simple, default Pin Labels can be consumed later in the same Patch through
Palette-provided local members:

```sal
connect @DefaultInputNode/out/In -> sample.in.Surface
```

Member paths accept identifiers and array indices. An unsafe or newly dynamic
Pin with no preflight-stable local key cannot be guessed. The caller applies
the creation, reads back the actual Node and quoted Pin StableRef, then uses a
following Patch.

## Patch

All mutation in this section is deferred beyond the current read-only slice.

PCG reuses the existing core Patch grammar:

```sal
patch g [dry run]

created = { palette: "<opaque-id>" }
add created

set <object>.<field> = <value>
reset <object>.<field>
move <node> to (<x>, <y>)

connect <exact-output-pin> -> <exact-input-pin>
disconnect <exact-output-pin> -> <exact-input-pin>
break <exact-pin>

remove <node>
invoke <exact-object> <ExactSchemaOperation>(namedArguments) [as outputs]
```

SAL statements end at a depth-zero newline. PCG examples and formatter output
keep `connect`, `disconnect`, and other statements on one line. There is no
implicit line continuation after `->`.

The PCG adapter explicitly rejects grammar shapes that Core accepts for other
Domains:

- `move ... by (...)`;
- `move ... before ...` or `move ... after ...`;
- a reference or tree destination for `move`;
- raw Pin `add` or `remove`;
- removal of the default Input or Output Node;
- same-direction, cross-Target, or non-Pin connection endpoints;
- `compile`;
- authored edits mixed with terminal `save`.

### Ordered example

```sal
patch g dry run
sample = { palette: "<opaque-surface-sampler-entry>" }
add sample
set sample.SettingsInterface.Seed = 42
move sample to (640, 0)
connect @DefaultInputNode/out/In -> sample.in.Surface
```

The field and Pin names in a real request must be copied from exact Palette or
instance schema. The example does not authorize guessing `Seed`, `Surface`, or
any Palette id for another engine or Node variant.

`move` accepts only an absolute signed 32-bit integer position. It calls the
native Node position path and returns actual persisted coordinates. A no-op
move stays in the operation plan but does not dirty the package or create Undo.

## Connection

`connect` is a checked topology operation, never a low-level unchecked Edge
append. Before invoking a mutating helper, the adapter handles an already exact
Edge as a no-op and rejects wrong direction, cross-Graph endpoints, same-Node
connections, and cycles using the target engine's native policy or an audited
public-runtime parity shim.

The baseline asks `UPCGPin::GetCompatibilityWithOtherPin()` and follows this
fail-closed policy:

| Native result | Baseline SAL behavior |
| --- | --- |
| `Compatible` with available destination cardinality | create the exact Edge |
| `Compatible` but a single-connection input is occupied | reject `connection_occupied`; require explicit `disconnect` |
| `RequireFilter` | reject with a structured `requires_filter` diagnostic; include a suggestion only when an exact replayable Palette entry exists |
| `RequireConversion` | reject with a structured `requires_conversion` diagnostic; include a chain suggestion only when every step is explicitly replayable |
| incompatible or unknown | reject with the native reason |

This baseline is intentional. It can use public PCG runtime APIs on UE 5.7 and
UE 5.8 without opening a full editor toolkit or depending on private
`UPCGEditorGraph` headers. It also prevents an ordinary `connect` from hiding
Node creation or destructive Edge replacement.

Adaptive insertion may be added later for individually certified engine
version and type-pair capabilities. It is public for one such path only when:

1. Preflight runs the audited filter or conversion hook on a complete isolated
   Graph without touching an active World, global compilation cache, or live
   external object.
2. Every expected source-to-chain-to-destination Edge is verified; a helper
   returning a Node is not evidence of complete connection success.
3. Per-statement before/after snapshots identify every implicit Node, Pin
   change, added Edge, removed Edge, and stored position.
4. Dry run and live results share a stable operation-local role keyed by
   `(statementIndex, effectKind, ordinal)`; live effects add actual StableRefs,
   while roles never enter Object Text or later statement resolution.
5. Live apply runs inside the SAL top-level authored transaction and canonical
   readback matches the preflight topology.
6. Any failure restores the complete pre-Patch Target-authored snapshot.

An uncertified conversion path returns capability unavailable. It must not
silently create an unchecked Edge, skip conversion, expose a sandbox UObject
name as a future StableRef, or leave a partially connected helper Node.

Connection success is verified by native topology readback. The Boolean return
from `UPCGGraph::AddLabeledEdge()` cannot be treated as success because it
reports whether other Edges were broken.

`disconnect output -> input` removes exactly one existing Edge. Missing Edge
is an error rather than a no-op. `break pin` removes every Edge incident to the
exact Pin. Both operations may trigger dynamic type propagation; secondary
Pin and Edge changes are effects.

## Graph Parameters

Graph Parameters are descriptors and default values in the Graph's
`FInstancedPropertyBag`. They are not Node Settings, runtime instance
overrides, or Get Parameter Nodes. The identity contract below is reserved,
but its collection and exact Query forms are not enabled in the current slice.

### Query and identity

```sal
query g
parameters "Density"
```

Each Parameter returns its persistent descriptor Guid, native name, native
property type/container/object facts, current Graph default value, and
authored order. Exact access uses:

```sal
query g
@11111111-1111-1111-1111-111111111111
with schema
```

### Mutation boundary

Every Graph Parameter mutation is unavailable in the baseline `pcg` Patch:
create, default-value edit, rename, type/container change, metadata change, and
remove. This is an ownership decision, not a missing text syntax.

Native Parameter notifications are observed by loaded `UPCGGraphInstance`
objects. Refreshing their override bags can call `Modify()` and dirty packages
outside the Graph Target, and the dependency chain may continue through child
instances. Unloaded dependents cannot be enumerated as a complete immediate
effect closure. A single-Graph sandbox, transaction, rollback, and save
therefore cannot honestly call Parameter mutation target-local.

If Loomle later adds Parameter mutation, it is a separate multi-Target
migration operation outside ordinary one-active-Target SAL Query/Patch, with an
explicit dependency closure, revision guards, package/dirty ledger, native
semantic lowering, and per-Target readback. Rename
must use `RenameUserParameter`; value changes must use the native scalar,
array, or set path; type and removal must report Getter Node/Pin/Edge cascades.
Dry-run creation must use an operation-local role rather than publishing a
sandbox-generated descriptor Guid unless a normalized plan explicitly
preallocates and reuses that Guid during live apply.

UE 5.8 Parameter metadata and hierarchy do not exist with the same semantics
in UE 5.7. Query reports exact engine/version availability rather than
flattening or dropping the distinction.

## Dynamic Pins And Compound Operations

Pins belong to Settings and native Node update logic. They are never raw
independently creatable objects.

Settings with dynamic inputs may expose an exact-schema `invoke` operation
only when Loomle can describe:

- the native editor action;
- its named arguments;
- its primary outputs;
- Pin rename/add/remove behavior;
- Edge and downstream propagation effects;
- availability on the exact Node instance and engine version.

For example, a native `AddInputPin()`-style action may return a local Pin alias
when preflight can determine its final Label and role. No generic
`AddPin(index)` operation is invented.

Graph Input/Output custom Pin editing is deferred until the same lifecycle and
effect contract is source-grounded. The default Input and Output Nodes remain
queryable and connectable but cannot be removed.

## Dry Run, Transaction, And Readback

Dry run and live Patch share:

```text
parse -> Target resolve -> identity resolve -> validate -> native plan
```

When a certified native operation can create Settings, rebuild Pins, or run
callbacks, validation alone is not sufficient. Preflight duplicates the
complete Graph into an isolated transient owner and executes the same ordered
runtime-adapter operations there. Post-load normalization must leave the
sandbox baseline identical to the canonical live authored snapshot; otherwise
the operation returns `capability.preflight_unavailable`.

The sandbox must preserve and compare:

- Node UObject names and actual Classes;
- Settings interface ownership and concrete Classes;
- readable authored Settings values;
- input/output Pin Labels, types, cardinality, and order;
- every Edge endpoint;
- read-only Graph Parameter descriptors, order, and values;
- persistent Node positions and comments;
- the source package dirty state, Undo/Redo state, and related external assets.

Sandbox identities map back to existing live StableRefs or request creation
aliases. A transient UObject name is never published as a future live
identity. Implicit native creations without caller aliases use non-referenceable
effect roles during dry run.

The sandbox strips editor-facade state and cannot retain live external Settings
delegates, subgraph listeners, cache owners, or mutable external references.
An external-object guard records serialized state and package dirty flags and
requires them to remain unchanged after dry run and failed live apply.

Live authored mutation uses one top-level transaction plus a notification
barrier. Each statement executes in written order. After every lifecycle
boundary, subsequent references are resolved again from the current Graph.
Graph and Parameter notifications that could synchronously dirty dependents or
refresh World components remain isolated during apply and rollback. On commit,
observers see the committed state; on failure, explicit transaction reversal
and canonical readback must restore the pre-Patch Target-authored snapshot,
package dirty state, and pre-existing Redo history before notifications resume.

The Graph adapter also participates in the shared PCG operation coordinator.
Graph Patch and save fail closed while any admitted nonterminal execution reads
that Graph, whether or not inspection capture is enabled; execution admission
likewise fails while Graph Patch or save holds the Graph lease. After terminal
capture, bounded retained inspection data must be self-contained so a later
Graph edit does not retarget it or require the live Graph lease.

This atomicity contract covers serialized Target-authored state. Native
`NotifyGraphChanged` observers may invalidate compiler caches, refresh loaded
components, clear inspection, or recompute other derived state after the
barrier opens. Such observer churn is neither claimed as authored effects nor
promised to be undoable. Any operation that cannot keep cross-Target serialized
state out of the transaction is not certified for this Domain.

A no-op remains visible in `planned.operations` with `changed: false`, but it
does not call `Modify()`, dirty the package, or create an Undo entry.

The first version returns complete ordered operation and effect plans. It does
not publish revision tokens, `expectedRevision`, or a rich diff until those
contracts are implemented generically and proven under native reconstruction.
A previous dry-run response is therefore not a reservation: live apply repeats
preflight against current Graph and engine-registry state and guarantees parity
only within that live request.

Once Bridge application begins, client cancellation cannot safely interrupt
arbitrary native editor callbacks. The existing SAL non-cancellation contract
continues to apply.

## Save

Persistence is an independent terminal Patch:

```sal
patch g
save
```

The request contains exactly one `save` statement. Authored edits and save do
not appear in the same Patch.

Save writes only the outermost package that owns the exact PCG Graph Target.
Related subgraph assets and external Settings assets are not included. A dry
run is advisory: it validates Target, package, PIE, source-control, and policy
facts without executing native `PreSave` or writing disk.

PCG v1 exposes no `compile` statement. PCG cache priming, recompilation, editor
notification, and cooking are native derived-state concerns rather than an
independent authored compile lifecycle. `save` never implies compile or Graph
execution.

Live save may execute PCG `PreSave`, which can synchronize editor extra nodes,
comments, edited-document state, or cached Pins into the Graph depending on
engine version. It persists the package's complete current dirty state, not
only the effects of an earlier Loomle Patch. The adapter records PCG pre/post
snapshot and disk/in-memory readback, but dry-run/live parity and authored
transaction atomicity explicitly exclude this request.

Save is a persistence barrier, not an undoable part of the preceding UObject
transaction. Failure before checkout, `PreSave`, or I/O returns
`applied: false`. If checkout, `PreSave`, or package I/O has already produced
an observable effect, failure reports `applied: true` plus the exact partial
persistence/pre-save outcome. Both forms retain the exact Target, report
`validation.save_failed` and `dirtyAfter`, and do not claim that prior
independent in-memory edits or `PreSave` effects were rolled back. The closed
package-status fields remain a result-schema release gate shared with Level
save.

## Results, Effects, And Diagnostics

PCG uses the shared SAL result envelope without a `structuredContent` special
case:

- the first MCP block is canonical round-trippable Result Text;
- mutation metadata follows in an independent comment block;
- diagnostics follow independently and never fabricate Object Text;
- an opened Target remains canonical when a later operation fails;
- a Target-open failure returns `unresolved_target`, no StableRef scope, and at
  least one error.

Every certified Target-authored cascade is an ordered entry in
`planned.effects`, including:

- created or removed Nodes;
- created, removed, renamed, or retyped Pins;
- added or removed Edges;
- Settings interface replacement;
- operation-local implicit Nodes for a certified adaptive extension.

Related Targets and handoffs are Result context, not mutation effects. Runtime
observer refreshes and cache invalidations are derived notifications, not
authored `planned.effects`.

Likely existing diagnostic codes include:

- `resolution.target_not_found`;
- `resolution.identity_conflict`;
- `resolution.node_not_found`;
- `resolution.pin_not_found`;
- `resolution.palette_entry_not_found`;
- `validation.field_value_invalid`;
- `validation.connect_failed`;
- `validation.atomic_apply_failed`;
- `validation.atomic_rollback_failed`;
- `validation.save_failed`;
- `capability.operation_unavailable`;
- `capability.transaction_unavailable`;
- `capability.preflight_unavailable`.

Any PCG-specific code enters the closed diagnostic registry before use. A
suggestion points to an exact Query, `with schema`, contextual Palette, or
related Target. It never guesses a Node name, Pin Label, Settings Class,
Parameter type, or Palette id.

## Cross-Domain Handoffs

This result projection is deferred beyond the coordinated Query-only release.
A later PCG Query capability may return independent related Targets:

- Asset Target for the current Graph asset;
- PCG Target for an independently saved subgraph asset;
- Asset Target for an external Settings asset;
- Blueprint or Class Target when a supported PCG element has a factual native
  declaration relationship.

Related Targets remain flat and independent. An object may retain the native
object path as data, but that string does not retain a Target or supply
mutation authority. Every related Target must be referenced by Object Text or
an explicit `handoff <purpose> to <alias>` as required by SAL Core. The handoff
is emitted only in Result Text as copyable context for a later independent
request; it is not a Query/Patch statement, never performs its purpose, never
expands the active `pcg` request, and never switches Domain implicitly.

## UE 5.7 And UE 5.8 Compatibility

The public PCG contract is semantic. Engine API drift belongs behind a small
native compatibility layer.

Known differences include:

- UE 5.8 adds `UPCGGraph::GetAllEdges()`; UE 5.7 must traverse Node Pins;
- UE 5.8 adds embedded subgraph ownership APIs, which v1 rejects as Targets;
- execution inspection and Data View APIs differ materially and remain out of
  this Domain;
- UE 5.8 adds Parameter metadata/hierarchy semantics that v1 does not expose;
- Settings change and Pin-update signatures have minor version drift;
- debugging setters and some break/update APIs differ;
- instant execution APIs are UE 5.8-only and out of scope.

The following core semantics exist in both supported versions and must be
tested on both:

- Graph-owned Node UObject names;
- input/output Pin Label lookup;
- Graph Parameter descriptor Guids;
- native Node creation from Settings Classes;
- Settings-driven Pin reconstruction;
- `GetCompatibilityWithOtherPin()` and compatible direct Edge creation;
- Edge removal and package persistence.

The production baseline always enables UE's built-in PCG plugin and both
`LoomleBridge` and `LoomleBridgeTests` compile against its public `PCG` runtime
module. Neither module depends on `PCGEditor`. The adapter never includes
private PCGEditor headers, requires a Graph asset to be open in a toolkit, or
depends on UE 5.8's experimental PCGToolset. PCGEditor and experimental-tool
source may still be read as behavioral reference material.

## Protocol And Catalog Impact

The internal family Phase 0 work allocates `pcg` as a new closed authored
Domain in Client-Bridge protocol v6. That v6 groundwork includes:

- add `pcg` to Domain keywords and reserved keywords;
- add PCG discovery and canonical Target shapes;
- preserve explicit `domain, asset, type` formatter ordering;
- update normalized JSON Schema and generated TypeScript types;
- update Client and Bridge protocol fixtures together;
- add `pcg` to the Result Target and handoff Target variant sets;
- add `pcg` to formatter ordering, schema-validator `targetKey`, related-Target
  deduplication, and canonical Result validation branches;
- move premature global Guid StableRef validation into Domain adapters.

Slice 1 consumes those v6 shapes unchanged. Its `target`, `summary`, `nodes`,
exact-object, schema, layout, and Result-relationship forms already exist in
the normalized protocol, so this slice does not bump v6 to v7. It adds no
public MCP method or private RPC route.

Historical Slice 1 created no card or catalog entry. The current branch RC now
contains `interfaces/pcg.md`, its static catalog entry, and offline
`sal_schema({module: "pcg"})`; these remain unpublished. External publication
still requires:

- update `LANGUAGE_CORE.md`, `DOMAINS.md`, and the local schema guide together;
- verify the static card and local Bridge-independent offline schema against
  the current implementation;
- generalize parser diagnostics that currently say "Graph Palette" when they
  mean any Pin-context Palette.

Unless a later slice changes normalized wire values, those later capability and
catalog additions continue to use protocol v6. The coordinated Query-only
release keeps `pcg` out of `PatchTarget`. Publishing any later PCG Patch or save
capability requires a coordinated protocol/capability bump even if the SAL
statement spelling is unchanged.

## Implementation Slices

Implementation should remain unpublished while a slice lacks its acceptance
gates. Passing a PCG slice permits internal landing, not an independent public
catalog release.

The family Phase 0 Target/admission and Domain-specific StableRef dispatch
groundwork is a prerequisite. Slice 1 supplies the PCG Node/Pin lowerer but does
not expand that groundwork with effects, save, mutation, or execution behavior.

### Slice 1: Target, identity, and read-only Query — implemented in RC

- at the original Slice 1 landing, consume the internal family protocol v6
  Target branch without publishing a card or adding a catalog/schema module;
- exact Target open/canonicalization;
- `target`, `summary`, and bounded `nodes` Query;
- structured Node and Pin StableRef lowering, including quoted Pin Labels and
  strict rejection of empty or ambiguous native Pin identity;
- exact Node and Pin reads, exact schema, and persistent layout readback;
- incident Edge relationships on exact Node and Pin projections;
- independent Outer-chain classification of the Settings interface and its
  effective Settings, including evidence-only external Settings instances.

`context`, `data flow`, Parameters, Palette, related Target/handoff projection,
all mutation, and all execution remain deferred beyond Slice 1.

### Slice 1B: coordinated Query-only publication — RC implemented, branch-local acceptance passed

- RC source contains Query identity persistence, pagination/cursor,
  corruption, no-load, no-mutation, and saved-target hostile fixtures;
- RC source contains `interfaces/pcg.md`, the offline
  `sal_schema({module: "pcg"})` card, and branch-local nine-Domain catalog entry;
- advertise only the Slice 1 Query surface; retain `context`, `data flow`,
  Parameters, Palette, and PCG-owned related Target/handoff projection as exact
  unavailable capabilities; and
- reject `pcg` from `PatchTarget` before Bridge dispatch, with no effect, save,
  projection, or execution schema added.

The Query-only RC is branch-locally accepted but not released. Its current
snapshot passes the PCG Automation group 7/7 on both UE 5.7 and UE 5.8, the
related Level and family Phase 0 suites, both engine builds, and packaged
Client/Bridge end-to-end acceptance. The protocol-v6 cards, offline schema,
and nine-Domain catalog are complete in the branch but remain unpublished.
Final release-archive validation, Windows validation, and the coordinated
promotion gate remain outstanding.

### Slice 2: Blueprint-grade certified authored core

- canonical authored snapshot/diff and external-object guard;
- detached transient sandbox and notification barrier;
- native/preconfigured Node Palette;
- Node add/remove;
- certified graph-owned Settings set/reset lifecycle;
- external Settings mutation guards and sandbox shadows for any future
  wrapper-owned writable capability;
- absolute move;
- compatible direct connection into an unoccupied input;
- direct disconnect/break;
- complete transient preflight and transaction rollback.

### Slice 3: Save and packaged acceptance

- source-control-aware package save;
- PCG `PreSave` snapshot and save failure semantics;
- save/unload/reload identity and value readback;
- save-capability packaged acceptance against both supported engines.

### Slice 4: Capability-gated authored extensions

- external wrapper-owned enable state with a detached Settings shadow;
- individually certified dynamic-Pin actions;
- individually certified adaptive filter/conversion type pairs;
- operation-local effect roles and live StableRef readback.

### Separate future project: Parameter migration

- new protocol outside ordinary one-active-Target SAL Query/Patch;
- explicit multi-Target GraphInstance dependency closure;
- revision and dirty-package ledger across every authorized Target;
- native create/value/rename/type/remove semantic lowering;
- Getter topology and loaded-instance override migration effects;
- explicit limits for unloaded dependents and UE 5.8-only metadata.

## Test Requirements

### TypeScript and protocol

- `pcg` Target parse, format, normalization, and reserved-keyword tests;
- quoted Node/Pin StableRef round-trip tests;
- structured one-segment Node and three-segment Pin lowering tests, including a
  Label containing `/` that remains one segment;
- generated schema/type parity;
- protocol remains exactly v6 across Client and Bridge Slice 1 fixtures;
- historical static-catalog absence through pre-RC Slice 1, followed by RC
  card/catalog presence, offline schema, and `PatchTarget` rejection in 1B;
- exact Client-Bridge protocol mismatch fixtures;
- one-line connection formatter tests when the later mutation slice lands.

### Target and identity

- correct and wrong native Class assertion;
- non-PCG asset rejection;
- embedded or instance Target rejection;
- Node `FName` persistence across save/unload/reload;
- title changes preserve Node identity;
- duplication creates Target-scoped or Node-unique identity as appropriate;
- input/output Pins with equal Labels remain distinct;
- quoted Labels containing spaces, dots, and slashes;
- Parameter collection, Parameter-only lookup, and cross-category collision
  audit remain unavailable together;
- empty or duplicate native Label corruption diagnostics.

### Query

- target, summary, Nodes, exact Node, and exact Pin only;
- default Input/Output Nodes;
- actual Node and Settings Classes;
- allowed/current structured `FPCGDataTypeIdentifier` values, including
  composed IDs and `CustomSubtype`;
- persistent layout on Node-bearing operations;
- exact Node and Pin incident Edges without recursive traversal;
- pagination, result budget, and deterministic order;
- Query never changes package dirty state or Undo history;
- external Settings is classified by Outer chain and remains read-only;
- Parameters, `context`, `data flow`, Palette, related Targets, and handoffs are
  rejected as unavailable rather than partially projected.

### Patch lifecycle for later slices

- dry run leaves source Graph, package dirty state, Undo/Redo, external assets,
  Nodes, Pins, Edges, Settings, and Parameters unchanged;
- live add, certified set/reset, move, compatible direct connect, disconnect,
  break, and remove;
- local alias resolution after each Settings-driven Pin reconstruction;
- one top-level Undo restores the complete Patch;
- a later failure rolls back earlier successful statements;
- no-op plans do not dirty or create Undo;
- native and SAL readback agree after every operation family.

### PCG-specific effects

- Settings change adds, removes, renames, or retypes Pins;
- downstream dynamic propagation;
- incompatible Edges removed by Pin reconstruction;
- compatible direct connection and exact duplicate-edge no-op;
- same-Node, cycle, occupied-input, filter-required, conversion-required, and
  incompatible connection rejection before mutation;
- separately gated tests for every certified adaptive type pair, including
  full intermediate-edge readback and operation-local role parity;
- `AddLabeledEdge()` success verified by topology, not its Boolean;
- disconnect exact missing Edge failure;
- break-all secondary propagation;
- every Parameter mutation is rejected before sandbox execution;
- the entire external Settings wrapper and descendants are rejected before
  sandbox execution;
- notification-barrier release exposes only committed or restored authored
  state to observers.

### Persistence and compatibility

- terminal-only save validation;
- save dry run does not execute `PreSave` and is explicitly advisory;
- save failure retains exact Target and reports dirty in-memory state;
- successful save/unload/reload preserves Node names, Pin references,
  Settings values, positions, topology, and Parameter Guids;
- targeted path passes against the official UE 5.7 and UE 5.8 installations;
- the adapter builds without the experimental PCGToolset enabled.

Recommended native automation names are:

```text
Loomle.Sal.PCG.Query.*
Loomle.Sal.PCG.Mutation.*
Loomle.Sal.PCG.Robust.*
```

## Acceptance Requirements

The PCG Domain satisfies its Query-only family-release gate when:

- every canonical Target and StableRef round-trips after package reload;
- no identity depends on Node title, localized text, facade Guid, array index,
  or synthesized Guid;
- the advertised Target, summary, Node, Pin, schema, layout, and Edge reads are
  deterministic, bounded, zero-load where specified, and
  leave Graphs, external Settings, packages, delegates, and Undo unchanged;
- every deferred Query operation returns an exact unavailable diagnostic rather
  than a partial projection;
- `pcg` is accepted by `QueryTarget` and rejected by `PatchTarget` before
  Bridge dispatch;
- both UE 5.7 and UE 5.8 targeted Query suites pass; and
- the Client, Bridge, generated protocol, static catalog, offline card, and
  packaged artifacts advertise the same Query-only `pcg` contract.

The current branch snapshot satisfies this Query-only gate locally: the PCG
group is 7/7 on UE 5.7 and UE 5.8, the related Level and family Phase 0 suites
pass, both engine builds pass, and packaged end-to-end acceptance passes. The
RC contract is distilled into `interfaces/pcg.md`, and the protocol-v6 cards,
offline schema, and nine-Domain catalog are formed but not externally
published. External publication still requires final release-archive and
Windows validation plus the coordinated promotion gate with `level` and
`pcg_component`; PCG-local Query acceptance alone does not authorize
promotion.

The later authored mutation/save capability has a separate gate:

- exact schema is authoritative for every writable field and operation;
- all certified Pin reconstruction and connection cascades are visible in
  `planned.effects`;
- baseline connection accepts only compatible direct, unoccupied topology;
- adaptive connection is either certified for the exact engine/type-pair path
  or explicitly unavailable before live mutation;
- authored dry run and live apply use the same ordered native path and show
  within-request readback parity; save is explicitly excluded;
- one Undo restores every authored change in a successful Patch;
- rollback restores the complete Target-authored Graph snapshot after any
  later failure;
- no dry run or current-Target Patch mutates an external Settings package;
- save is independent, source-control-aware, and reload-verified;
- both UE 5.7 and UE 5.8 mutation/save suites pass; and
- the Client, Bridge, generated protocol, static card, and packaged artifacts
  advertise the same bumped Patch capability.
