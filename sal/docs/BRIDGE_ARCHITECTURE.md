# SAL Bridge Architecture

## Role

Loomle Bridge maps normalized SAL requests to UE Editor behavior. It is thin in
public semantics and strict in native execution:

- SDK/Core owns language structure;
- `Target.domain` selects exactly one Domain adapter;
- the Domain adapter owns UE resolution, identity, Query, Patch, Palette,
  schema, planning, and handoffs;
- shared mutation utilities own transaction, dry-run, result, and diagnostic
  invariants.

The Bridge does not expose a public resolver or capability-composition layer.

## Request Path

```text
MCP Client
  → protocol/version check
  → project binding
  → JSON Schema and normalized request validation
  → read Target.domain
  → Domain Target validation and native open
  → Target canonicalization
  → Domain operation parsing
  → Target-relative identity resolution
  → Query or mutation planner
  → ordered result assembly
```

No stage after Domain selection may switch Domain because of native Class,
semantic tag, object `type`, Palette identity, or operation name.

## Project Binding

One MCP session is bound to one UE project. Binding is sticky across temporary
Editor disconnects and restarts. Every UE-backed call resolves through that
binding; it never falls through to another online project.

`sal_schema` is local and does not require an online project. Query, Patch,
Editor Context, and project-backed discovery do.

## Target Opening

The Bridge validates one flat Target union:

| Domain | Native open |
| --- | --- |
| Asset | Asset Registry Path and optional/required native Class assertion |
| Blueprint | load Asset Path, verify `BlueprintGuid` |
| Class | resolve exact native or generated Class Path |
| Graph | load Blueprint container, verify `BlueprintGuid`, resolve unique `GraphGuid` or discovery name |
| StateTree | load Asset Path, verify StateTree native Class |
| Widget | load WidgetBlueprint Asset Path, verify `BlueprintGuid` |

Targets contain no nested owner. Graph carries its durable Blueprint container
and verification fields directly.

After a successful open, the Bridge emits the canonical Target even when a
later operation fails. Failed open yields `unresolved_target`, no StableRef
scope, and at least one error diagnostic.

## Domain Adapters

Each adapter implements one closed surface:

```text
validate target fields
open and canonicalize target
build identity environment
resolve query operation and clauses
read ordered Object Text
discover exact schema
discover/revalidate Palette
parse/validate/plan Patch
apply or dry run
return related Targets and handoffs
```

The same UObject may be opened by different adapters through different
Targets. Blueprint and Widget adapters do not merge merely because both load a
`UWidgetBlueprint`. Asset and StateTree likewise remain separate.

## Identity

StableRefs arrive as native identity paths without a kind namespace:

```sal
@node-guid
@node-guid/pin-guid
```

The active Domain builds the exact lookup sets for each supported path shape.
All categories sharing one shape are searched as one injective environment.

Resolution succeeds only when exactly one native object matches. The Bridge
does not retry with:

- semantic tag;
- native Class;
- display name;
- collection name;
- current array index;
- a different Domain.

Owner-relative objects include their native owner component. Graph Pins use
NodeGuid plus PinId. StateTree Parameters use container Guid plus property
Guid. Function locals use their top-level Function Graph owner when the active
Target does not already supply it.

## Object Readback

Domain adapters emit ordinary ObjectExpr:

```json
{
  "kind": "object",
  "fields": {
    "id": "node-guid",
    "type": "/Script/BlueprintGraph.K2Node_Event"
  },
  "semanticTag": "node"
}
```

The optional tag is applied only by formatting policy. Adapters must produce
identical identity, schema, validation, and effects when it is removed.

Native field names and values remain exact. Unrepresentable field names are
preserved through quoted object keys where possible; unsupported member-path
mutation is reported rather than silently sanitizing a name.

## Query

Query execution:

1. opens and canonicalizes the Target;
2. validates the Domain operation and allowed clauses;
3. resolves StableRefs inside that Target;
4. performs the native read without mutation;
5. emits ordered Object Text, page state, diagnostics, related Targets, and
   handoffs.

Reads must not invoke native helpers that repair, compile, reconstruct, dirty,
or save state unless the operation explicitly requests that behavior.

Exact `with schema` and Patch share the same instance-level capability checks.
Schema must not advertise an operation that Patch would reject for the same
unchanged native state.

## Palette

Palette identities are Domain-owned opaque capability ids. They may encode
native semantic facts needed for exact re-resolution, but no public prefix
selects a Domain.

Creation values are ObjectExpr:

```sal
created = { palette: "P_ExactCapability" }
```

The Domain, Palette identity, materializing operation, and destination provide
creation meaning. The Bridge re-resolves the Palette identity against current
native state before planning and apply.

## Mutation Pipeline

Every mutating Domain follows the shared contract:

```text
parse
  → resolve exact Target and references
  → validate current capabilities
  → build ordered plan and native effect manifest
  → verify current state still matches the plan
  → apply in one top-level transaction
  → verify result and assemble Object Text
  → perform explicit external save, if requested
```

`dryRun=true` shares parse, resolve, validate, and plan. It stops before live
application. When exact native planning requires execution, the Domain uses an
isolated transient owner and the same native operation functions used by live
apply.

Transient ids never escape. Planned creations remain identified by request
alias until live apply returns final native identity.

On live apply failure, the Bridge closes the scoped transaction and explicitly
undoes it. Transaction cancellation is not rollback. Prior dirty state is
restored after successful undo. A rollback failure is reported separately.

An all-no-op Patch succeeds without dirtying state.

## Domain Mutation Ownership

One Patch belongs to one Domain planner. Stable identity resolution does not
grant cross-Domain writes.

Examples:

- Graph may read a referenced Blueprint Variable but cannot edit its
  declaration;
- Widget edits cannot include Blueprint compile;
- Graph edits cannot save the owning Blueprint;
- Class may save a generated-Class Default through its source Package but does
  not own Blueprint compile.

The result returns an independent canonical related Target and handoff for the
next request.

## Compile And Save

Compile belongs only to Domains with a native compilation unit:

- Blueprint compiles the whole `UBlueprint`;
- StateTree compiles the `UStateTree`;
- Graph and Widget return Blueprint handoffs;
- Class returns a Blueprint compile handoff;
- Asset has no compile.

Save is explicit external Package I/O after the in-memory transaction. It never
means compile. A save failure cannot roll back completed in-memory edits or
compiled state and therefore may return `applied: true` with an error.

## Result Assembly

The Bridge assembles one normalized result:

```text
result context
main canonical Target, when resolved
related canonical Targets
handoffs
optional ordered Object Text
page state
registered diagnostics
mutation state and plan, when applicable
```

Related Targets are structurally deduplicated and cannot repeat the main
Target. Every handoff points to a related alias. Object Text uses a
ScopedStableRef for foreign identity; normalized request payloads reject that
result-only shape. Request text may redundantly use its own active Target alias
as a qualifier, but the parser lowers it to an unscoped StableRef before the
Bridge receives it.

Zero-load evidence without all canonical Target fields remains ordinary object
data and cannot seed a Target table entry.

The SDK formatter derives canonical Result Text only from context, Target
table, handoffs, and optional Object Text. The Client then exposes ordered MCP
text content blocks:

1. the first block is that canonical, round-trippable Result Text;
2. an optional later SAL-comment block carries page or mutation metadata;
3. an optional later SAL-comment block carries diagnostics.

The later blocks are not appended to or parsed with the first. If normalized
Object Text is absent, the first block ends in `no_objects`. That marker is a
strict terminator; metadata, diagnostics, and zero-load evidence cannot
fabricate object presence.

## Threading And Editor State

UE object access and mutation run on the Game Thread. The Bridge captures
request data at the transport boundary, schedules native work, and returns only
after ordered result assembly is complete.

Adapters recheck volatile editor conditions during validation and immediately
before apply:

- PIE/simulation state;
- transaction availability;
- current object revision and identity;
- compiler availability;
- Package and Source Control state where relevant.

No adapter relies on last-focused editor tab or selection when an exact Target
or operation requires deterministic identity.

## Compatibility Boundary

Legacy syntax is lowered in a version-pinned protocol reader before Domain
entry. New adapters receive only:

- ObjectExpr values;
- explicit flat Targets;
- Target-relative StableRefs;
- new request/result unions.

Legacy kind prefixes, nested owner calls, and native-Class-based Domain
inference never reach identity hashing or mutation planning.
