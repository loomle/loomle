# SAL Reference Queries

> Status: implemented for the confirmed UE 5.7 Blueprint, Graph, and Widget
> scopes in the normalized SDK and Bridge. Contract tests and a full UE 5.7
> Mac arm64 `BuildPlugin` compile pass; live Editor integration remains to be
> exercised against representative project assets.

## Intent

`references` finds authored static use-sites of one exact declaration. It gives
agents the equivalent of source-code Find References for non-text UE objects
without reducing UE identity to display names or Find-in-Blueprints text.

Reference discovery is a shared Query relationship. Domains still own the UE
objects, native identity fields, and use-site extractors that make the query
factual. SAL does not add a `Reference`, `ReferenceResult`, symbol table, or
translated UE type system.

## Query Text

The first confirmed subject forms are an existing kind-qualified stable reference and
an optional domain-owned member path:

```sal
query door
references to variable@health-guid

query eventGraph
references to node@variable-get-guid

query eventGraph
references to node@call-on-member-guid.FunctionReference in project
page limit 50
```

The primary operation is:

```text
references to <exact-subject> [in project]
```

`<exact-subject>` is initially `<stable-ref>` or
`<stable-ref>.<native-member-path>`. It is resolved inside the complete bound
target and therefore never becomes a global id lookup. A later domain may add
another already-defined exact native selector, but that selector requires its
own reviewed domain design. This document does not silently add direct Class,
Asset, Function, or Property subject syntax.

`references` accepts shared cursor `page` clauses. It does not accept `where`,
`order by`, `with`, or `depth`. Query text retains one primary operation and
one clause per line.

### Normalized Request

Reference discovery is one shared Query operation:

```ts
interface ReferencesOperation {
  kind: "references";
  target: StableRef | StableMemberRef;
  scope?: "project";
}

interface StableMemberRef {
  kind: "member";
  object: StableRef;
  path: (string | number)[];
}
```

The outer `Query.target` says where the Query is bound. The nested
`ReferencesOperation.target` says whose references are requested. This follows
the existing relationship-operation convention instead of introducing a
parallel `subject` term. `StableMemberRef` is only the normalized restriction
of existing Member Reference text such as `node@id.FunctionReference`; it is
not a new SAL object or text form. Domains with indexed native member paths use
`[N]`, normalized as a numeric segment.

Omitting `scope` selects the bound local target. `scope: "project"` is the
exact normalized form of `in project`. Shared `Query.page` remains outside the
operation. No normalized Reference result type is added.

## Declaration Resolution

Every successful query resolves exactly one canonical UE declaration identity.
Canonical identity is internal adapter state, not a new SAL object or text
form.

An exact subject is resolved in this order:

1. If the object itself is an authored declaration, it is the target.
2. Otherwise the owning adapter extracts the object's native static reference
   targets.
3. If exactly one target exists, the object is a convenient use-site subject
   for that declaration.
4. If several targets exist, the adapter does not choose or union them. It
   returns an ambiguity diagnostic whose compact `matches` are complete,
   copyable replacement primary-operation lines under the same Query binding.
5. An explicit member path selects the reference-bearing native field and is
   resolved to one declaration.
6. If no target can be resolved, the query returns a capability or unresolved
   reference diagnostic rather than an empty result.

For example, a Variable Get Node has one `VariableReference`, so the Node alone
is sufficient:

```sal
query eventGraph
references to node@variable-get-guid
```

`UK2Node_CallFunctionOnMember` carries two independent reference facts. Its
candidate queries reuse existing SAL Member References and exact UE field
names:

```sal
references to node@call-on-member-guid.FunctionReference
references to node@call-on-member-guid.MemberVariableToCallOn
```

There is no `via`, `role`, or `where role` syntax. `with schema` on the exact
object may expose supported reference-bearing members and copyable query
guidance through ordinary comments.

Only existing legal Member References may be returned as candidates. If a
compound use-site cannot express one target through its current public member
paths, the diagnostic requires the direct declaration subject; it never
invents an array index, role selector, or partial native name.

An object can be a declaration while also storing other relationships. The
bare object targets its own declaration; an explicit member path selects an
embedded reference. Definition artifacts such as a Function Entry resolve
through their owning declaration rather than becoming separate symbols.

## Static Reference Boundary

`references` reports authored static identity, matching the declaration stored
by UE. It does not infer runtime behavior.

- A virtual or interface call references its declared function slot. Possible
  runtime implementations are not ordinary references.
- A dynamic Class or object value driven through data flow is not guessed from
  runtime possibilities.
- An opaque string that happens to contain an object or member name is not a
  static reference.
- C++, TypeScript, Verse, and other text source references are outside this UE
  authored-object query.
- Broken or stale reference data remains unresolved evidence. The adapter must
  not turn a same-named object into a factual match.

Native members without a Blueprint GUID can still have an exact current
identity through authoritative owner path and native name. That identity is
not claimed to survive a native rename.

## Search Scope

Without `in project`, the bound Query target defines the complete local search
scope. A Graph target searches that Graph. A Blueprint target searches its
owned authored state. Other domains must document their corresponding target
scope. The operation never silently ascends to an owner or expands to other
assets.

Search scope and declaration ownership are distinct. Under a Graph target,
Blueprint-owned subjects such as member Variables, Dispatchers, Components,
source Widgets, and Function or Macro Graphs still resolve in the owning
Blueprint; this does not expand the use-site scan beyond the bound Graph. A
`node@id` remains Graph-owned, and a local `variable@id` resolves through the
bound Graph's top-level Function Graph so Collapsed Graphs retain the real UE
local scope.

`in project` selects project-owned authored content instead of the local target
scope:

```text
/Game
+ enabled and mounted Project, Mod, and External plugin content
+ current loaded, dirty, and newly created authored state in those roots
```

It excludes `/Engine`, Engine and Enterprise plugin content, `/Script`,
`/Temp`, `/Memory`, previews, and other transient objects. `project` means the
project's creatable content, not every root mounted by the current engine.

For Blueprint references, the active supported provider set and UE's registered
`FBlueprintAssetHandler` classes define the container universe inside those
roots. It includes `UBlueprint` subclasses, World Level Script Blueprints,
Level Sequence Director Blueprints, and containers registered by loaded editor
modules. Before taking the scan snapshot, Loomle must ensure its supported
provider modules have registered or return an incomplete diagnostic. The
handler class set is part of the cursor snapshot; a later registration
invalidates that cursor.

The no-`page.next` completeness claim is relative to this frozen, advertised
provider and container universe. If Loomle detects an in-scope container that
its active providers cannot extract or verify, it returns an error diagnostic
instead of claiming complete project coverage.

The registered Handler set is not assumed to cover every possible embedded
Blueprint container. During the project snapshot, assets outside that set that
advertise Blueprint-bearing state through native tags such as
`BlueprintPathWithinPackage` or versioned/unversioned FiB data are unsupported
container sentinels. Loomle returns an incomplete/error diagnostic for such a
container unless an active provider can extract and verify it. This catches UE
5.7 cases such as an enabled container type forwarding Director Blueprint tags
without registering an `FBlueprintAssetHandler`.

## Results

The declaration itself is not a result. Results are the existing Blueprint,
Graph, Node, Widget, or other authored objects that contain matching use-sites,
serialized through ordinary ordered Object Text.

There is no result wrapper or parallel reference array. A use-site that stores
several reference facts may appear in queries for several declarations. When
the matching fact would otherwise be unclear, the adapter places one adjacent
comment after the existing object using the UE field name:

```sal
owner = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
g = graph(asset: owner, id: "graph-guid")
call = node(
  graph: g,
  id: "call-on-member-guid",
  type: "/Script/BlueprintGraph.K2Node_CallFunctionOnMember"
)
# matched through MemberVariableToCallOn
```

Single-target use-sites need no repeated target id or role comment. The request
already states the target. Domain adapters preserve useful native authored
order locally and a deterministic order across project containers. Reference
queries do not add `order by`.

Pagination counts distinct authored use-site objects and defaults to 50. If
several fields on one object match the same declaration, that object appears
once and its matching field comments follow in stable native-field order.
`page limit` counts objects, not individual reference facts.

Override and implementation declarations are not silently mixed into use-site
results. They are distinct UE relationships and require a separately reviewed
query if exposed later.

## Pagination And Completion

Reference queries reuse the shared cursor contract:

```sal
query door
references to variable@health-guid in project
page limit 50

query door
references to variable@health-guid in project
page limit 50
page after "cursor-from-previous-result"
```

`Result.page.next` means either that buffered matches remain or that the
incremental project scan still has work. Agents do not distinguish those
states; they pass the cursor back unchanged. A page may therefore contain
fewer than its limit, or no matches, while still returning `page.next`.

When `Result.page` is present, its non-empty `next` field is required. An
in-progress scan with no matches on the current page can therefore return only
`diagnostics` and `page.next`; it does not need a placeholder Object Text or a
second scan-status field. Every returned Object Text page is self-contained
and repeats any compact owner bindings needed to read that page independently.

The requested scope is complete only when the result has no `page.next`, no
error diagnostic, and no `validation.reference_scan_incomplete` diagnostic of
any severity. Registry, container extraction, index coverage, reference
resolution, or provider failures must produce diagnostics rather than a false
complete empty result.

Asset Registry discovery must also be settled before completion. While
`IAssetRegistry::IsLoadingAssets()` is true, the scan either continues behind
`page.next` until the registry settles or returns an incomplete diagnostic. A
mounted-root, handler-set, or registry-generation change invalidates the
cursor.

The opaque cursor binds at least the canonical declaration, bound request,
scope, deterministic ordering, project/container snapshot, and relevant
project generation. A related authored change invalidates the cursor. The
agent must restart instead of combining pages from inconsistent project state.

## UE 5.7 Identity Mapping

The first Blueprint-facing providers preserve these native identities:

| Declaration | Canonical authored identity | Common use-site state |
| --- | --- | --- |
| Blueprint member Variable | owner Blueprint/Class + `VarGuid` | `UK2Node_Variable::VariableReference` |
| Function local Variable | owner Blueprint + top-level Function Graph/function scope + local `VarGuid` | `VariableReference.MemberScope` + `MemberGuid` |
| Dispatcher | owner Blueprint/Class + Variable `VarGuid` | `UK2Node_BaseMCDelegate::DelegateReference` |
| SCS Component | owner Blueprint/Class + `USCS_Node::VariableGuid` | Variable references mapped back to the SCS Node; bound-event component field |
| source Widget | owner Widget Blueprint + `WidgetVariableNameToGuidMap` GUID | generated member reference when a real generated Property exists; Widget binding destination |
| Function Graph | owner Blueprint + `UEdGraph::GraphGuid` | `UK2Node_CallFunction::FunctionReference.MemberGuid` |
| Macro Graph | owner Blueprint + `UEdGraph::GraphGuid` | `UK2Node_MacroInstance::MacroGraphReference` |
| Custom Event | owner Blueprint + `UK2Node_CustomEvent::NodeGuid` | Function reference GUID and Create Delegate selection GUID |
| native Function or Property | authoritative owner path + native name | resolved `FMemberReference` |

The Dispatcher Signature Graph is backing signature state; the Dispatcher
Variable remains the public declaration. Component use-sites that store only a
property name must resolve the real property and map it back to the owning SCS
Node before comparing `VariableGuid`. Widget bindings may contain destination,
source Function, and multi-segment source-path references; each matching fact
retains its native field or path provenance.

Function, Macro, and Blueprint Interface declaration Graphs can own reference
identity. A concrete override or Interface implementation Function Graph also
retains its own generated Function identity through its `GraphGuid`; a call
that names that concrete Class may store this GUID. Its Function Entry
`FunctionReference` and `InterfaceGuid` separately express parent-signature or
Interface relationships and must not replace the concrete authored identity.
Ordinary `references` matches the identity actually stored by each use-site.

Event Graphs, Construction Script, Collapsed Graphs, Dispatcher Signature
Graphs, and Timeline Graph/backing state do not become independent reference
declarations merely because they have a `GraphGuid`. Parent, Interface,
override, and implementation navigation remains a separate relationship.

## Project Scan Principles

Project reference discovery has a strict zero-implicit-load boundary. A Query
must never call `GetAsset`, `LoadPackage`, or an equivalent asset-loading path
merely to inspect another container. It follows these UE principles:

1. Resolve and preserve the raw native reference subject before following UE
   redirects or mutable `FMemberReference` fixups.
2. Build the project snapshot from Asset Registry state, then apply project-root
   and Blueprint-container filtering without resolving or loading asset Classes.
3. If a container is already loaded, inspect its current native Blueprint so
   dirty and newly authored state wins over its saved index. `FastGetAsset(false)`
   is observation, not permission to load a missing object.
4. If a container is unloaded, inspect only its saved Find-in-Blueprints data.
   Decode the Asset Registry tag directly and preserve the Blueprint path, Node
   GUID, saved short Node Class label, and native searchable field path. FiB stores a
   schema-produced Graph display label, not `GraphGuid` or the native
   `UEdGraph::GetName()`, so that label is provenance only and must never become
   a `graph(name)` locator. Do not invent Graph or Blueprint GUIDs that FiB does
   not contain.
5. Treat FiB as a versioned fact source with explicit coverage, not as a reason
   to load candidates. UE 5.7 FiB can factually expose
   `UK2Node_Variable::VariableReference`; facts absent from that index version
   remain unverified. FiB `FText` lookup decoding runs off the game thread so
   UE's String Table bridge cannot start an asset load. If the lookup contains
   String-Table-backed text that would need resolution, fail that container
   closed as incomplete instead of resolving or loading the table.
6. A missing, stale, oversized, corrupt, or insufficient index produces partial
   matches plus `validation.reference_scan_incomplete`. It never silently means
   zero references and never triggers automatic recaching, saving, or loading.
7. Cancellation is checked throughout snapshot traversal, before and after the
   one-shot FiB decode, and during decoded-tree traversal. Stopping the client
   request prevents the next container from starting and abandons the active
   container at its next checkpoint. UE's FiB deserializer is not interruptible,
   so cancellation latency is bounded by one index decode; Loomle caps that
   input at 512 Ki characters. No later container continues as detached work.
8. Advance bounded work incrementally behind the ordinary page cursor. Do not
   expose a separate public job model.
9. A Function local Variable cannot be referenced outside its owning Blueprint
   and top-level Function scope. `in project` therefore resolves to the same
   complete native owner-Blueprint scan instead of enumerating project assets.

Standard native reference structs such as `FMemberReference` and
`FGraphReference` provide reusable extractors. Domain providers supplement
native compound cases such as Component Bound Events, Create Delegate, and
Widget bindings. Opaque runtime strings are outside the static contract rather
than silently treated as provider coverage.

## Diagnostics

Diagnostics must distinguish at least these situations:

- the exact object is not a referenceable declaration or use-site;
- a use-site has several targets and requires one member-path candidate from
  the diagnostic's compact `matches`;
- native reference state is stale or cannot resolve;
- corrupted state makes the requested declaration or an emitted stable
  use-site identity ambiguous;
- an opaque cursor no longer matches project state;
- project enumeration, index extraction, or verification failed before
  completeness was established;
- an unloaded container has no usable index, or the requested native fact is
  outside the saved index's declared coverage;
- the active request was cancelled.

Zero results are valid only after the selected subject and entire requested
scope have been resolved and verified successfully.

Multi-target ambiguity reuses `Diagnostic.matches`. Its entries are complete,
copyable primary-operation strings under the current Query binding, for
example `references to node@id.FunctionReference`. No candidate or Reference
result object is introduced.

## Implementation Audit — 2026-07-20

The UE 5.7 Bridge implements synchronous local Graph and Blueprint-backed
scans. Project scope freezes the Asset Registry snapshot, project content roots,
and supported Blueprint-container Class paths. Already loaded containers use
native authored state; unloaded containers are never loaded and use their saved
FiB index only.

The first page asks Asset Registry only for recursive project content roots and
registered Blueprint-container Class families, then freezes and sorts that
filtered snapshot without enumerating Engine, unrelated plugin, or unrelated
asset content. Per-container extraction advances incrementally
behind `page.next`, with bounded work per request. FiB lookup and JSON decoding
use a bounded private reader on a worker thread; String-Table-backed lookup text
fails closed before matching and never crosses back to the game thread. Local
matches are collected synchronously and use the same cursor only when their
returned objects require more than one page. The RPC control message is handled
while the provider runs; cancellation becomes terminal at the next scan
checkpoint, with at most the current bounded FiB decode finishing.

The cursor is process-local, single-sequence scan state. It binds the request,
canonical declaration, effective page limit, project roots, handler set,
container snapshot, and authored generation. Any observed authored change is
treated conservatively and invalidates an active cursor; an expired, replayed,
or invalidated cursor must restart at the first page. Asset Registry
`OnAssetUpdated` is deliberately not an authored-generation signal: UE 5.7
also emits it when Loomle's own asset load refreshes PostLoad registry data.
Live edits remain covered by object modification and in-memory asset events;
external disk refreshes use `OnAssetUpdatedOnDisk`.

An indexed result uses only identities the index actually stores. A Blueprint
binding may therefore omit `id`. For an unloaded container, it is returned as a
factual containing Blueprint with the matching `NodeGuid`, Node Class, and Graph
display label in provenance Comments; the Node Class value is FiB's saved short
label, and no Graph or Node binding is fabricated.
After that Blueprint is explicitly opened, the same local Query returns exact
Graph and Node locators from native state. Missing index coverage is reported on
the final page and is not repaired by an implicit load.

## Deferred Relationships

This first design does not add direct Class Function/Property subject syntax,
Asset dependency or referencer queries, runtime dispatch targets, override
chains, implementations, or text-source reference search. Those capabilities
may reuse the same factual principles, but each needs its UE identity and
public Query shape reviewed before entering SAL.
