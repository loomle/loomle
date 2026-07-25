# SAL Diagnostics

## Contract

Diagnostics are structured, registered, ordered, and actionable. They explain
what failed, where it failed, and what exact discovery or handoff can resolve
it.

```ts
interface Diagnostic {
  severity: "error" | "warning" | "info";
  code: string;
  message: string;
  path?: (string | number)[];
  span?: {
    line: number;
    column: number;
    length?: number;
  };
  domain?: string;
  operation?: string;
  ref?: string;
  expected?: unknown;
  actual?: unknown;
  supported?: unknown;
  matches?: unknown[];
  suggestion?: string;
}
```

Human-readable SAL comments may mirror diagnostics in a later, independent MCP
text content block, but comments do not replace structured diagnostics. The
Client never appends them to the first canonical Result Text block.

## Result Context

Diagnostics obey Target resolution state:

- `exact_target`: opening succeeded, so the canonical Target remains present
  even when a later Query, Patch, compile, or save step fails;
- `domain_root`: only the Query-only Asset root is present;
- `unresolved_target`: no Target or StableRef scope exists, and at least one
  `severity: error` diagnostic is required.

An unresolved mutation also has `isError: true`, `valid: false`, and
`applied: false`.

Related Targets referenced by a diagnostic must already be retained by Object
Text or an explicit handoff. A diagnostic alone cannot smuggle a new Target
into the result.

The first MCP text block encodes only the canonical Result Text. If it ends in
`no_objects`, that marker is a strict terminator. Diagnostics follow, when
present, as an independent comment-only text block; they do not create an
`objects` section or synthetic object presence.

## Code Shape

Codes use readable snake_case fragments:

```text
<category>.<specific_condition>
```

The registry's closed layer set is:

- `language`
- `resolution`
- `validation`
- `capability`
- `project`
- `runtime`
- `tool`

Examples:

```text
language.invalid_object_shape
language.invalid_target
resolution.target_not_found
resolution.identity_conflict
resolution.pin_ambiguous
validation.invalid_cursor
validation.result_too_large
capability.reference_unavailable
capability.transaction_unavailable
validation.save_failed
project.offline
runtime.incompatible
tool.invalid_arguments
```

Codes are protocol values, not localized display strings.

## Diagnostic Layers

### Language

- malformed brace ObjectExpr;
- duplicate decoded object key;
- non-finite numeric literal;
- reserved keyword used as a semantic tag;
- parentheses used as an object representation;
- Target syntax inside an ordinary object field;
- unknown Domain or Target field;
- non-string or empty Target field;
- nested Target;
- malformed StableRef or member path;
- foreign or unknown scoped qualifier in request text, or any
  `ScopedStableRef` in a normalized request (a redundant active qualifier is
  lowered before validation).

### Resolution

- no native object at the supplied address;
- native type assertion mismatch;
- BlueprintGuid or GraphGuid verification failure;
- discovery Target resolves ambiguously;
- incomplete Target cannot be canonicalized;
- unsupported legacy request cannot choose exactly one Domain;
- unused or mixed explicit/legacy Target declaration in compatibility input.
- unknown Target-relative identity;
- collision across categories sharing one identity path shape;
- missing native owner component;
- malformed or duplicate PinId within its owning Node;
- invalid StateTree Context descriptor identity;
- stale member path after schema change.

Identity failures never retry by semantic tag, display name, native Class, or
array position.

### Capability

- operation absent from the selected Domain;
- identity is readable but outside that Domain's mutation scope;
- current native object does not support an operation;
- exact schema or Palette prerequisite is missing;
- transaction, PIE, compiler, or save capability is unavailable;
- project reference scope has no complete bounded provider.

### Validation

- stale Palette identity;
- invalid destination or relationship direction;
- incompatible native types;
- unplanned native cascade;
- alias used before materialization;
- binding consumed more than once or never consumed;
- finalization mixed with authored edits;
- result or schema exceeds a complete-response budget.

Native apply, rollback, compile invocation, and save failures that occur after
request resolution also use registered `validation.*` codes, such as
`validation.atomic_apply_failed`, `validation.atomic_rollback_failed`, and
`validation.save_failed`.

### Project

- no matching project;
- a bound project is offline;
- selection or project disambiguation is required;
- more than one Editor claims the same project.

### Runtime

- connection, health, protocol, or identity failure;
- request timeout or cancellation;
- malformed RPC or Bridge response;
- Editor shutdown or unresponsiveness;
- internal Client or Bridge failure.

### Tool

- unknown public tool;
- arguments do not match that tool's closed input schema.

Blueprint compiler errors and warnings are resulting native state and remain
ordered comments in the first Result Text block. They are not structured
execution diagnostics merely because their native severity is Error or
Warning.

## Suggestions

Suggestions must be copyable and stay within known facts. Preferred forms are:

```sal
query g
target
with schema
```

```sal
query g
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
with schema
```

```sal
query g
nodes "search text"
```

or an explicit related Target and handoff:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

A suggestion must not:

- invent a Guid, path, native type, Palette id, or operation argument;
- infer Domain from native Class or semantic tag;
- omit a required native owner identity component;
- advise name search as if it were stable identity;
- serialize a Target inside an object.

## Identity Conflict Example

If Graph Domain finds the same one-segment identity in two categories:

```sal
query g
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa
```

it returns `resolution.identity_conflict` with canonical candidate descriptions.
Adding `node` or another tag does not make the reference exact.

For a Pin, the required form includes the native owner:

```sal
@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa/bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb
```

If that path still finds multiple Pins inside the same owning Node, the result
is `resolution.pin_ambiguous`.

## Cross-Domain Example

If Graph Domain resolves an owning Blueprint Variable but a Patch tries to
edit it:

```sal
set @variable-guid.Category = Stats
```

the Bridge returns a registered capability diagnostic such as
`capability.operation_unavailable`, retains the exact Graph Target, and may
return a related Blueprint Target with a handoff when that handoff is supported
by the operation. It does not switch Domain or reinterpret the reference.

## Dry Run And Mutation

Dry run and live apply use the same registered diagnostics through parse,
resolve, validate, and plan. Dry run stops before live application.

Mutation results clearly separate:

- `valid`: request and plan are valid;
- `applied`: live authored state changed;
- ordered `planned.operations`;
- native `planned.effects`;
- diagnostics;
- current Object Text.

At the MCP boundary, current Object Text stays in the first canonical Result
Text block. Mutation state and plan are formatted as a later SAL-comment
metadata block, and diagnostics as another later SAL-comment block. Neither is
concatenated to Result Text or parsed as part of it.

External save occurs after the in-memory transaction. A save failure therefore
may return `applied: true` with an error and dirty unsaved state.

## Registration

Every emitted code must be declared in the shared registry with:

- its full code;
- one of the registered layers;
- its default severity;
- its stable title.

The JSON Schema closes the optional diagnostic fields to `path`, `span`,
`domain`, `operation`, `ref`, `expected`, `actual`, `supported`, `matches`,
and singular `suggestion`. Unknown ad hoc codes fail registry tests. Domain
adapters may add registered codes but cannot redefine Core meanings.
