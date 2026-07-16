# SAL SDK Design

## Intent

The SDK turns agent-facing SAL Text into a small normalized request contract,
calls one configured executor, validates its response, and formats ordered SAL
Object Text. It owns language mechanics; UE remains the source of truth for
object meaning and edit legality.

## Facade

```ts
interface Sal {
  query(text: SalText): Promise<TextResult>;
  patch(text: SalText): Promise<TextResult>;
  schema(module?: string): Promise<TextResult>;
}

interface CreateSalOptions {
  executor: SalExecutor;
}

declare function createSal(options: CreateSalOptions): Sal;
```

`query` and `patch` accept self-contained SAL Text. Target locators, Query
clauses, Patch order, and dry-run intent are in that Text, not side parameters.

`schema()` returns the compact active-module index. `schema(module)` returns
one static interface card. Neither form calls the executor. Current-instance
discovery remains a normal exact Query using `with schema`.

## Pipeline

```txt
SAL Text
  -> parse and pure normalization
  -> SalObject validation
  -> executor
  -> ObjectResult validation
  -> ordered Object Text formatting
  -> TextResult
```

The parser owns:

- delimiter-aware statement boundaries and comments;
- expressions, Calls, names, values, bindings, and references;
- target locator expansion;
- Query operations and clauses;
- ordered Patch bindings and operations;
- language diagnostics with source spans.

The parser may perform only state-independent rewrites. It must not inspect UE
objects, resolve Palette entries, infer types, validate Pins, or choose a domain
implementation.

The executor owns:

- resolving generic target Calls against active interfaces;
- selecting composed capabilities from the resolved native object;
- Query capability and state validation;
- exact-object `with schema` content;
- Palette discovery and creation-entry resolution;
- UE field, relationship, and operation semantics;
- Patch preflight, dry run, apply, and verification;
- capability, resolution, and validation diagnostics.

## Normalized Requests

All requests use one generic target:

```ts
interface Target {
  alias: string;
  value: Call | Name;
}
```

Leading locator bindings recursively expand into `Target.value`. `alias` is
presentation context only. A collection root such as `query asset` becomes a
`Name`; Patch requires a bound `Call`. There is no `RequestTarget`, public
`domain`, or Asset/Blueprint/Graph/Widget target union.

Only `Target.alias` remains available to the request body. Intermediate locator
aliases disappear during expansion. Other local references must resolve to an
earlier binding or `invoke` output; executor results are rejected if their
ordered Object Text violates the same rule. Result validation starts with an
empty local-alias scope: an executor must declare a compact target or owner
binding before any returned statement refers to that alias.

```ts
interface Query {
  kind: "query";
  target: Target;
  operation: QueryOperation;
  where?: Condition;
  with?: string[];
  orderBy?: OrderBy[];
  page?: Page;
}

interface Patch {
  kind: "patch";
  target: Target;
  dryRun: boolean;
  statements: PatchStatement[];
}

type PatchStatement = Binding | PatchOperation;
```

Query has exactly one primary operation. Plural operations enumerate or search,
singular operations resolve exact current names, and typed stable references
resolve exact IDs. Relationship reads such as `tree`, `context`, `exec flow`,
and `data flow` are direct operations rather than `find` variants.

Patch order is semantic. Bindings and operations remain in one `statements`
array; no `bindings`/`ops` parallel arrays or wrapper statement are allowed.

## Shared Expressions And References

```ts
type Expr =
  | null | boolean | number | string
  | Name | Ref | Call
  | Expr[] | { [key: string]: Expr };

interface LocalRef { kind: "local"; name: string; }
interface StableRef { kind: string; id: string; }
interface MemberRef {
  kind: "member";
  object: LocalRef | StableRef;
  path: string[];
}

type Ref = LocalRef | StableRef | MemberRef;
```

Existing native objects use a typed stable reference when UE supplies a stable
ID. Objects without one retain their native Path or scoped exact name. New
objects use aliases until the executor returns native identity. Constructor
names describe SAL structure; `type` and other native fields remain UE text.

## Results

Query and Patch share one content model:

```ts
interface ObjectText {
  statements: Statement[];
}

type Statement = Binding | Edge | Comment;

interface Result {
  object?: ObjectText;
  diagnostics: Diagnostic[];
  page?: { next?: string };
}

interface MutationResult extends Result {
  isError: boolean;
  dryRun: boolean;
  valid: boolean;
  applied: boolean;
  assetPath?: string;
  operation: string;
  resolvedRefs?: unknown;
  planned?: unknown;
  diff?: unknown;
  previousRevision?: string;
  newRevision?: string;
}

type ObjectResult = Result | MutationResult;
```

Bindings, edges, and comments are serialized in reading order. The formatter
must not regroup them into domain arrays. There are no `GraphResult`,
`ClassResult`, `AssetResult`, `BlueprintResult`, `WidgetResult`, or
`PaletteResult` payloads.

Mutation fields describe execution around ordinary Object Text. A dry run uses
the real parse, resolve, validate, and plan path and stops before apply. Optional
revision fields remain absent until the concrete executor enforces them.

Public formatting produces:

```ts
interface TextResult {
  text?: SalText;
  diagnostics: Diagnostic[];
  page?: ResultPage;
  isError?: boolean;
  dryRun?: boolean;
  valid?: boolean;
  applied?: boolean;
  assetPath?: string;
  operation?: string;
  resolvedRefs?: unknown;
  planned?: unknown;
  diff?: unknown;
  previousRevision?: string;
  newRevision?: string;
}
```

## Executor Contract

```ts
interface SalExecutor {
  readonly interfaces: readonly string[];
  query(object: Query): Promise<Result>;
  patch?(object: Patch): Promise<MutationResult>;
}
```

The facade calls exactly one executor. It does not route by a public domain
field. The executor may internally compose domain services after resolving the
generic target against real object state.

The generic in-memory executor and its graph, asset, Blueprint, and widget
wrappers are deterministic SDK fixtures. They prove request/result, ordering,
pagination, dry-run, and facade behavior; they do not claim UE-complete
semantics.

## Schema And RPC Boundary

`schema/sal-object.schema.json` is the source of truth for normalized requests,
Object Text, results, mutation fields, and diagnostics. Generated TypeScript is
written to `src/generated/sal-object-schema.ts`.

The UE Bridge boundary carries schema-valid normalized JSON in both directions:

```txt
TypeScript SDK -> normalized JSON RPC -> UE Bridge -> normalized JSON -> SDK
```

TypeScript keeps SAL parsing and formatting. C++ owns codecs and every operation
that depends on UE state. A direct RPC caller must receive the same structural
validation as an SDK caller. Loomle's UE 5.7 plugin exposes this executor as
`sal.query` and `sal.patch`; an SDK host supplies the transport-specific
`SalExecutor` wrapper around those two RPC calls.

## Diagnostics

The SDK produces `language.*` diagnostics. Executors produce
`capability.*`, `resolution.*`, and `validation.*` diagnostics. All public codes
are registered in `diagnostics/catalog.json`; all results are validated before
formatting.

## Verification

The default test gate checks:

- generated schema types are current;
- valid and invalid normalized fixtures;
- diagnostic registration;
- invalid syntax and source spans;
- Text -> Object -> Text round trips;
- current example requests;
- facade result validation and mutation-field preservation;
- generic and interface-specific memory executor loops.
