# SAL SDK Design

## Scope

The TypeScript SDK owns:

- normalized request and result types;
- generated JSON Schema;
- parser and canonical formatter;
- static interface catalog;
- explicitly opted-in legacy parsing for migration tools;
- fixtures and schema/round-trip tests.

It does not resolve UE objects, infer Domains, interpret native identity,
choose Palette entries, validate UE capabilities, or execute mutations.

## Structural Types

### ObjectExpr

```ts
interface ObjectExpr<E> {
  kind: "object";
  fields: Record<string, E>;
  semanticTag?: SemanticTag;
}
```

`fields` is the only ordinary object data. `semanticTag` is retained
presentation metadata in the AST, but it is excluded from native identity,
lookup, routing, validation, planning, and execution comparisons.

The generated semantic-tag schema uses the identifier pattern and excludes the
Core/Domain reserved-word set. Schema and parser copies are locked by an exact
conformance test; Bridge and interface validation must enforce the same closed
set.

### Target

```ts
type QueryTarget =
  | AssetTarget
  | BlueprintTarget
  | ClassTarget
  | GraphTarget
  | StateTreeTarget
  | WidgetTarget
  | LevelTarget
  | PcgTarget
  | PcgComponentTarget;

type PatchTarget =
  | CanonicalAssetTarget
  | CanonicalBlueprintTarget
  | ClassTarget
  | CanonicalGraphTarget
  | CanonicalStateTreeTarget
  | CanonicalWidgetTarget;

type Target = QueryTarget; // compatibility alias
```

Every branch has `kind: "target"` and a literal `domain`. Target branches are
closed and flat. All address/verification fields are non-empty strings.

```ts
interface TargetBinding<T extends QueryTarget = QueryTarget> {
  alias: LocalIdentifier;
  target: T;
}
```

Query accepts Domain discovery forms. Patch accepts only generated canonical
Target profiles from its separate `PatchTarget` union. `level`, `pcg`, and
`pcg_component` are Query-only in protocol v6 and do not enter that union.

### References

```ts
interface StableRef {
  kind: "stable_ref";
  identityPath: [NonEmptyString, ...NonEmptyString[]];
  semanticTag?: SemanticTag;
}

interface ScopedStableRef {
  kind: "scoped_stable_ref";
  target: LocalRef;
  reference: StableRef;
}

interface MemberRef<O> {
  kind: "member";
  object: O;
  path: [string | number, ...(string | number)[]];
}
```

Normalized request unions accept only unscoped StableRefs. Source request text
may redundantly qualify a reference with its own active Target alias; parsing
lowers that spelling to an unscoped StableRef. Foreign and unknown qualifiers
are rejected. Result unions additionally accept ScopedStableRefs for related
Target context.

### Expression Unions

```ts
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

Target is intentionally absent from both Expr unions. Target appears only in a
Target binding or related Target table.

## Requests

```ts
interface QueryRequest {
  kind: "query";
  target: TargetBinding<QueryTarget>;
  operation: QueryOperation;
  where?: Condition;
  with?: [Identifier, ...Identifier[]];
  orderBy?: [OrderBy, ...OrderBy[]];
  page?: Page;
}

interface PatchRequest {
  kind: "patch";
  target: TargetBinding<PatchTarget>;
  dryRun: boolean;
  statements: [PatchStatement, ...PatchStatement[]];
}
```

Domain operation unions remain closed. Cross-field capability checks run after
structural schema validation and reject ignored clauses.

The exact Target Query uses `{kind: "target"}`. Contained-object exact lookup
uses:

```ts
interface ExactObjectOperation {
  kind: "object";
  target: StableRef;
}
```

The Target relationship subject uses `{kind: "target_self"}` rather than a
fabricated reference. This is structural request shape, not universal native
capability: the current Bridge resolves bare Target-self only for callable
Function/Macro Graph Targets and resolves the direct
`target.InterfaceGuid` member for a valid Graph Interface declaration.
Unsupported Domain or Graph roles return
`capability.reference_unavailable`.

## Ordered Object Text

```ts
interface ObjectText {
  statements: Statement[];
}

type Statement = Binding | Edge | Comment;
```

The statements array is the only reading order. The SDK never regroups results
into parallel object-kind arrays. A formatter walks the array once and
preserves comment adjacency and reference dependencies.

Creation values are ordinary ObjectExpr:

```json
{
  "kind": "object",
  "fields": {
    "palette": "P_PrintString"
  },
  "semanticTag": "node"
}
```

The tag may be absent with no normalized execution difference.

## Results

```ts
interface TargetHandoff {
  kind: "target_handoff";
  purpose: NonEmptyString;
  target: LocalRef;
}

interface ExactTargetResultContext {
  targetContext: "exact_target";
  target: TargetBinding<CanonicalTarget>;
  relatedTargets?: TargetBinding<CanonicalTarget>[];
  handoffs?: TargetHandoff[];
}

interface DomainRootResultContext {
  targetContext: "domain_root";
  target: TargetBinding<AssetRootTarget>;
  relatedTargets?: TargetBinding<CanonicalTarget>[];
  handoffs?: TargetHandoff[];
}

interface UnresolvedTargetResultContext {
  targetContext: "unresolved_target";
}
```

The public `ObjectResult` is a closed union of:

- exact Query;
- exact Mutation;
- Domain-root Query;
- unresolved Query;
- unresolved Mutation.

Generated schema expands those five leaves directly. It does not use
intersections of independently closed objects.

Both unresolved leaves require at least one error diagnostic. Domain-root
Mutation is impossible.

Related Targets are canonical, structurally deduplicated, and referenced by
Object Text or handoffs. Handoffs point to related aliases and never embed
Targets.

### MCP Content Boundary

The normalized `ObjectResult` keeps structured diagnostics and, for mutations,
execution metadata such as `dryRun`, `valid`, `applied`, `planned`, and
revisions. Those fields do not become lines in canonical Result Text.

The Client maps one result to ordered MCP text content blocks:

1. `content[0]` is only the canonical, round-trippable Result Text produced
   from result context, Target table, handoffs, and optional Object Text.
2. A later independent text block may contain mutation metadata formatted as
   SAL comments.
3. A later independent text block may contain diagnostics formatted as SAL
   comments.

The Client never concatenates blocks 2 or 3 onto block 1. Only real
`ObjectText.statements` may appear after the first block's `objects` marker.
When normalized `object` is absent, block 1 ends with `no_objects`; that token
is a strict terminator, and the Result Text parser rejects any following line
in the same block.

Metadata and diagnostics do not imply object presence. They cannot cause an
absent `object` to format as `objects`, create an empty synthetic object, or
make `no_objects` non-terminal. Consumers parse block 1 as Result Text and
treat later blocks as transport annotations, not as more Result Text.

## Text Codec

The canonical parser and formatter support:

- brace ObjectExpr with quoted arbitrary keys;
- optional semantic tags;
- flat Target bindings;
- Target-relative StableRefs with quoted identity segments;
- result-only scoped references;
- explicit result context, Target table, related Targets, and handoffs;
- ordered Object Text;
- Query and Patch operations.

Canonical formatting is deterministic:

- safe keys and segments use identifiers/bare text;
- everything else uses canonical JSON strings;
- Target fields use Domain-defined order;
- Guid strings use canonical lowercase hyphenated form after Bridge
  canonicalization;
- ordinary objects always use braces;
- statement order never changes.

## Validation Layers

1. lexical and grammar validation;
2. generated structural JSON Schema;
3. shared cross-field language validation;
4. Domain operation/Target capability validation;
5. Bridge native resolution and UE validation.

SDK validation can prove that a Graph Target has required strings. Only the
Bridge can prove they identify the same native Blueprint and Graph.

## Compatibility Reader

The direct TypeScript parser has one protocol-version-pinned, explicitly
opted-in reader that lowers legacy forms before new request validation. MCP
tools and the default SDK facade remain strict:

- object call values to ObjectExpr;
- legacy Asset/Blueprint/Class/Graph Target calls to flat Target branches;
- fused kind references to Target-relative StableRefs only when the active
  Domain makes the complete native identity shape unambiguous;
- old native-Class-based StateTree/Widget routing to one explicit Domain only
  when the complete request is unambiguous.

Compatibility nodes never enter canonical hashing, planning, storage, or
formatting. Only `object(...)` may lower without a semantic tag; reserved
constructor names fail rather than losing their callee. Mixed legacy Domains,
explicit-v3/legacy Target mixtures, Target declarations outside the selected
alias-dependency closure, target-self fused references, under-scoped owner
identities, and any form requiring UE-assisted recovery fail explicitly. Asset
and Class accept no fused references. Blueprint, Graph, StateTree, and Widget
accept only the closed safe shapes specified in Language Core.

The current formatter emits no legacy object calls, Target calls, or fused kind
references.

## Generation And Tests

Generated artifacts include:

- JSON Schema and its embedded TypeScript modules, including semantic-tag
  exclusions;
- protocol types;
- static interface catalog;
- protocol version.

Tests cover:

- JSON-compatible ObjectExpr round trip;
- tag-erasure identity, lookup, validation, planning, and execution
  equivalence while the AST may retain presentation metadata;
- reserved tag rejection;
- closed Target fields and completeness profiles;
- Target nesting rejection;
- StableRef owner paths and quoted segments;
- request/result reference separation;
- all five result leaves;
- related Target and handoff invariants;
- compatibility lowering and canonical reformatting;
- parser, schema, formatter, and generated-artifact parity.
