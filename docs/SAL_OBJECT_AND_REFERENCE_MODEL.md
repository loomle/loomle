# SAL Object, Domain Target, And Stable Reference Model

## Status

Promoted on 2026-07-25. This document records the confirmed and implemented
design for SAL object expressions, semantic tags, Domain Targets, and stable
references. The SAL v3 model, now carried by Client-Bridge protocol v4, the SAL
Core, Client, Bridge source, current Domain cards, and site implement this
model. The complete repository suite, 135-test
UE Automation category, stripped release audit, exact-archive packaged
end-to-end workflow, and rendered-site gates have passed.

The current public contract is the v3 model in `sal/docs/` and `interfaces/`.
Constructor-shaped values, Domain-name Target constructors, and fused
`kind@id` references are legacy input accepted only by the explicit
TypeScript compatibility reader. Protocol v4 Bridge requests reject their
normalized legacy shapes.

## Decision

SAL separates data, presentation, execution scope, and identity:

```text
object value       = fields
semantic tag       = optional, erasable presentation metadata
Domain Target      = one Domain keyword + that Domain's Target fields
stable reference   = native identity path inside one exact Domain Target
```

The intended surface forms are:

```sal
{ id: "N", name: "Example" }
node { id: "N", name: "Example" }

g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

@N
@N/P
node @N
pin @N/P
```

Curly braces are the general object expression. A Domain may recommend
semantic tags such as `node` and `pin`, but a tag never supplies information
required to infer a type, choose a Domain, resolve identity, create an object,
or select an operation.

`target`, `domain`, and the value in the `domain` slot are SAL structural
syntax. In `domain: graph`, `graph` is a Domain keyword, not a semantic tag.

Parentheses are not an object representation. They remain available for true
calls, such as an explicitly invoked operation:

```sal
invoke @N Rename(displayName: "New Name")
```

The current `asset(...)`, `blueprint(...)`, `class(...)`, and `graph(...)`
forms are legacy Domain Target constructors. The new language lowers them to
`target { domain: ... }` only in the compatibility path; they are not redefined
as semantic tags.

## Core Invariants

1. `{...}` is the only ordinary object data expression.
2. Every JSON object can be represented without loss in an SAL object
   expression; the JSON-compatible subset of SAL object fields round-trips
   without loss.
3. `()` denotes a true call or another explicitly defined non-object syntax.
4. A semantic tag is optional and erasable.
5. Tag erasure cannot change type interpretation, identity, Domain selection,
   validation, planning, effects, or mutation.
6. Every Target has exactly one `domain`.
7. A Target never recursively contains another Domain Target.
8. `target.domain` is the only public mechanism that selects a Domain.
9. A native UE Class validates a Target inside the selected Domain; it never
   adds another Domain automatically.
10. A Domain owns its Target grammar, canonicalization, identity environment,
    Query, Patch, Palette, formatting, and diagnostics.
11. Stable references are native identity paths interpreted inside one exact
    Target. Tags are not identity namespaces.
12. One native UE object may have several distinct Domain Targets.
13. Cross-Domain work uses an explicit, independent Target handoff.
14. Domain Targets, semantic tags, and native `type` fields are separate
    concepts and cannot substitute for one another.

## Design Intent

The model must:

- express arbitrary JSON object fields without reserving ordinary field
  combinations such as `{kind, id}` or `{callee, args}`;
- retain readable domain vocabulary without making it an execution
  discriminator;
- follow UE's native identity and ownership boundaries;
- remain exact after unrelated objects are added or copied;
- distinguish stable object identity from a member path;
- make the active Domain explicit;
- avoid invented object-kind namespaces and parallel public routing concepts;
- fail closed when native identity is missing, incomplete, duplicated, or
  corrupt.

It does not attempt to provide one universal project-wide address for every UE
object. UE does not expose one persistent, globally resolvable identifier for
every asset, class, and editor object.

## Terms

### Domain

A Domain is SAL's syntax and UE-semantic adapter. It is the sole public owner
of:

- one closed Target field schema;
- Target discovery and exactness rules;
- native Target verification;
- one Target-relative identity environment;
- Query, Patch, Palette, and schema behavior;
- recommended semantic tags and canonical formatting;
- Domain-specific diagnostics and cross-Domain handoffs.

The current Domains are:

```text
asset
blueprint
class
graph
state_tree
widget
```

Shared C++ loaders, lookup helpers, mutation utilities, and reference-fact
extractors may remain implementation details. They do not become SAL concepts
parallel to Domain.

### Domain Target

A Domain Target is the root against which one Query or Patch executes:

```sal
target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
```

The `domain` field selects exactly one Domain. The remaining fields belong to
that Domain and are meaningless outside its Target schema.

A Target has a lifecycle, not several public Target kinds:

1. a discovery Query may supply a Domain-approved incomplete Target;
2. the Domain opens and verifies it;
3. successful readback returns the same Target form with every canonical
   identity and verification field;
4. Patch and transported stable identity require that exact form.

SAL does not expose separate selector, locator, or resolution-object syntax.
Those are validation states of the same Domain Target.

### Exact Target

An exact Target contains the complete canonical fields required by its Domain
to reopen and verify the same root. For example, a Blueprint Target uses an
Asset Path to load the object and a `BlueprintGuid` to verify that the loaded
Blueprint is the intended one.

“Exact” describes Target completeness. It is not another surface construct:

```sal
target {
  domain: blueprint,
  asset: "/Game/B.B",
  id: "11111111-1111-1111-1111-111111111111"
}
```

### Target-Relative Identity Environment

Every exact Target declares one closed identity environment in which a
StableRef is interpreted.

This is usually the Target's authored contents. A Domain may also include
native declarations that UE makes lexically available to that Target. Graph is
the important case: a Graph Domain may expose owning-Blueprint declarations
that Graph references can name, while still limiting mutation and reference
use-site scanning to the bound Graph.

This is Graph Domain behavior. It does not switch to Blueprint Domain and does
not compose two Targets.

### Native Identity Path

A native identity path is a non-empty, root-to-leaf sequence of persistent UE
identity components:

```text
[local-id]
[owner-id, local-id]
[ancestor-id, owner-id, local-id]
```

Each component must be native identity. Display names, semantic tags, inferred
types, array positions, and UI hierarchy labels are not identity components.

### Stable Reference

A StableRef is a compact native identity path:

```text
StableRef = NativeIdentityPath
ResolvedStableIdentity = ExactTarget + StableRef
```

The surrounding Query or Patch supplies the Target. A bare StableRef outside a
Target context is intentionally incomplete.

### Scoped Stable Reference

When one result document declares several exact Targets, a Target alias can
qualify a StableRef:

```sal
g::@N
g::@N/P
pin g::@N/P
```

The Target alias is real identity context. The optional leading word remains
an erasable semantic tag.

```ts
interface ScopedStableRef {
  kind: "scoped_stable_ref";
  target: LocalRef;
  reference: StableRef;
}
```

`ScopedStableRef` is result-side structure. A request may recognize a redundant
qualifier naming its one active Target, prove it, and lower it to an ordinary
StableRef. The normalized request protocol rejects `ScopedStableRef`.

### Local Reference

A local alias identifies a value declared inside one SAL document. It is
appropriate for provisional objects that do not yet have a native ID.

Local aliases are not stable references and do not survive into another
request. A successful mutation returns the native StableRef generated by UE.

### Semantic Tag

A semantic tag is a human-readable presentation annotation:

```sal
node { ... }
pin @N/P
```

Its text form is a non-reserved SAL identifier. JSON literals, the retired
generic label `object`, `target`, `domain`, the six Domain names, and the
irreducibly ambiguous exact-operation prefixes `tree`, `context`, and
`palette` are reserved and are not semantic tags. Unknown non-reserved tags
remain structurally valid so a formatter can preserve forward-compatible
presentation metadata.

It may:

- improve formatting and scanning;
- group or rank documentation and completion entries after the Domain, fields,
  and native shape have already determined the available schema;
- produce a non-error advisory when it appears unusual for the resolved
  native shape.

It may not:

- select a Domain;
- infer a type;
- choose an identity lookup set;
- validate an otherwise incomplete object;
- select Query, Patch, Palette, or creation behavior;
- add, remove, or change an available documentation, schema, or completion
  entry;
- change `valid`, `applied`, a dry-run plan, or effects.

For every valid tag:

```text
eraseTag(tag { fields }) == { fields }
eraseTag(tag @identity)   == @identity
```

Target syntax contains no `semanticTag`. Erasing all tags never touches
`Target.domain`.

If removing a tag makes an object impossible to resolve or create, the object
is missing real information. That information must come from fields, the
active Domain, the operation, the destination, the Palette identity, or the
native identity path.

## General Object Expression

The canonical surface syntax is:

```sal
{
  id: "N",
  type: "/Script/..."
}
```

A Domain may decorate the same object:

```sal
node {
  id: "N",
  type: "/Script/..."
}
```

Object keys use either a legal identifier or a JSON string:

```text
object      = [semantic_tag] "{" [member {"," member}] "}"
member      = object_key ":" expression
object_key  = identifier | json_string
```

The quoted form makes every JSON object key representable:

```sal
{
  "key-with-dash": 1,
  "key with space": true,
  kind: "ordinary-data"
}
```

The formatter emits an identifier key when legal and otherwise emits a JSON
string. Duplicate decoded keys are rejected. JSON null, Boolean, finite number,
string, array, and nested object values map recursively without changing their
data meaning. The formatter preserves `-0`; `NaN` and infinities are not JSON
numbers and are rejected. SAL references, names, and semantic tags are
additional SAL structure rather than JSON object data.

This is a representability guarantee. It does not require every JSON payload
to be rewritten as SAL when no SAL expression is needed.

### Normalized Object Shape

The normalized AST uses a structural wrapper so arbitrary user fields cannot
collide with AST nodes:

```ts
type SemanticTag = string;
// Schema: SAL identifier syntax minus the schema-declared reserved-keyword set.

interface ObjectExpr<E> {
  kind: "object";
  fields: Record<string, E>;
  semanticTag?: SemanticTag;
}
```

The schema applies `^[A-Za-z_][A-Za-z0-9_]*$` and a
`not: {enum: [...]}` containing Core and Domain keywords. Parser and schema
reserved-word sets are locked together by a conformance test; TypeScript and
Bridge validation enforce the same set. The formatter assumes already
validated normalized input.

For example:

```json
{
  "kind": "stable_ref",
  "id": "ordinary-data",
  "callee": "ordinary-data",
  "args": {}
}
```

is stored under `ObjectExpr.fields`. It cannot be mistaken for a StableRef or
legacy Call.

Constructor-like object values migrate to `ObjectExpr`. The normalized `Call`
node is not retained as an object-kind mechanism. Statement-level invocation
remains an operation rather than an object expression.

Request and result expression unions remain structurally closed:

```ts
interface MemberRef<O> {
  kind: "member";
  object: O;
  path: [string | number, ...(string | number)[]];
}

type RequestMemberRef = MemberRef<LocalRef | StableRef>;
type ResultMemberRef =
  MemberRef<LocalRef | StableRef | ScopedStableRef>;

type RequestRef =
  | LocalRef
  | StableRef
  | RequestMemberRef;

type ResultRef =
  | LocalRef
  | StableRef
  | ScopedStableRef
  | ResultMemberRef;

type RequestExpr =
  | null
  | boolean
  | number
  | string
  | Name
  | RequestRef
  | ObjectExpr<RequestExpr>
  | RequestExpr[];

type ResultExpr =
  | null
  | boolean
  | number
  | string
  | Name
  | ResultRef
  | ObjectExpr<ResultExpr>
  | ResultExpr[];
```

Target is not a tagged ObjectExpr. It is a separate structural node used in
request Target bindings, result Target tables, and explicit handoffs.

## Domain Target Model

### Surface Grammar

```text
target_expression =
    "target" "{"
    "domain" ":" domain_name
    { "," target_field ":" target_value }
    "}"

domain_name =
    "asset"
  | "blueprint"
  | "class"
  | "graph"
  | "state_tree"
  | "widget"

target_field = domain_defined_identifier
target_value = json_string
```

`domain` is required exactly once. Each Domain closes the remaining field set
and rejects unknown fields. In the current six Target schemas every
non-`domain` value is a non-empty string. A Target field cannot contain an
object, array, alias, or another Target. Graph discovery `name` is a JSON
string, not a symbolic object tag.

A Target cannot contain another Target. Native ownership and verification
facts are fields of the active Domain's Target:

```sal
target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

The following recursive form is not part of the language:

```sal
# Invalid planned syntax.
target {
  domain: graph,
  owner: target {
    domain: blueprint,
    asset: "/Game/BP_Door.BP_Door",
    id: "11111111-1111-1111-1111-111111111111"
  },
  id: "22222222-2222-2222-2222-222222222222"
}
```

### Normalized Target Shape

The normalized Target is one closed union discriminated by `domain`:

```ts
type NonEmptyString = string;
type NonEmptyArray<T> = [T, ...T[]];

interface TargetBase {
  kind: "target";
}

interface AssetRootTarget extends TargetBase {
  domain: "asset";
  path?: never;
  type?: never;
}

interface AssetPathTarget extends TargetBase {
  domain: "asset";
  path: NonEmptyString;
  type?: NonEmptyString;
}

type AssetTarget = AssetRootTarget | AssetPathTarget;

interface BlueprintTarget extends TargetBase {
  domain: "blueprint";
  asset: NonEmptyString;
  id?: NonEmptyString;
}

interface ClassTarget extends TargetBase {
  domain: "class";
  path: NonEmptyString;
}

interface GraphByIdTarget extends TargetBase {
  domain: "graph";
  asset: NonEmptyString;
  blueprintId?: NonEmptyString;
  id: NonEmptyString;
  name?: NonEmptyString;
}

interface GraphByNameTarget extends TargetBase {
  domain: "graph";
  asset: NonEmptyString;
  blueprintId?: NonEmptyString;
  id?: never;
  name: NonEmptyString;
}

type GraphTarget = GraphByIdTarget | GraphByNameTarget;

interface StateTreeTarget extends TargetBase {
  domain: "state_tree";
  asset: NonEmptyString;
  type?: NonEmptyString;
}

interface WidgetTarget extends TargetBase {
  domain: "widget";
  asset: NonEmptyString;
  id?: NonEmptyString;
}

type Target =
  | AssetTarget
  | BlueprintTarget
  | ClassTarget
  | GraphTarget
  | StateTreeTarget
  | WidgetTarget;
```

The union excludes structurally invalid states such as Asset `type` without
`path` or Graph `asset` without either `id` or `name`.

When a Graph Target supplies both `id` and `name`, `id` selects identity and
`name` is a strict readable-state assertion. A mismatch fails. Canonical
readback drops `name`, because a mutable display name is not Target identity.

Query, Patch, and result schemas apply completeness profiles to the same Target
AST node. These profiles are generated validation views, not additional
surface concepts:

```ts
interface CanonicalAssetTarget extends TargetBase {
  domain: "asset";
  path: NonEmptyString;
  type: NonEmptyString;
}

interface CanonicalBlueprintTarget extends TargetBase {
  domain: "blueprint";
  asset: NonEmptyString;
  id: NonEmptyString;
}

type CanonicalClassTarget = ClassTarget;

interface CanonicalGraphTarget extends TargetBase {
  domain: "graph";
  asset: NonEmptyString;
  blueprintId: NonEmptyString;
  id: NonEmptyString;
  name?: never;
}

interface CanonicalStateTreeTarget extends TargetBase {
  domain: "state_tree";
  asset: NonEmptyString;
  type: NonEmptyString;
}

interface CanonicalWidgetTarget extends TargetBase {
  domain: "widget";
  asset: NonEmptyString;
  id: NonEmptyString;
}

type CanonicalTarget =
  | CanonicalAssetTarget
  | CanonicalBlueprintTarget
  | CanonicalClassTarget
  | CanonicalGraphTarget
  | CanonicalStateTreeTarget
  | CanonicalWidgetTarget;

type QueryAcceptedTarget = Target;
type PatchAcceptedTarget = CanonicalTarget;
```

Input Paths, Guids, and native type strings normalize to UE's canonical
spelling. The same surface `type` field is an assertion on input and a verified
fact in canonical readback.

Every Target field defined as an `FGuid` must parse as a non-zero valid Guid
under `FGuid::IsValid()` and canonicalizes to lowercase digits-with-hyphens.
Asset and Class paths canonicalize to the exact loaded native path. Canonical
Target equality, hashing, caching, and readback use those normalized strings.

`blueprintId` is the owning `BlueprintGuid` verification field for the current
Blueprint-backed Graph Domain. It never contains a Blueprint Target. A future
non-Blueprint Graph owner must add an explicit closed Graph Target variant or a
separate Domain rather than overloading this field.

### Request Target Binding

Target bindings have one normalized landing on both request and result sides:

```ts
interface TargetBinding<T extends Target = Target> {
  alias: LocalIdentifier;
  target: T;
}

interface QueryRequest {
  kind: "query";
  target: TargetBinding<QueryAcceptedTarget>;
  operation: QueryOperation;
  where?: Condition;
  with?: NonEmptyArray<Identifier>;
  orderBy?: NonEmptyArray<OrderBy>;
  page?: Page;
}

interface PatchRequest {
  kind: "patch";
  target: TargetBinding<PatchAcceptedTarget>;
  dryRun: boolean;
  statements: NonEmptyArray<PatchStatement>;
}
```

`Identifier`, `LocalIdentifier`, `QueryOperation`, `Condition`, `OrderBy`,
`Page`, and `PatchStatement` are shared Core schema types. Existing
operation-specific validation rules continue to apply. The only replacement
inside the shared Query and Patch shapes is the old constructor-valued
`target`: it becomes `TargetBinding<QueryAcceptedTarget>` or
`TargetBinding<PatchAcceptedTarget>`. It is not wrapped around another Query or
Patch and therefore cannot create a second Target landing. The request prelude
declares the alias used by `query alias` or `patch alias`. One request contains
exactly one active Target binding.

### Domain Target Rules

| Domain | Discovery Query | Canonical exact Target | Patch |
| --- | --- | --- | --- |
| Asset | `{domain: asset}` for the collection root, or non-empty `path` with optional `type` assertion | `path` plus resolved native `type` | exact asset only |
| Blueprint | `asset`; `id` may be omitted for first discovery | `asset + BlueprintGuid` | canonical `id` required |
| Class | exact native or generated Class `path` | same exact `path` | exact `path` required |
| Graph | `asset` plus `id` or exact `name`; `blueprintId` may be omitted for discovery | `asset + BlueprintGuid + GraphGuid` | canonical `blueprintId` and `id` required |
| StateTree | `asset` with optional native `type` assertion | `asset + resolved StateTree native type` | canonical `type` required |
| Widget | `asset`; `id` may be omitted for first discovery | `asset + BlueprintGuid` | canonical `id` required |

`type` is an exact native-Class assertion inside the already selected Domain.
It never selects a Domain.

The Asset collection root is simply:

```sal
target { domain: asset }
```

It is a Query-only state of Asset Domain Target, not another root-address
concept.

### Canonical Examples

```sal
assets = target {
  domain: asset
}

doorAsset = target {
  domain: asset,
  path: "/Game/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
}

doorBlueprint = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}

actorClass = target {
  domain: class,
  path: "/Script/Engine.Actor"
}

eventGraph = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}

behavior = target {
  domain: state_tree,
  asset: "/Game/AI/ST_Behavior.ST_Behavior",
  type: "/Script/StateTreeModule.StateTree"
}

menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

### One Native Object, Several Domain Targets

Domain is an explicit semantic view, not a property inferred from the UObject.
The same native object may therefore have several Targets:

```sal
menuBlueprint = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

menuWidgets = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

These are different SAL Targets even though both open the same
`UWidgetBlueprint`:

- Blueprint Domain exposes declarations, Graph lifecycle, Components, Class
  Settings, compile, and save.
- Widget Domain exposes WidgetTree, Widgets, Slots, Widget Query, Widget
  Palette, and Widget Patch.

They do not combine StableRef sets, Palettes, summaries, or Patch operations.

Likewise, a `UStateTree` may be opened as a generic Asset Target or as a
StateTree Target. The selected `domain` determines the capability surface.

### Cross-Domain Handoff

Cross-Domain work returns another independent Target:

- Widget Event guidance returns a Graph Target.
- Widget finalization returns or requires a Blueprint Target.
- Graph finalization returns or requires a Blueprint Target.
- Asset discovery may return suggested Blueprint, StateTree, or Widget
  Targets.

A handoff is explicit data in a designated Target position. It is never hidden
capability composition and never a Target nested inside another Target.

### Graph Ownership And Child Graphs

Graph Domain owns the complete Graph Target identity. `asset` is the durable
container UObject path used by the existing Blueprint Target contract,
including embedded Blueprints; `blueprintId` verifies the owning
`BlueprintGuid`; and `id` identifies the `UEdGraph::GraphGuid`.

Top-level and child/collapsed Graphs use the same Graph Target model. Current
UE and Loomle behavior already supports this:

- `UBlueprint::GetAllGraphs()` recursively includes `UEdGraph::SubGraphs`;
- a Composite Node's `BoundGraph` is one of those child Graphs;
- the current Graph implementation queries and patches the resolved
  `UEdGraph*` without a top-level-only restriction.

The relevant source paths are:

- `Engine/Source/Runtime/Engine/Private/Blueprint.cpp`,
  `UBlueprint::GetAllGraphs`;
- `Engine/Source/Runtime/Engine/Private/EdGraph/EdGraph.cpp`,
  `UEdGraph::GetAllChildrenGraphs`;
- `Engine/Source/Editor/BlueprintGraph/Private/K2Node_Composite.cpp`,
  `UK2Node_Composite::PostPlacedNewNode`;
- `engine/LoomleBridge/Source/LoomleBridge/Private/Sal/SalTargetResolver.cpp`,
  Graph Target opening;
- `engine/LoomleBridge/Source/LoomleBridge/Private/Sal/Graph/SalGraphInterface.cpp`,
  Graph Query, dry run, and Patch.

SAL therefore does not invent a nested Graph Domain. The current exact Target
uses the native Blueprint scope plus `GraphGuid`, matching UE's
`FGraphReference` contract. If a supported UE copy path nevertheless produces
duplicate `GraphGuid` values inside one Blueprint, Graph Domain reports a
normal identity conflict. A focused regression test must verify Composite
copy, save, reload, Query, dry run, and Patch behavior, but that test does not
block the Domain or Target model.

## Canonical Target Readback

Every result after a Target opens successfully includes its canonical Target,
even if a later Query, Patch, compile, or save step fails.

```ts
type CanonicalTargetBinding = TargetBinding<CanonicalTarget>;
type DomainRootTargetBinding = TargetBinding<AssetRootTarget>;

interface TargetHandoff {
  kind: "target_handoff";
  purpose: NonEmptyString;
  target: LocalRef;
}

interface ExactTargetResultContext {
  targetContext: "exact_target";
  target: CanonicalTargetBinding;
  relatedTargets?: CanonicalTargetBinding[];
  handoffs?: TargetHandoff[];
}

interface DomainRootResultContext {
  targetContext: "domain_root";
  target: DomainRootTargetBinding;
  relatedTargets?: CanonicalTargetBinding[];
  handoffs?: TargetHandoff[];
}

interface UnresolvedTargetResultContext {
  targetContext: "unresolved_target";
  target?: never;
  relatedTargets?: never;
  handoffs?: never;
}

interface UnresolvedQueryFailurePayload extends Result {
  diagnostics: [Diagnostic, ...Diagnostic[]];
}

interface UnresolvedMutationFailurePayload extends MutationResult {
  isError: true;
  valid: false;
  applied: false;
  diagnostics: [Diagnostic, ...Diagnostic[]];
}

type ExactQueryResult = ExactTargetResultContext & Result;
type ExactMutationResult = ExactTargetResultContext & MutationResult;
type DomainRootQueryResult = DomainRootResultContext & Result;
type UnresolvedQueryResult =
  UnresolvedTargetResultContext & UnresolvedQueryFailurePayload;
type UnresolvedMutationResult =
  UnresolvedTargetResultContext & UnresolvedMutationFailurePayload;

type ObjectResult =
  | ExactQueryResult
  | ExactMutationResult
  | DomainRootQueryResult
  | UnresolvedQueryResult
  | UnresolvedMutationResult;
```

`Result`, `MutationResult`, `Diagnostic`, and `LocalRef` are shared Core schema
types. The generated JSON Schema expands the five `ObjectResult` branches into
closed merged leaves rather than intersecting independently closed objects.
Both unresolved leaves require at least one `severity: error` diagnostic.

Rules:

- `exact_target` carries one Domain-validated `CanonicalTarget`, including
  post-open operation failures;
- `domain_root` is currently the Query-only
  `target { domain: asset }`;
- `unresolved_target` carries at least one error diagnostic and cannot seed
  StableRef scope;
- MutationResult is forbidden under `domain_root`;
- every `relatedTargets` entry is another canonical, independent Domain
  Target;
- `relatedTargets` are structurally deduplicated, never repeat the main Target,
  and every entry is referenced by Object Text or a handoff;
- diagnostics may refer to a Target alias already retained by Object Text or a
  handoff, but a diagnostic alone cannot retain a `relatedTargets` entry;
- aliases are unique across the Target table and Object Text bindings;
- each handoff names one `relatedTargets` alias through `LocalRef`; it never
  embeds a Target;
- an exact foreign Target is never reconstructed from an object's tag.

Result text uses an explicitly marked Target table:

```sal
result exact_target
target g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
objects
call = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/..."
}
```

A multi-Target result may add independent related Target lines and explicit
handoffs:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

The envelope grammar is:

```text
result_text =
    result_context newline
    ["target" target_binding newline]
    {"related" target_binding newline}
    {"handoff" handoff_purpose "to" identifier newline}
    object_section

result_context =
    "result exact_target"
  | "result domain_root"
  | "result unresolved_target"

handoff_purpose = identifier | json_string
target_binding = identifier "=" target_expression
object_section =
    "objects" newline object_text
  | "no_objects" newline
```

`exact_target` requires one canonical `target` line. `domain_root` requires one
Asset root `target` line. `unresolved_target` forbids all Target and handoff
lines. Each `related` line maps one-to-one and in order to
`relatedTargets`; each `handoff` line maps one-to-one and in order to
`handoffs`.

`TargetHandoff.purpose` is any non-empty string. The formatter emits it as a
bare identifier when it is legal in the `handoff_purpose` position and
otherwise as a canonically escaped JSON string. The parser decodes both forms
to the same string, so normalized handoffs always round-trip.

The parser validates every Target before seeding alias scope, then parses
Object Text. The formatter emits canonical Target fields and never reconstructs
a Target from an object tag or native display state.

Under `exact_target`, an unqualified StableRef is relative to the main Target.
A reference relative to `relatedTargets` uses `alias::@identity`. Under
`domain_root`, every StableRef is qualified because there is no exact default
object root; the Asset root alias itself cannot qualify a StableRef. Under
`unresolved_target`, StableRefs are forbidden.

Zero-load Asset Registry or Find-in-Blueprints evidence that lacks canonical
Target verification fields remains an ordinary object:

```sal
{
  assetPath: "/Game/BP_Door.BP_Door",
  indexedNodeGuid: "N",
  graphDisplayName: "Event Graph",
  exactTargetAvailable: false
}
```

Display labels and indexed IDs are provenance. The formatter must not invent
missing Target fields.

## Stable Reference Model

### Canonical Identity Rule

For an object `O` in Target `T`'s identity environment:

```text
if O.nativeId is unique in T by UE's native contract:
    identityPath(O, T) = [O.nativeId]
else:
    identityPath(O, T) =
        identityPath(O.nativeIdentityOwner, T) + [O.localNativeId]
```

The rule uses UE's identity contract, not the number of matches that happen to
exist in the current asset.

A Graph Pin therefore remains `@NodeGuid/PinId` even when its `PinId` is
currently unique across the Graph. Copying an unrelated Node must not change
whether an existing reference is exact.

### Injectivity Contract

Within every valid exact Target:

```text
identityPath(A) == identityPath(B) implies A == B
```

If a Domain cannot establish this property, it must:

1. include an additional native owner identity component;
2. report an identity conflict; or
3. decline to expose a cross-request StableRef for that object.

A semantic tag, native Class, display name, or current array index cannot make
an otherwise ambiguous identity valid.

Injectivity is checked across every object category that shares one path shape
inside the active Domain Target. If a Blueprint Variable Guid and Graph Guid
are equal, `@G` is a conflict even though legacy `variable@G` and `graph@G`
look different.

The Domain builds a read-only union of its native lookup sets and succeeds only
when exactly one object matches. It never retries by tag.

### Text Form

```sal
@N
@N/P
@A/B/C

node @N
pin @N/P
```

Whitespace between an optional tag and `@` is required. Domain collection
words such as `nodes` and `pins` do not appear in the identity path.

```text
stable_ref       = [semantic_tag whitespace] "@" identity_segment
                   { "/" identity_segment } [member_path]
identity_segment = bare_identity_segment | json_string
```

The formatter uses a compact bare segment only for a closed safe character
set and otherwise emits a JSON string:

```sal
@N/P
@"owner/part"/"leaf.with.dot".Value
```

An identity component whose Domain schema declares native `FGuid` semantics
must parse as a valid Guid and formats as lowercase digits-with-hyphens.

### Normalized Reference Shape

```ts
interface StableRef {
  kind: "stable_ref";
  identityPath: [NonEmptyString, ...NonEmptyString[]];
  semanticTag?: SemanticTag;
}
```

`semanticTag` is excluded from equality, hashing, lookup, planning, and
mutation.

### Member Paths

Identity paths and member paths are distinct:

```sal
@N/P.Value[0]
```

```text
identity path = [N, P]
member path   = [Value, 0]
```

A member path can become stale when the object's schema changes. It does not
turn the member into an independently stable object.

### Exact Object Query

Exact lookup of a contained object is one structural Query operation:

```sal
query g
@N
with schema
```

An optional tag decorates the same operand:

```sal
query g
node @N
with schema
```

Both normalize to:

```ts
interface ExactObjectOperation {
  kind: "object";
  target: StableRef;
}
```

The active Target's Domain is already known before `@N` is interpreted. That
Domain searches its complete identity environment and requires exactly one
object. The resolved native shape may choose an object-specific schema inside
the Domain, but it never chooses another Domain.

The exact Target itself is read structurally and does not receive a synthetic
StableRef:

```sal
query g
target
with schema
```

```ts
interface TargetOperation {
  kind: "target";
}
```

### Exact Relationship Subject

The bound Target can be a relationship subject without a synthetic StableRef:

```sal
query functionGraph
references to target
```

```ts
interface TargetSelfRef {
  kind: "target_self";
}

interface StableMemberRef {
  kind: "member";
  object: StableRef;
  path: [string | number, ...(string | number)[]];
}

interface TargetSelfMemberRef {
  kind: "member";
  object: TargetSelfRef;
  path: [string | number, ...(string | number)[]];
}

type ExactRelationshipSubject =
  | TargetSelfRef
  | TargetSelfMemberRef
  | StableRef
  | StableMemberRef;
```

This union defines structural request shape, not universal native capability.
The current provider resolves bare `target` only for callable Function/Macro
Graph Targets, plus the direct `target.InterfaceGuid` member when that Graph
has a valid implemented-Interface declaration identity. Unsupported Domain or
Graph roles return `capability.reference_unavailable` with exact Target
context.

Other declarations use Target-relative StableRefs:

```sal
references to @MemberVariableGuid
references to @TopLevelFunctionGraphGuid/LocalVariableGuid
```

Whether `target` is a valid declaration subject remains Domain- and native
role-specific. It does not broaden the relationship scan scope.

## Domain Identity And Capability Matrix

### Native Identity

| Domain / object | Exact Target scope | Native identity path | Notes |
| --- | --- | --- | --- |
| Asset | canonical Asset Target | Target itself | Asset Path is Target identity; rename or move changes it |
| Blueprint | Asset Path + `BlueprintGuid` | Target itself | Path loads, Guid verifies |
| Blueprint Variable / Dispatcher | Blueprint | `@VarGuid` | One combined Blueprint one-segment audit |
| Blueprint Graph, including child/collapsed Graph | Blueprint | `@GraphGuid` | `GetAllGraphs()` scope; duplicates fail closed |
| Function-local Variable | Blueprint | `@GraphGuid/VarGuid` | Local Guid is Function-Graph-scoped |
| SCS Component | Blueprint | `@USCS_Node.VariableGuid` | Combined one-segment audit |
| Blueprint-scoped referenceable Node | Blueprint | `@NodeGuid` | UE supplies Blueprint-wide Node lookup and duplication repair |
| Class | canonical Class Path | Target itself | No universal persistent Class Guid |
| Reflected Property / Function | Class | no StableRef in the current contract | Exact names and native owner paths remain Query data |
| Graph | durable Blueprint container path + `BlueprintGuid` + `GraphGuid` | Target itself | Same Target form for top-level and child Graphs |
| Graph Node | Graph | `@NodeGuid` | Copy creates a new Node Guid |
| Graph Pin | Graph | `@NodeGuid/PinId` | Pin identity includes its owning Node |
| StateTree | Asset Path + native type | Target itself | No separate persisted asset Guid |
| StateTree State | StateTree | `@State.ID` | Combined one-segment StateTree audit |
| StateTree Node | StateTree | `@EditorNode.ID` | Same set across Node roles |
| StateTree Transition | StateTree | `@Transition.ID` | Same set across categories |
| StateTree Parameter | StateTree | `@ContainerGuid/PropertyGuid` | Property Guid is container-local |
| StateTree Context | StateTree + current Schema | `@DescriptorGuid` | Only a valid, unique, completely scanned Descriptor Guid is referenceable |
| Widget | Asset Path + `BlueprintGuid` | Target itself | Widget Domain root |
| Authored Widget | Widget | `@WidgetGuid` | From `WidgetVariableNameToGuidMap` |

The confirmed owner-local StableRefs are:

1. Graph Pin: `NodeGuid/PinId`;
2. StateTree Parameter: `ContainerGuid/PropertyGuid`;
3. Function-local Variable under Blueprint:
   `GraphGuid/VarGuid`.

Blueprint Node remains one segment because UE exposes
`FBlueprintEditorUtils::GetNodeByGUID(Blueprint, NodeGuid)` across all Nodes in
the Blueprint and regenerates Blueprint-wide Node Guids during duplication.
Pin identity is different: UE's native Pin reference contract includes the
owning Node.

Inside an exact top-level Function Graph Target, the Target already supplies
the Function Graph owner and its local Variable may use `@VarGuid`. A child
Graph inside that Function still uses
`@TopLevelFunctionGraphGuid/VarGuid` because the native owner is outside the
bound child Graph.

Child Graph is not another Domain or Target shape. Its currently supported
identity remains `GraphGuid`, matching UE's `FGraphReference`; a duplicate
inside one Blueprint is an invalid identity state and fails closed. The
focused copy experiment verifies that normal authoring remains valid and that
any corrupted duplicate produces the required diagnostic. It does not change
the Target shape or introduce a nested Domain.

### Identity Environments

| Active Domain Target | One segment | Multiple segments |
| --- | --- | --- |
| Asset | none | none |
| Blueprint | combined Variable, Dispatcher, Graph, SCS Component, and referenceable Node sets | `GraphGuid/VarGuid` |
| Class | none in the current StableRef contract | none |
| Graph | current-Graph Nodes plus explicitly declared owning-Blueprint declarations and, for a top-level Function Graph, its local Variables | `NodeGuid/PinId`; an outer top-level Function scope uses `GraphGuid/VarGuid` |
| StateTree | combined State, Node, Transition, and Context sets | `ContainerGuid/PropertyGuid` |
| Widget | authored Widgets only | none currently |

Path length is not a universal type code. It only narrows the lookup sets
declared by the active Domain.

Graph's owning-Blueprint declarations are read and relationship capabilities
of Graph Domain. They do not authorize Blueprint mutation. Widget Domain does
not inherit Blueprint Domain identity sets.

### Operation Ownership

| Domain | Owns | Explicit handoff |
| --- | --- | --- |
| Asset | asset discovery, canonical exact Target readback, generic asset save | Blueprint, StateTree, Widget, or another asset-backed Domain Target |
| Blueprint | settings, Variables, Dispatchers, top-level Graph lifecycle, SCS Components, compile, save | Graph Target for Graph body |
| Class | Reflection reads and supported generated-Class Default edits/save | Blueprint Target when explicit Blueprint compile is required |
| Graph | bound Graph, Nodes, Pins, Edges, Graph schema, Graph Palette | Blueprint Target for compile/save |
| StateTree | StateTree Query, Patch, Palette, compile, save | other Domains only when a returned relationship requires one |
| Widget | WidgetTree, Widgets, Slots, Widget Query/Patch/Palette | Graph Target for events; Blueprint Target for compile/save |

One request has one active Domain. No native Class, operation name, tag,
Palette prefix, or StableRef kind can switch it.

## Capability And Creation Routing

Routing follows one order:

1. read `Target.domain`;
2. let that Domain validate and open its Target fields;
3. let that Domain interpret the structural Query or Patch operation;
4. resolve StableRefs only inside that Domain Target's identity environment;
5. resolve creation through that Domain's exact Palette identity and
   destination;
6. execute that Domain's plan or return an explicit Target handoff.

There is no second public capability-selection layer.

### Creation Binding

A Palette-backed creation binding is an ordinary object:

```sal
print = {
  palette: "P_PrintString"
}

add print
```

A formatter may add an erasable tag:

```sal
print = node {
  palette: "P_PrintString"
}
```

Both forms behave identically. The active Domain, `add`, the Palette identity,
and any destination supply the real creation information.

Palette IDs remain opaque. A string prefix may aid diagnostics but cannot
select a Domain.

For Graph creation, a Palette identity is resolved in the exact live Graph
Target that produced it. Native dry-run preflight may execute the creation
against an isolated Blueprint sandbox, but the sandbox is not a second
capability-discovery environment. Blueprint-owned `UFunction` and `FProperty`
actions are remapped to the corresponding sandbox member before UE materializes
the Node; native and external actions retain their original UE identity. A
missing action reports `resolution.palette_not_found`, while an action that is
present but cannot materialize a Node reports
`resolution.palette_not_spawnable`. Exact Palette schema advertises `bind` and
`add` only for an action that can materialize in the current Graph context.

### Mutation Scope

Resolving a StableRef proves identity, not mutation authority.

For example, Graph Domain may resolve an owning Blueprint Variable as a
relationship subject, but Graph Patch cannot mutate that Variable. It returns
a registered capability diagnostic such as
`capability.operation_unavailable` and may return a canonical Blueprint Target
and handoff for a separate request. It never changes Domain implicitly.

Widget and Blueprint authored edits likewise require separate Targets and
requests. There is no mixed Widget/Blueprint Patch.

## Resolution Contract

Stable identity processing is ordered and exact:

1. `Target.domain` selects the Domain;
2. the Domain validates and opens the Target;
3. the Domain selects every identity lookup set accepting the path shape;
4. each component resolves only inside its native owner scope;
5. the union must contain exactly one final object;
6. any following member path resolves on that object.

It never:

- searches outside the Domain Target's declared environment;
- expands reference use-site scope merely because an owner declaration is
  visible;
- retries by display name, native Class, semantic tag, or array position;
- accepts an incomplete native owner path because it happens to have one
  current match;
- switches Domain after examining the resolved native object.

Required diagnostics include:

- `resolution.insufficient_scope`;
- `resolution.object_not_found`;
- `resolution.identity_conflict`;
- `resolution.target_not_found`;
- `resolution.field_not_found`.

A recovery diagnostic may perform a broader read-only search and return
canonical candidates. The incomplete reference still fails.

## Stability Boundary

Stable means faithful to UE's native identity lifecycle, not immortal.

A reference remains valid across operations that preserve the exact Target and
every identity component, commonly including save, reload, recompile, rename,
and non-identity-changing movement.

A reference can become stale when:

- the object is deleted or replaced;
- a copy receives a new native identity;
- a native identity owner changes;
- a StateTree Schema replacement changes the native owner of Context
  descriptors;
- an Asset or Class address changes;
- reconstruction replaces a native identity component;
- the Target fails its verification identity.

Graph Node copy illustrates the model. UE gives the copied Node a new
`NodeGuid` while corresponding Pins may retain their `PinId`:

```text
original Pin = @N1/P
copied Pin   = @N2/P
```

There is no collision because the native owner scope is present.

## Legacy Migration

This change affects text, normalized JSON, Client types, Bridge validation, and
all six Domains. It requires a Client-Bridge protocol version increment.

### Legacy Object Constructors

For one migration window:

```sal
node(...)       # legacy object constructor
node { ... }    # new tagged object
{ ... }         # new untagged object
```

The compatibility reader converts `object(...)` to an untagged
`ObjectExpr.fields`. Any other accepted legacy object constructor retains its
name as an erasable `semanticTag`; a reserved callee is rejected rather than
silently discarded.

The new formatter emits only brace-based object syntax and never constructor
parentheses. Whether it emits a recommended, erasable tag follows the Domain's
canonical presentation policy.

### Legacy Domain Target Constructors

Legacy Target calls are parsed only in Target context:

```sal
asset(...)
blueprint(...)
class(...)
graph(...)
```

The compatibility path projects them to one flat Target:

```text
legacy target call
→ read the legacy arguments and aliases
→ choose the legacy Domain mapping
→ flatten native address and verification fields
→ emit target { domain: ..., ... }
→ let that Domain validate it
```

The compatibility projector uses a closed field policy:

| Legacy constructor | Structural fields retained |
| --- | --- |
| `asset` | `path`; optional `type` assertion |
| `blueprint` | `asset`; optional `id` |
| `class` | `path` |
| `graph` | flattened Blueprint `asset` and `id`; Graph `id` or `name`; copied descriptive `type` cannot be verified in the TypeScript-only reader and is rejected |

Unknown fields are rejected. No assertion field is silently discarded.
In particular, a Graph `type` or a nested owner Asset `type` cannot be verified
by the TypeScript-only reader and fails explicitly. Unknown aliases, alias
cycles, and an owner whose legacy shape does not match the expected constructor
fail before Domain entry. Every declaration must be in the selected Target's
alias-dependency closure. Unused declarations and mixtures of explicit v3 and
legacy Targets are rejected rather than ignored.

For example:

```sal
bp = blueprint(
  asset: "/Game/B.B",
  id: "11111111-1111-1111-1111-111111111111"
)
g = graph(asset: bp, id: "22222222-2222-2222-2222-222222222222")
```

becomes:

```sal
g = target {
  domain: graph,
  asset: "/Game/B.B",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
```

The version-pinned compatibility reader does not infer Domain from the request
body or from a native Class. Its direct TypeScript API requires `legacyDomain`
when one old constructor could name more than one v3 Domain:

- `asset(...)` requires an explicit choice between `asset` and `state_tree`;
- `blueprint(...)` requires an explicit choice between `blueprint` and
  `widget`;
- `class(...)` and `graph(...)` have only their same-named Domain projection.

The default SDK facade and MCP tools do not enable this path. The compatibility
reader never splits a mixed request or silently guesses; it materializes one
explicit Domain Target before normal planning. The new formatter never infers
a Domain.

Legacy Target constructors are not semantic tags and are never accepted as
ordinary new object syntax.

### Legacy Typed References

For one migration window:

```sal
node@N          # legacy kind-qualified reference
node @N         # new tag + StableRef
@N              # new tag-free StableRef
```

The compatibility reader retains the fused kind until the active Domain is
known, then applies this closed lowering table:

| Active Domain | Accepted legacy kind and native identity |
| --- | --- |
| `asset`, `class` | none; these Domains expose no contained StableRefs |
| `blueprint` | `dispatcher`, `graph`, `component`, or `node` with one Guid; function-local `variable` with `GraphGuid/VarGuid` |
| `graph` | `node`, `dispatcher`, or `component` with one Guid; `pin` with `NodeGuid/PinId`; function-local `variable` with `GraphGuid/VarGuid` |
| `state_tree` | `state`, `node`, `transition`, or `object` with one Guid; `parameter` with `ContainerGuid/PropertyGuid` |
| `widget` | `widget` with one Widget Guid |

Every component is canonicalized as a non-zero GUID. A legacy kind outside the
active Domain's row is rejected. A fused spelling for the exact Target itself
is also rejected: exact Target reads use the `target` operation, and
relationship subjects use the structural `target` / `target.<member>` form.

No legacy kind reaches hashing, mutation planning, response formatting, or
storage.

There is no UE-assisted owner recovery and no generic `rawId.split("/")`.
StateTree Parameters, Graph Pins, and function-local Variables split only
their specified two-component native contracts. Missing owner scope is
rejected rather than resolved from current matches. A one-component form must
contain exactly one Guid; an embedded `/` therefore fails instead of becoming
an opaque or guessed identity.

### Normalized JSON

A legacy Graph Target Call:

```json
{
  "kind": "call",
  "callee": "graph",
  "args": {
    "asset": {
      "kind": "call",
      "callee": "blueprint",
      "args": {
        "asset": "/Game/B.B",
        "id": "11111111-1111-1111-1111-111111111111"
      }
    },
    "id": "22222222-2222-2222-2222-222222222222"
  }
}
```

becomes:

```json
{
  "kind": "target",
  "domain": "graph",
  "asset": "/Game/B.B",
  "blueprintId": "11111111-1111-1111-1111-111111111111",
  "id": "22222222-2222-2222-2222-222222222222"
}
```

An ordinary object Call:

```json
{
  "kind": "call",
  "callee": "node",
  "args": {
    "palette": "P"
  }
}
```

becomes:

```json
{
  "kind": "object",
  "fields": {
    "palette": "P"
  },
  "semanticTag": "node"
}
```

The next protocol version is allocated at implementation time. Mismatched
Client and Bridge builds continue to fail before dispatch.

## Implementation Plan

### Phase 1: Freeze The Planned Contract

- freeze `{...}` as the universal object expression;
- freeze `()` as a non-object delimiter for true calls, condition grouping,
  and Graph coordinate pairs;
- freeze tag syntax, erasure semantics, and advisory-only diagnostics;
- freeze the six-value `domain` keyword set;
- freeze the closed Target fields and Query/Patch completeness rules for every
  Domain;
- freeze one-Target/one-Domain and no-nested-Target validation;
- inventory every Domain identity set and cross-category injectivity audit;
- inventory every Palette-backed creation path that currently depends on a
  constructor name;
- inventory every current native-Class or interface-composition dispatch and
  map it to an explicit Domain Target;
- freeze result Target tables, scoped StableRefs, and cross-Domain handoff
  representation;
- allocate the protocol version and legacy removal window.

### Phase 2: Normalized Model And Parser

- replace object `Call` values with `ObjectExpr`;
- replace Target `Call` values with the closed `Target` union;
- parse `target { domain: ... }` only as structural Target syntax;
- reject a missing, duplicate, unknown, or nested Target Domain;
- parse `{...}`, `tag {...}`, `@id`, and `tag @id`;
- parse `targetAlias::@id[.Member]` in result context;
- in request text, accept that qualifier only when it names the one active
  Target, lower it to an unscoped StableRef, and reject every foreign alias;
- add `identityPath` and optional `semanticTag`;
- keep member paths separate from identity paths;
- keep legacy constructors and typed refs isolated from the new normalized
  unions;
- implement legacy Target alias flattening, closed field projection, and
  StateTree Parameter slash handling; reject root-self fused references and
  every under-scoped form that would require owner recovery;
- reject ambiguous Asset/StateTree and Blueprint/Widget legacy mappings and
  mixed legacy Domain Patches;
- make the formatter emit only canonical new syntax;
- regenerate TypeScript types and embedded schema data.

### Phase 3: Client-Bridge Protocol

- increment the protocol version;
- update Client serialization, validators, fixtures, memory executor, and
  protocol tests;
- validate the Target closed union before dispatch;
- carry the canonical Target on every post-open success and failure result;
- add independent `relatedTargets` and scoped result references;
- add normalized handoffs that refer only to canonical `relatedTargets`;
- generate the closed five-branch result union for exact Query, exact
  mutation, Domain-root Query, unresolved Query, and unresolved mutation;
- keep unresolved and Domain-root result states closed;
- keep protocol mismatch rejection ahead of tool dispatch.

### Phase 4: Domain Entry And Identity

- dispatch first and only by `Target.domain`;
- make each Domain validate, canonicalize, and open its Target fields;
- expose StateTree and Widget as explicit Domains;
- remove Asset-to-StateTree and Blueprint-to-Widget automatic capability
  composition from the new protocol path;
- make every Domain resolve tag-free ObjectExpr values;
- replace kind-selected identity lookup with each Domain's aggregate identity
  environment;
- implement Graph Pin `NodeGuid/PinId`;
- retain StateTree Parameter `ContainerGuid/PropertyGuid`;
- implement Function-local Variable `GraphGuid/VarGuid`;
- add cross-category duplicate detection;
- enforce Graph owner-declaration read scope without granting Blueprint
  mutation;
- enforce Asset `type` assertions for all assets;
- return canonical Targets and StableRefs in Query, Palette, Patch, mutation
  results, diagnostics, and handoffs.

### Phase 5: Domain Capability Migration

- make Palette identity, destination, operation, and object fields replace
  constructor-name routing;
- separate Blueprint and Widget Query, Patch, Palette, summary, and identity
  environments;
- route Widget events to explicit Graph Targets;
- route Graph and Widget finalization to explicit Blueprint Targets;
- keep StateTree Query, Patch, Palette, compile, and save inside StateTree
  Domain;
- reject any operation unavailable in the active Domain rather than trying
  another Domain.

### Phase 6: Public Surface Migration

- promote this design into `sal/docs/LANGUAGE_CORE.md` and
  `sal/docs/DOMAINS.md`;
- update all six Domain cards and resident interface cards;
- regenerate the interface catalog;
- migrate examples, schema discovery, reference queries, site documentation,
  and root README;
- document the compatibility window and removal release.
- remove the compatibility reader only after its versioned diagnostics,
  ambiguous-Domain cases, closed safe-reference conversions, and unsafe
  root-reference rejection have shipped for the promised window.

### Phase 7: Verification

The migration is not complete until all of the following pass:

- arbitrary object fields, including `kind`, `id`, `callee`, and `args`,
  round-trip without collision;
- quoted JSON keys and nested JSON objects round-trip without renaming or
  information loss;
- `{...}` and `tag {...}` normalize to identical fields;
- erasing all tags leaves Domain selection, Target verification, identity,
  validation, planning, effects, and mutation unchanged;
- a tag never supplies native type or creation semantics;
- Domain and Core keywords are rejected as semantic tags;
- Target rejects zero, two, or unknown `domain` values;
- Target rejects a recursively nested Domain Target;
- Target syntax is rejected inside ordinary ObjectExpr fields;
- all six Domain Target forms round-trip;
- Asset `type` without `path` and Graph without either `id` or `name` are
  rejected;
- Graph `name` is a strict discovery assertion and is absent from canonical
  Target equality/readback;
- a discovery Target returns the same Domain's canonical exact Target;
- Patch rejects a Domain-incomplete Target;
- `target { domain: asset }` is Query-only;
- `type` mismatch fails inside the selected Domain and never changes Domain;
- the same `UWidgetBlueprint` has separate Blueprint and Widget Targets with
  disjoint capabilities and identity environments;
- the same `UStateTree` has separate Asset and StateTree Targets without
  automatic capability composition;
- Widget Event, Widget finalization, and Graph finalization return explicit
  independent Target handoffs;
- top-level and child/collapsed Graphs use the same Graph Target form;
- Composite/collapsed Graph Query, dry run, apply, save, reload, and copy paths
  pass focused UE Automation;
- a duplicate Graph Guid fails with a normal identity-conflict diagnostic;
- `@N` and `tag @N` resolve identically;
- `@N/P` and `tag @N/P` resolve identically;
- Graph Pins copied with retained `PinId` remain distinct through
  `NodeGuid/PinId`;
- StateTree Parameters with repeated Property Guids remain distinct through
  their container paths;
- Function-local Variables with repeated VarGuids remain distinct through
  their Graph paths;
- cross-category equal paths fail rather than falling back to a tag;
- Graph owner Blueprint declarations can be read without widening Graph-local
  use-site scans or mutation scope;
- Widget Domain cannot resolve Blueprint-only StableRefs;
- Palette creation works identically with and without presentation tags;
- no Palette prefix or legacy constructor name selects a Domain;
- every exact result carries a canonical Target;
- post-open Query or mutation failure retains that canonical Target;
- Domain-root result is Query-only, and unresolved Query/mutation results each
  require an error diagnostic;
- every foreign result reference carries an exact related Target scope;
- related Targets are canonical, deduplicated, distinct from the main Target,
  and actually referenced by Object Text or a handoff;
- a diagnostic-only related Target is rejected as redundant;
- every handoff points to one related Target alias and never embeds a Target;
- the complete ResultText envelope round-trips context, main Target, related
  Targets, handoffs, aliases, and object presence;
- zero-load evidence never fabricates canonical Target fields;
- no legacy Target Call or typed StableRef reaches the new planning model;
- legacy target-self/root fused references fail rather than becoming synthetic
  StableRefs or structural Target-self operations;
- legacy StateTree Parameter paths split only under their closed old contract;
- ambiguous legacy StateTree/Asset and Widget/Blueprint requests fail rather
  than guessing a Domain;
- schema, TypeScript validation, Bridge validation, formatter, and parser
  accept the same model;
- SAL unit tests, Client tests, UE Automation, packaged end-to-end tests, and
  site build all pass.

## Promotion Record

This design became the current SAL contract after:

1. the normalized schema and parser implement it;
2. all six Domains accept and return the new Target and object model;
3. the allocated protocol version is generated on both sides;
4. current public documentation and generated interface artifacts are updated;
5. the complete verification matrix passes.

All five conditions were first satisfied by protocol v3. Protocol v4 adds
Editor transport operations without changing this SAL model. Current normative
behavior lives in `sal/docs/`, `interfaces/`, and the generated protocol
artifacts; this file remains the design and migration record.
