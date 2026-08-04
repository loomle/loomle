# SAL Object And Target Migration Report

## Status

This report tracks the protocol migration from constructor-shaped objects,
kind-prefixed references, and native-Class capability composition to:

- brace `ObjectExpr`;
- erasable semantic tags;
- explicit flat Domain Targets;
- Target-relative native identity paths;
- independent cross-Domain Target handoffs.

Protocol v3, the SAL Core, Client, Bridge source, all six Domain cards,
generated artifacts, examples, and site implement this destination contract.
The compatibility reader is confined to explicitly opted-in direct TypeScript
parser calls; MCP tools and the default SDK facade are strict. The v3 Bridge
accepts only the normalized model described below.

## Required Runtime Shape

The Bridge receives one normalized request containing:

```text
one TargetBinding
one Target.domain
one Domain operation
ObjectExpr values
Target-relative StableRefs
```

The Bridge returns one of five closed result leaves:

- exact Query;
- exact Mutation;
- Asset Domain-root Query;
- unresolved Query;
- unresolved Mutation.

An opened Target is always returned canonically. An unresolved result contains
no Target and at least one error diagnostic.

The first MCP content block is canonical Result Text and must round-trip
through the result parser. A missing normalized object formats as terminal
`no_objects`. Client mutation metadata and diagnostics are later independent
SAL-comment text blocks; they never append to the first block or fabricate
object presence.

## Domain Entry

All six adapters enter through `Target.domain`:

| Domain | Canonical Patch Target |
| --- | --- |
| Asset | `path + type` |
| Blueprint | `asset + id` |
| Class | `path` |
| Graph | `asset + blueprintId + id` |
| StateTree | `asset + type` |
| Widget | `asset + id` |

Native Class checks remain inside the selected Domain. Blueprint/Widget and
Asset/StateTree no longer compose capability surfaces.

## Object Migration

All request, result, Palette, schema, fixture, and mutation object values use:

```sal
{ field: value }
```

Formatters may emit a Domain-recommended tag:

```sal
node { id: "N", type: "/Script/..." }
```

Tests must prove that removing the tag leaves:

- normalized fields;
- identity and lookup;
- schema;
- operation selection;
- validation and plan;
- mutation effects

unchanged.

Bridge producers distinguish normalized Name, reference, and ObjectExpr values
from ordinary UE object data through an out-of-band C++ expression wrapper.
That marker is process-local and never enters JSON. An unmarked JSON object is
always wrapped as ObjectExpr, even when its fields exactly equal
`{ kind: "name", ... }`, `{ kind: "local", ... }`, or another normalized SAL
shape. Output conversion therefore never infers semantics from an ordinary
object's field names.

## Reference Migration

The Bridge resolves:

```sal
@native-id
@native-owner-id/native-local-id
```

Confirmed owner-relative paths are:

- Graph Pin: `@NodeGuid/PinId`;
- StateTree Parameter: `@ContainerGuid/PropertyGuid`;
- Blueprint function local: `@TopLevelFunctionGraphGuid/VarGuid` when the
  Target does not already supply that owner.

Every Domain audits all categories that share a path shape as one lookup set.
A semantic tag cannot disambiguate a collision.

The exact Target itself has no StableRef. `references to target` uses the
structural `target_self` subject only inside the References operation. The
current native provider resolves bare callable Function/Macro Graph Targets
and the direct Graph `target.InterfaceGuid` member when its Interface
declaration exists. Unsupported Domain or Graph roles return
`capability.reference_unavailable` with the already opened exact Target
context.

## Cross-Domain Migration

Graph and Widget finalization return a canonical Blueprint Target. Widget event
guidance returns a canonical Graph Target. Other cross-Domain navigation uses
the same result structure:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

No nested Target or hidden adapter composition remains.

## Compatibility Boundary

Only callers that explicitly enable compatibility on the direct TypeScript
parser may submit the previous text syntax during protocol v3. MCP tools and
the default SDK facade are strict. Lowering occurs before schema validation and
Domain planning and accepts fused references only when their complete native
identity shape is unambiguous in the already selected active Domain.
Under-scoped owner identities, target-self fused references, and forms
requiring UE-assisted recovery fail. The protocol v4 Bridge rejects legacy
Call objects, fused kind references, and implicit Domain selection.

Ambiguous legacy StateTree/Asset or Widget/Blueprint requests fail. Only
`object(...)` can lower to an untagged ObjectExpr; a reserved constructor
callee is never discarded. A mixed legacy Patch is never split into several
new requests, and explicit-v3/legacy Target mixtures or declarations outside
the selected Target's alias-dependency closure are rejected.

The new formatter emits only the destination model. Protocol v4 explicitly
extends the reader-only migration window because its Editor transport addition
does not change SAL syntax; removal requires a later incompatible protocol and
release note.

## Verification Matrix

Required automated coverage:

- every JSON-compatible object key/value round-trips through ObjectExpr,
  including `-0`; non-finite runtime numbers are rejected;
- ordinary objects that exactly resemble normalized Name, LocalRef,
  StableRef, or ObjectExpr shapes remain ordinary data;
- semantic-tag erasure is behaviorally identical;
- reserved keywords cannot be tags;
- every Target branch rejects unknown, nested, non-string, and empty fields;
- Query discovery forms canonicalize to exact Targets;
- Patch rejects incomplete Targets;
- Graph child/collapsed Targets query, dry run, patch, save, and reload;
- copied Graph Nodes regenerate NodeGuid and Pin refs retain owner scope;
- Pin identity conflict is local to its owning Node;
- Blueprint and StateTree one-segment cross-category collisions fail closed;
- related Target aliases, ScopedStableRefs, and handoffs round-trip;
- unresolved Query and Mutation require error diagnostics;
- the first MCP block round-trips as Result Text and terminal `no_objects`
  rejects any appended line;
- mutation metadata and diagnostics occupy later independent comment blocks
  without changing object presence;
- old protocol lowers or fails with an explicit migration diagnostic;
- schema, Client, Bridge, formatter, fixtures, and protocol version agree.

## Acceptance

Migration is complete only when:

1. generated schemas and TypeScript types contain no object-kind Call union;
2. Bridge Domain entry reads only `Target.domain`;
3. every active result includes correct Target context;
4. all current formatters emit braces, flat Targets, and Target-relative refs;
5. old forms appear only inside compatibility tests and clearly marked legacy
   documentation;
6. all six interface cards and dynamic schema examples use the new model;
7. repository-wide audits find no unmarked legacy syntax.

## Verification Status

Current source has passed:

- generated SAL schema and interface-catalog consistency checks;
- SAL parser, formatter, compatibility, fixture, SDK, and memory-executor
  suites;
- the complete unrestricted repository `npm test` gate, including the real
  Client Unix-socket restart, stale-record, and in-flight disconnect lifecycle;
- the pinned macOS arm64 Client executable build and isolated executable smoke
  test;
- source Fab assembly for `darwin-arm64`;
- a same-source UE 5.7 macOS arm64 Development BuildPlugin compile and all 135
  discovered UE Automation tests, with zero failure, timeout, missing test,
  crash report, or runner-classified log hazard;
- the stripped release BuildPlugin compile plus directory and ZIP boundary
  audits, including native arm64 checks, exact Client/license/notice bytes,
  executable permissions, descriptor shape, and exclusion of tests and build
  state;
- the nine-step packaged end-to-end workflow against that exact ZIP, including
  canonical Blueprint Target discovery, dry run, live mutation, readback,
  fixture restoration, Editor Context, Client reconnect, runtime lifecycle,
  and final offline state; and
- a production Jekyll build of the migrated site.

The final packaged audit also locks two Result Text consequences into the test
harness: canonical owner identity is read from the Result Target table rather
than reconstructed from redundant ObjectExpr fields, and a recognized Editor
surface without an exact supported Domain Target is accepted only as the
documented `unresolved_target` plus `resolution.unresolved_target` branch.

The audited pre-version-bump local QA ZIP has SHA-256
`744fb3b910dac25b285cc89efb427489432c13c89e947a7c739f182d0b801d8e`.
This is migration verification evidence, not a future release-asset identity.
No artifact has been published, staged, or promoted.
