# SAL Diagnostics

## Intent

Diagnostics are agent navigation data. They should explain what failed, where
it failed, what layer owns the failure, and what the agent can do next.

Human-readable `message` text is useful, but agents and tests should depend on
stable fields such as `code`, `path`, `span`, `supported`, and `matches`.

## Shape

All SDK, bridge, and domain diagnostics use the same normalized shape:

```ts
interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  path?: DiagnosticPath;
  span?: SourceSpan;
  domain?: string;
  operation?: string;
  ref?: string;
  expected?: unknown;
  actual?: unknown;
  supported?: unknown;
  matches?: unknown[];
  suggestion?: string;
}

type DiagnosticPath = Array<string | number>;

interface SourceSpan {
  line: number;
  column: number;
  length?: number;
}
```

Required fields:

- `severity`: error level.
- `code`: stable machine-readable code.
- `message`: concise human-readable description.

Optional fields:

- `path`: normalized JSON path, such as `["where", "field"]` or `["ops", 0]`.
- `span`: SAL source span when text source is available.
- `domain`: domain that produced or owns the diagnostic.
- `operation`: patch operation or query operation involved.
- `ref`: unresolved or ambiguous reference.
- `expected`: expected shape, value, edge, type, or operation.
- `actual`: observed shape, value, edge, type, or state.
- `supported`: supported values or capability alternatives.
- `matches`: candidate matches for ambiguity diagnostics.
- `suggestion`: one direct next action when the repair is clear.

## Layers

Diagnostic codes use layer prefixes. The prefix is part of the public contract.

### `language.*`

Language diagnostics report malformed SAL text or malformed normalized JSON.
They do not require UE state.

Examples:

- `language.invalid_operation`
- `language.unsupported_condition`
- `language.invalid_order_by`
- `language.invalid_page`
- `language.invalid_object_shape`
- `language.invalid_result_shape`

Example:

```json
{
  "severity": "error",
  "code": "language.invalid_order_by",
  "message": "Invalid order by clause.",
  "path": ["orderBy", 0],
  "span": { "line": 4, "column": 1 },
  "expected": "order by <field> [asc|desc]",
  "actual": "order by score descending",
  "suggestion": "Use: order by score desc"
}
```

### `capability.*`

Capability diagnostics report language-valid requests that the resolved target
or its active interface does not support.

Examples:

- `capability.interface_unavailable`
- `capability.unsupported_query_operation`
- `capability.patch_unavailable`

Example:

```json
{
  "severity": "error",
  "code": "capability.unsupported_query_operation",
  "message": "The resolved target does not support data_flow.",
  "operation": "data_flow",
  "supported": ["summary", "nodes", "context"],
  "suggestion": "Use sal_schema({ module: \"graph\" }) to inspect the active interface."
}
```

### `resolution.*`

Resolution diagnostics report valid requests whose target, reference, or
context cannot be resolved, or resolves ambiguously.

Examples:

- `resolution.target_not_found`
- `resolution.object_not_found`
- `resolution.binding_not_found`
- `resolution.edge_not_found`

Example:

```json
{
  "severity": "error",
  "code": "resolution.target_not_found",
  "message": "Target /Game/BP_Door.BP_Door was not found.",
  "path": ["target"],
  "ref": "/Game/BP_Door.BP_Door",
  "suggestion": "Run query asset with assets \"BP_Door\" to discover the asset path."
}
```

Future ambiguity codes should include compact `matches` and must be registered
before an executor emits them.

### `validation.*`

Validation diagnostics report target-state or operation legality failures. They
usually require a resolved target and are common in patch planning.

Examples:

- `validation.field_unavailable`

Example:

```json
{
  "severity": "error",
  "code": "validation.field_unavailable",
  "message": "NodeComment is not writable on this object.",
  "operation": "set",
  "path": ["statements", 0],
  "ref": "node@A001.NodeComment",
  "suggestion": "Query node@A001 with schema before choosing a writable field."
}
```

### `runtime.*`

Runtime diagnostics report discovery, compatibility, connection, transport,
cancellation, timeout, shutdown, and internal execution failures between the
MCP client and the active Loomle Bridge.

Examples:

- `runtime.not_found`
- `runtime.incompatible`
- `runtime.request_cancelled`
- `runtime.editor_shutting_down`
- `runtime.internal_error`

### `tool.*`

Tool diagnostics report failures at the public MCP tool or private Bridge tool
invocation boundary, before a valid SAL operation reaches a domain interface.

Examples:

- `tool.invalid_arguments`
- `tool.unknown`

## Rules

1. Codes are stable. Messages may improve over time.
2. Do not merge unrelated failures into one diagnostic.
3. Return multiple diagnostics when multiple independent repairs are possible.
4. Include `supported` for capability failures whenever the set is small.
5. Include `matches` for ambiguity when candidates are useful and compact.
6. Include `path` for normalized JSON diagnostics and `span` for text-source
   diagnostics when available.
7. Avoid free-form diagnostic fields. Add fields to the shared schema when a
   new kind of repair data becomes common.

## Diagnostic Catalog

All public diagnostic codes should be registered in a lightweight catalog. The
catalog is for consistency and tests; it is not a heavy runtime error system.

Location:

```txt
diagnostics/catalog.json
diagnostics/catalog.schema.json
```

Each catalog entry records the stable code contract:

```ts
interface DiagnosticDefinition {
  code: string;
  layer: "language" | "capability" | "resolution" | "validation" | "runtime" | "tool";
  defaultSeverity: "error" | "warning" | "info";
  title: string;
  requiredFields?: DiagnosticField[];
  optionalFields?: DiagnosticField[];
}

type DiagnosticField =
  | "path"
  | "span"
  | "domain"
  | "operation"
  | "ref"
  | "expected"
  | "actual"
  | "supported"
  | "matches"
  | "suggestion";
```

Example:

```json
{
  "code": "capability.unsupported_where_field",
  "layer": "capability",
  "defaultSeverity": "error",
  "title": "Unsupported where field",
  "requiredFields": ["domain", "actual", "supported"],
  "optionalFields": ["path", "suggestion"]
}
```

Catalog rules:

1. Every public diagnostic `code` must have one catalog entry.
2. The code prefix must match the entry's `layer`.
3. `defaultSeverity` supplies a default, but callers may still send a different
   severity when the situation is genuinely warning or info level.
4. The catalog should define field expectations, not dynamic message text.
5. `message` may be written at the call site so it can include local context.
6. Tests should fail when SDK or bridge diagnostics use an unregistered code.
7. Generated constants or helper APIs may come from the catalog later, but the
   catalog should stay readable JSON.

## SDK Rules

The SDK produces `language.*` diagnostics from parsing, pure normalization, and
schema validation. Parser-specific `ParseError` values should be converted into
the shared diagnostic shape.

The SDK executor boundary may produce `capability.*`, `resolution.*`, and
`validation.*` diagnostics. Its test-only generic memory executor is a
contract fixture for a UE-backed executor, so its diagnostic behavior should
follow the same layer boundaries.

The SDK should validate normalized objects before executor calls and validate
executor results before formatting. Schema validation failures should become
`language.invalid_object_shape` or `language.invalid_result_shape`.

## Bridge Rules

The bridge produces `language.*` diagnostics only at the RPC object boundary:
envelope decoding, object kind, query or patch shape, result shape, and other
schema-level failures.

Bridge domain adapters should not hand-code language parsing diagnostics.
After bridge core validation, adapters should produce:

- `capability.*` for unsupported but language-valid query or patch features.
- `resolution.*` for missing or ambiguous UE targets.
- `validation.*` for target-state legality failures.

The UE Bridge uses `FSalDiagnostics` as its shared construction helper. Domain
interfaces add UE-specific context through that helper after the normalized
object boundary has been validated centrally.

JSON-RPC numeric error codes belong only to the private transport protocol;
they are not public Diagnostic codes. An RPC error exposed to an agent must
carry a registered string Diagnostic code in `error.data.code`. The Client
normalizes that string into the shared Diagnostic shape and uses
`runtime.rpc_error` only when an older or malformed peer omits it.

## Domain Rules

Each domain document defines its supported primary operations, `where` fields,
details, order keys, pagination support, patch operations, and common resolution
or validation failures.

Domain diagnostics should make the next query or patch obvious. For example,
missing assets should suggest an asset query, ambiguous nodes should return
candidate nodes, and patch legality failures should include the operation path
and expected target state.

Diagnostics also close the interface-discovery loop without adding another
reference field: unsupported Query or Patch syntax should name the relevant
`sal_schema({ module: "<module>" })` call in `suggestion`; unknown fields and Operations
should give the exact Query to retry `with schema`; and stale ids should point
to the relevant summary, collection, or tree operation.
