# Cross-Blueprint Function Calls In Graph Patch

## Status and baseline

- Status: implemented on the fork branch and verified on UE 5.7.4 and UE 5.8.4
  (Windows), 2026-11-04: 214 of 214 `Loomle` Automation tests pass on each
  (207 before). Upstream submission is pending. Tests are in
  `SalGraphRobustnessTests.cpp` (`Loomle.Sal.Robustness.Graph.CrossBlueprint*`);
  coverage is recorded in `docs/UE_AUTOMATION_COVERAGE.md`.
- Baseline: Loomle 0.7.16 (`c1dae30`), Bridge protocol 6, UE 5.7.4 on Windows.
- Tracks upstream issue `loomle/loomle#198`: a Graph Palette action for a
  Blueprint-defined function of another Blueprint is discoverable but is
  rejected at spawn validation with `resolution.palette_not_spawnable`
  (`... belongs to a different Blueprint (SKEL_<Blueprint>_C)`).
- Scope: Blueprint-defined **function** (and Blueprint **interface** message)
  call Nodes whose target is another Blueprint's class.
- Out of scope, each needing its own design:
  - foreign Blueprint **variable** Nodes (getters and setters), inherited
    variable getters, and function-parameter getters. The existing test
    `Loomle.Sal.Robustness.Graph.ForeignVariablePreflightDiagnostic` asserts
    the current rejection and stays unchanged here;
  - adding pins to an existing Blueprint function;
  - a Kismet crash after a refused transient preflight followed by `compile`
    (not reproduced on 0.7.16, see "Open items").

## Intent and boundaries

An agent that sees a callable function of another Blueprint in a contextual
Palette query (for example from a typed `Create Widget` return Pin or a typed
object variable Pin) must be able to add that call Node through `sal_patch`,
in dry run and in a real edit, with the same result the Blueprint editor
produces.

Loomle must not widen what it accepts beyond what is proven safe. The existing
guard exists because a stale or foreign owner class crashed inside UE
(`#195`); that fail-closed property stays. The change is to **resolve** a
foreign action instead of rejecting every foreign Blueprint owner.

No public schema changes: no new operation, field, or error code.

## UE source basis

Verified in `UE_5.7/Engine/Source/Runtime/Engine/Private/MemberReference.cpp`:

- `FMemberReference::SetGivenSelfScope` (line 78) stores
  `MemberParent = InMemberParentClass->GetAuthoritativeClass()`. A Blueprint
  skeleton class is therefore normalized to the up-to-date class by UE itself.
- The same function sets `bSelfContext` when the scope is a child of the
  member parent or both share `ClassGeneratedBy`; otherwise the reference is
  an external member reference whose Target Pin is the object being called.
  It `ensure`s a valid `SelfScope`; a null or stale scope is the #195 failure
  shape.
- `FMemberReference::GetClassToUse` (line 271) resolves through
  `FBlueprintEditorUtils::GetMostUpToDateClass`.

## Current Loomle behavior and cause

`ValidatePaletteActionForTarget` (`SalGraphInterface.cpp`) rejects a function
action when its owner class is Blueprint-generated, is not owned by the target
Blueprint, and is not an ancestor of the target's class. Contextual Palette
actions for another Blueprint's function carry that Blueprint's **skeleton**
class as owner, so every such action is rejected, including valid ones.

Reproduced on 0.7.16 with an Automation test (two Actor Blueprints; target
holds a variable typed as the other Blueprint; Palette query from that
variable's value Pin): the dry run fails with exactly the message in `#198`.

## Design

Replace the unconditional rejection of foreign Blueprint function owners with a
resolution step in `ValidatePaletteActionForTarget`. A foreign function action
is admitted only when all of the following hold:

1. the owner class's `ClassGeneratedBy` is a different `UBlueprint`;
2. that Blueprint's `GeneratedClass` exists, is valid (`IsValid`), and is not
   superseded (`CLASS_NewerVersionExists` is not set);
3. that generated class defines a function with the same name as the action's
   function.

Otherwise the action is rejected as today. The message is changed to name the
cause and the next step, for example: the foreign Blueprint has no valid
compiled class or no longer defines the function; compile that Blueprint and
query the Palette again. The diagnostic code stays
`resolution.palette_not_spawnable`.

Node materialization is unchanged: UE's spawner and `SetGivenSelfScope`
normalize the owner. Loomle introduces no second source of truth for the
class.

Interface messages (`BPI_*`) use the same owner shape. Verified on UE 5.7.4: a
Blueprint implementing an interface exposes its message to a typed object Pin as
a `K2Node_Message` Palette action; it is admitted by the same resolution with no
extra handling, and dry run, apply, and compile are valid.

Removed functions and unloaded Blueprints never reach this guard: the Palette
entry is already gone, so the request is rejected earlier as
`resolution.palette_not_found`. The new message covers the remaining case of a
stale entry whose foreign Blueprint can no longer satisfy it. A function whose
signature changed after the query resolves to the new signature: the created
Node carries the new pins.

## Spike evidence (throwaway, not part of the repository)

With the resolution above applied to a copy of a real project (UE 5.7.4):

- the `#198`-shaped dry run is valid with no diagnostics;
- the real patch applies, adds exactly one Node, and `GetTargetFunction()`
  resolves; the Node is an external reference whose parent is the **generated**
  class (`BP_..._C`), not the skeleton;
- both Blueprints compile (`BS_UpToDate`);
- 17/17 tests in `Loomle.Repro` + `Loomle.Sal.Robustness.Graph` pass, including
  the `#195` function-call tests; the full baseline before the change was
  207/207 `Loomle` tests.

## Acceptance and audit

New UE Automation tests (independent development module), each asserting
dry run and real apply, one Undo step, compile status, and unchanged source
state on rejection:

1. Actor Blueprint calls a function of another Actor Blueprint through a typed
   variable Pin.
2. Widget Blueprint function called from a typed `Create Widget` return Pin,
   for an existing function and for one added after the last compile (`#198`).
3. Foreign Blueprint recompiled between the Palette query and the patch.
4. Foreign Blueprint with no valid generated class, or whose function was
   removed: rejected with the new message, no crash, source state unchanged.
5. Blueprint interface message to a foreign Blueprint.
6. `#195` regressions (existing tests) remain green; `ForeignVariablePreflightDiagnostic`
   remains green and unchanged.

Gate: the full `Loomle` Automation suite passes on UE 5.7.4; UE 5.8 is run
before upstream submission.

Audit questions: can an agent act on the rejection message without guessing;
does the Palette entry shown to the agent now always match what spawn
accepts; is the dry run still presentation-neutral
(`MUTATION_DRY_RUN_CONTRACT.md`).

## Open items

- Kismet crash after a refused transient preflight followed by `compile` was
  not reproduced on 0.7.16 (variable added without compile, graph patch, then
  compile-only Blueprint patch: all succeeded). Needs the original crash
  report or a reproduction on the original asset before any design.
- Variable and getter cases above need their own design, including a decision
  to reverse the current test expectation.
