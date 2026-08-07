# Mutation Dry Run Contract

SAL mutation interfaces give agents one consistent meaning for `dryRun`.
This contract applies to every `sal.patch` provider that can change authored UE
object or Asset state. Idempotent Editor presentation controls such as opening,
focusing, or requesting the native close of an editor surface are outside this
contract: they expose a requested postcondition and verification result rather
than a speculative authored-state edit plan.

## Intent

`dryRun=true` means: build the same edit plan that a real edit would use, but
do not apply it to UE.

It is not just JSON validation. A dry run should resolve UE references, validate
UE semantics, and produce enough structured result data for an agent to decide
whether a real edit is worth applying.

## Execution Model

Mutation tools should follow this internal path:

1. Parse the request.
2. Resolve UE references.
3. Validate UE semantics.
4. Build a mutation plan.
5. Apply the plan only when `dryRun` is false.
6. Return a structured result.

Real edits and dry runs should share steps 1-4. The real edit path should not
re-interpret the original request differently from the dry-run path.

Native UE edit and validation calls are object-lifecycle boundaries. A call may
synchronously reconstruct or remove Nodes, Pins, or other provisional objects
before returning. Planning code may retain stable identity and an owner captured
before the call, but it must not dereference a pre-call raw object pointer after
the boundary. A later operation re-resolves the current object; failure to do so
returns a structured diagnostic rather than an assertion or process exit.

Code should route mutation responses through the shared `LoomleMutation` helper
in `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleMutationResult.h`
rather than rebuilding `isError`, `valid`, `applied`, `planned`,
`resolvedRefs`, and `diagnostics` by hand in each tool. Individual tool modules
remain responsible for UE-specific parse, resolve, validate, plan details, and
apply logic.

## Result Shape

The mutation result extends the ordinary contextual SAL result. Query and
mutation responses use the same Target context, optional related Targets and
handoffs, optional `object: ObjectText`, diagnostics, and formatter; mutation
adds execution fields around that envelope. The SAL executor must not return a
second mutation-only object or text model.

Mutation results should use the same core fields where applicable:

- `targetContext`
- `target` after an exact Target has been opened and verified
- `relatedTargets`
- `handoffs`
- `object` for SAL-backed mutation surfaces
- `isError`
- `dryRun`
- `applied`
- `valid`
- `assetPath`
- `operation`
- `previousRevision`
- `newRevision`
- `resolvedRefs`
- `planned`
- `diff`
- `diagnostics`

Rules:

- `targetContext` is one of `exact_target` or `unresolved_target` for
  mutations. `domain_root` is Query-only.
- Once a Domain opens and verifies an exact Target, both success and later
  validation/apply failures must retain its canonical `target`.
- An unresolved mutation must omit `target`, set `isError=true`,
  `valid=false`, and `applied=false`, and include at least one error
  diagnostic.
- Every handoff refers by alias to one canonical entry in `relatedTargets`;
  it never embeds another Target.
- `dryRun=true` must always return `applied=false`.
- A successful dry run should return `isError=false`, `valid=true`, and a
  planned edit summary when the tool can produce one.
- A failed dry run should return `isError=true`, `valid=false`,
  `applied=false`, and structured diagnostics.
- A dry run that supports revisions must keep `previousRevision == newRevision`.
- Diagnostics should be returned by default when they exist. Agents should not
  need a separate `returnDiagnostics` flag to get actionable errors.
- Diff should be returned by default when a tool can produce a reliable
  structured change set. Agents should not need a separate `returnDiff` flag for
  cheap and reliable diffs.

## Revision And Diff

`expectedRevision` is separate from `dryRun`. It is an optimistic concurrency
guard: apply only if the current asset revision still matches the caller's
expected revision.

When supported, revision checks should run before any mutation is applied and
revision conflicts must return `applied=false`.

Revision-aware mutation tools should return:

- `previousRevision`: the asset revision read before mutation.
- `newRevision`: the revision after mutation, or the same value as
  `previousRevision` when nothing was applied.

On `REVISION_CONFLICT`, the result should keep the current revision as both
`previousRevision` and `newRevision` so the agent can retry from the latest
state.

`diff` describes the planned or applied state change as a structured change
set:

- `scope`: the adapter or object family responsible for the change; it must not
  reintroduce a retired generic member abstraction
- `changes`: ordered change entries

Each change should include:

- `kind`: `create`, `update`, `rename`, `delete`, `reorder`, `set_default`, or
  another stable mutation verb
- `target`: structured target identity
- `before`: previous state when reliably known
- `after`: requested or resulting state when reliably known

`returnDiff` should only be exposed by a tool after that tool has a real
mutation plan model and diff computation is expensive enough to justify an
opt-in flag. Diffs should be derived from the plan or from actual before/after
state, not hand-written as a separate guess.

## Capability Rule

Do not advertise `expectedRevision`, `returnDiff`, or rich dry-run result fields
until the responsible SAL interface actually enforces or returns them. New
mutation work must use the shared result helper before exposing those fields.
