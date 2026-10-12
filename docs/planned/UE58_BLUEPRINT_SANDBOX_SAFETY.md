# UE 5.8 Blueprint Sandbox Safety

## Status and baseline

The investigation and repair scope was confirmed on 2026-10-12. The local repair
and native regression audit are complete. Original-asset UE 5.8.3 confirmation
remains pending: issue assets belong to a colleague's machine and are
unavailable locally.

The baseline is GitHub `main` at
`e4b7c56639ae480f62071acb49cc88fa354d4372` (Loomle 0.7.15, protocol 6).
The local checkout was 0.7.8 when the investigation started. Its older
implementation and test-module layout must not be used as the repair baseline.

Reports:

- [Issue 201](https://github.com/loomle/loomle/issues/201): Blueprint-domain
  dry-run duplication asserts while accessing `Shooter_Hud_Reference` through
  an invalid UObject property container. The property is an object reference
  to a Widget Blueprint generated class; its effective default is `None`.
- [Issue 202](https://github.com/loomle/loomle/issues/202): Graph patches assert
  on a nonzero UObject refcount during sandbox duplication. Reported triggers
  include function parameters, global/contextual byte-to-int conversion nodes,
  and an unsupported cross-Blueprint variable action in a dry run.

Both reports use UE 5.8.3 / CL 58210709 on Win64. Available local Launcher
installations are UE 5.8.1 / CL 56057345 and UE 5.7.4 / CL 51494982.
Passing 5.8.1 tests will not by itself establish that the reported 5.8.3 asset
state is repaired.

## Intent and boundaries

The gap is native UObject lifetime and Blueprint compilation semantics inside
the shared transient preflight sandbox. SAL parsing and new tool capabilities
are outside this repair.

Blueprint, Graph, and Widget planners must keep using UE-authored declarations,
native schema actions, and native compilation paths. Preflight must produce an
isolated class/default-object state while preserving the source asset, stable
identities, package dirty state, Undo history, debugger settings, and Editor
presentation. Compiler-error status alone must remain editable.

Expected implementation scope is
`Private/Sal/Blueprint/SalBlueprintSandbox.cpp`, narrowly necessary caller
adjustments, regression fixtures in the independent `LoomleBridgeTests`
module, and the existing dry-run/testing documentation. No new public MCP
tool, Domain, release channel, or protocol version is proposed.

## UE source basis

Paths below are relative to the available UE 5.8.1 `Engine` directory.

- `Source/Runtime/Engine/Private/Blueprint.cpp:630-640`:
  `UBlueprint::PostDuplicate` calls native `PostDuplicateBlueprint` unless
  `bDuplicatingReadOnly` is set, then invokes the generated CDO's
  `PostDuplicate`. Read-only duplication alone therefore does not suppress
  every callback or establish CDO isolation.
- `Source/Editor/UnrealEd/Private/Kismet2/BlueprintEditorUtils.cpp:1547-1845`:
  native duplication creates new generated/skeleton classes, compiles, copies
  old CDO/subobject values, and reconstructs nodes. Its logic matches the
  available UE 5.7.4 implementation. `NoExtraCompilation` only suppresses the
  final extra compile; it does not suppress the earlier native compiles.
- `Source/Editor/Kismet/Private/BlueprintCompilationManager.cpp:351-354,423-427`:
  normal synchronous compilation collects garbage unless explicitly requested
  to skip it. The ordinary compile invoked by `PostDuplicateBlueprint` does
  not request `SkipGarbageCollection`.
- `Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp:3009-3184`
  and `Source/Runtime/CoreUObject/Public/Serialization/DuplicatedObject.h:44`:
  duplication retains source/copy annotations through duplicate callbacks;
  UE 5.8 replaces the previous sparse annotation with a temporary annotation
  type. `Source/Runtime/CoreUObject/Public/UObject/UObjectAnnotation.h:1469-1470`
  documents that temporary type as requiring destruction before GC. Nested
  Blueprint compilation can cross that lifetime boundary; its role in the
  reported crashes still requires reproduction evidence.
- `Source/Runtime/CoreUObject/Private/UObject/UObjectArray.cpp:382-405`:
  destroying a UObject with a nonzero refcount asserts. This check also exists
  in UE 5.7.4. Additional strong references cannot safely be treated as a
  universal fix for objects that native compilation replaces or destroys.
- `Source/Runtime/CoreUObject/Private/Serialization/DuplicateDataWriter.cpp:171-184`:
  duplication follows objects whose Outer is duplicated. Generated classes
  commonly share the Blueprint's package rather than being Blueprint children;
  their absence from a source-Outer retention list does not prove they are
  missing annotation keys.
- `Source/Runtime/Engine/Classes/Engine/Blueprint.h:817-825` and
  `Source/Editor/UnrealEd/Private/Kismet2/BlueprintEditorUtils.cpp:1610-1620`:
  native duplication derives generated/skeleton class names from the Blueprint
  name and creates them in the Blueprint package. `MakeUniqueObjectName` only
  checks its candidate object name, not these derived sibling names. An occupied
  class name can therefore enter native UObject overwrite/destruction.

Native Merge uses `bDuplicatingReadOnly` around `StaticDuplicateObject` in
`Source/Developer/Merge/Private/SBlueprintMerge.cpp:334-344`. Separating
structural duplication from subsequent native class initialization is a
research option, not an approved replacement algorithm. Widget subclass
callbacks, CDO callbacks, bindings, SCS/templates, and class isolation must be
accounted for before such a change can satisfy this contract.

## Current Loomle behavior and unresolved cause

0.7.15 calls `StaticDuplicateObject` inside the native duplication-flags scope.
`FScopedBlueprintDuplicationSourceRetention` retains the source Blueprint and
its currently nested UObjects for that call. It then audits copied identities,
GeneratedClass/CDO isolation, and source class/CDO identity.

The existing nested-GC fixture proves retention of one Blueprint-owned probe.
It does not identify the property container in issue 201 or the refcounted
object destroyed in issue 202. Both reports locate failure inside duplication;
they do not establish that sandbox teardown is the cause, or that both crashes
have the same immediate cause.

An isolated native fixture now proves one naming defect in 0.7.15: an existing
strongly referenced generated class occupying the first sandbox's derived class
name makes Graph preflight crash in `MakeBlueprintSandbox`, with the same
`ObjectItem->GetRefCount() == 0 || GExitPurge` assertion at `UObjectArray.cpp:405`
reported in issue 202. This identifies the destroyed object in the fixture,
not the destroyed object in the colleague's crash. The original 201-test native
suite passes on the unmodified 5.8.1 baseline.

## Implemented repair design

Before duplication, validate existing generated and skeleton classes and their
CDOs without creating missing defaults. Require valid registered objects, the
native Blueprint class type, a CDO belonging to that class, and source classes
belonging to that Blueprint. Check loaded derived class defaults as before.
Return the existing preflight diagnostic with compile/re-query guidance when
this source state is unsafe. This guards detectable invalid containers; it does
not prove issue 201's unavailable asset state has the same cause.

Choose a sandbox name using native `MakeUniqueObjectName`, derive both sibling
class names with `UBlueprint::GetBlueprintClassNames`, and retry while either
name is occupied by any UObject in the transient package. Only then call the
existing `StaticDuplicateObject` path. This prevents overwriting unrelated
objects without changing native compilation, retention, or class initialization.
No staged duplication algorithm is introduced.

## Confirmed repair sequence

1. Work from the latest 0.7.15 baseline on an isolated `codex/` branch, retaining
   the existing untracked `tools/` directory and unrelated local work.
2. Add minimal native regressions reaching normalized public SAL Patch, using
   healthy compiled assets and the operation/property shapes in both reports.
   Reproduce on 5.8.1; use the exact 5.8.3 asset/version if local fixtures do not
   reproduce the reported failure.
3. Locate the object and invalidation boundary before changing lifetime policy.
   Use test-scoped object identity, weak-reference/deletion observations, and
   duplication/compile/reinstance traces. Distinguish source objects,
   annotation keys, compiler replacements, and sandbox objects. Avoid
   dereferencing a pre-compile raw pointer after a native invalidation boundary.
4. Repair the shared helper using the proven native lifecycle. Retain objects
   only where their role permits it; re-resolve compiler-replaced objects.
   Detect unsafe source class/CDO state before native duplication and return
   an existing structured preflight diagnostic where detection is reliable.
   A pre-call validity check cannot by itself guarantee callback safety.
5. If evidence requires staged duplication or a change to class initialization,
   document and confirm that concrete algorithm before implementing it. Do not
   suppress assertions, wrap native checks in C++ exception handlers, hand-build
   generated classes, or remove preflight to avoid the crash.
6. Audit the regressions and full native suite on 5.8.1 and 5.7.4, then record
   actual outcomes and remaining 5.8.3 coverage limitations in the formal docs.

## Input, edit, output, and error contracts

Public `sal_patch` input and the existing Blueprint/Graph edit schemas remain
unchanged. The regressions use Blueprint graph removal, Graph
`AddInputParameter` / `AddOutputParameter`, Palette-backed Node creation, and
contextual `pin.from` actions through their existing schemas.

A successful dry run returns the current shared mutation envelope with
`dryRun=true`, `valid=true`, `applied=false`, and a plan. A live patch validates
on the same preflight path before applying through the existing transaction.

Detected sandbox failures keep the existing domain diagnostics:
`capability.preflight_unavailable` for Blueprint and
`capability.operation_unavailable` for Graph. The response must identify the
unsafe state and next action, retain a resolved canonical Target, and return
`isError=true`, `valid=false`, `applied=false` through `LoomleMutation`.
Unsupported cross-Blueprint Palette actions retain their normal resolution
diagnostic. No `stateMayHaveChanged` field or other unsupported result guarantee
is introduced by this repair.

## Acceptance and audit

- Blueprint dry-run graph removal on an asset with a cross-Blueprint Widget
  object property/default `None` completes or returns a structured diagnostic.
- Function input/output parameters and global/contextual byte-to-int Node
  creation work in dry-run and live apply on valid fixtures.
- Unsupported contextual cross-Blueprint variable creation returns its normal
  diagnostic without terminating the Editor or modifying the source.
- Repeat the cases with intervening native compile, save/reload, and GC. Check
  source class/CDO validity and identity, source properties and package bytes,
  dirty state, stable GUIDs, Undo history, and Editor presentation.
  Dry-run and failed preflight preserve source state; live apply changes only
  the intended declaration and Undo restores its authored state.
- Preserve the existing Widget sandbox/nested-GC tests. A retained sandbox must
  survive required GC; releasing its owner must allow cleanup without leaked
  standalone flags or stale annotations.
- Build the production and independent test modules using the current single
  BuildPlugin workflow; run the complete native Automation suite separately on
  UE 5.8.1 and UE 5.7.4. Use fresh isolated runner output directories.
- Record exact source revision, engine CL, test outcomes, and whether the issue
  assets/5.8.3 were exercised. Full release promotion still requires its normal
  same-source packaged gates.

## Native audit: 2026-10-12

The working branch is `codex/ue58-blueprint-sandbox-safety`, based on the 0.7.15
revision above plus the local repair. Production changes affect only the shared
sandbox implementation; public schemas and native duplication/compilation paths
are unchanged. Both production and independent test modules compile on the
local 5.8.1 and 5.7.4 installations.

The six new regressions are:

| Native test | Verified boundary |
|---|---|
| `Loomle.Sal.Graph.Mutation.SandboxClassNameCollision` | Both generated and skeleton name occupants retain identity, flags, parent, and CDO state; source class/CDO and graph identity remain isolated. |
| `Loomle.Sal.Blueprint.Mutation.WidgetObjectDefaultDryRun` | Actor and Widget Blueprint object property/default None, repeated public dry-run graph removal with intervening compile/GC, source/reference identity, dirty state, and GUID preservation. |
| `Loomle.Sal.Blueprint.Mutation.InvalidDefaultObjectPreflight` | Existing generated/skeleton CDO mismatch or missing skeleton CDO returns the registered preflight diagnostic without duplicating or changing source/foreign state. |
| `Loomle.Sal.Blueprint.Mutation.CompileErrorGraphRepair` | Genuine native compile error remains editable: public dry run preserves error state, live removal repairs it, and one Undo restores the erroneous graph and identities. |
| `Loomle.Sal.Robustness.Graph.PreflightSignatureConversionLifecycle` | Public parameter dry/live edits, compiled signature readback, populated native local cache, global/contextual Conv_ByteToInt actions, one-step Undo, repeated compile/GC. |
| `Loomle.Sal.Robustness.Graph.ForeignVariablePreflightDiagnostic` | Unsupported unrelated Blueprint variable action returns resolution.palette_not_spawnable and preserves both source assets across compile/GC. |

The collision test deterministically crashes against unchanged 0.7.15 production
and passes with the repair. Full native runs pass 207/207 on both UE 5.8.1 /
CL 56057345 and UE 5.7.4 / CL 51494982, with no runner-classified log hazards, new
crash reports, or retained runner workspace. SHA-256 checks confirm the production
and regression sources match both isolated build hosts. Fast `npm test` passes;
client and public schema code have no changes.

Local runner evidence is `.tmp/r58base/result.json` (201/201 baseline),
`.tmp/r58collisionbase/editor.log` (baseline assertion),
`.tmp/r58collisionfixed/result.json` (1/1 repaired collision),
`.tmp/r58final/result.json`, and `.tmp/r57final/result.json` (207/207 each).
These paths are local ignored artifacts, not committed release evidence.

These new cases cover compile/GC, dry-run isolation, native readback, and Undo;
the full suite retains existing save/unload/reload, nested-GC, identity, and
Editor presentation coverage. The new issue-shaped fixtures themselves do not
add a persistent save/reload round trip or a rendered 5.8.3 asset run.

The issue 201 property container and the colleague's issue 202 destroyed object
remain unidentified. Do not close either issue solely from this audit: the
colleague must rebuild this source for UE 5.8.3 and retry the original operations
and assets. A 5.8.1 test DLL is not a 5.8.3 validation artifact. Release promotion
and distribution still require the normal same-source packaged gates.
