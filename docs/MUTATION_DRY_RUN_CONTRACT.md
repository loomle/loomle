# Mutation Dry Run Contract

Loomle mutation tools should give agents one consistent meaning for
`dryRun`.

This contract applies to tools that can change UE state, such as graph edits,
member edits, widget tree edits, material edits, PCG edits, asset edits, and
class edits.

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

Code should route mutation responses through the shared `LoomleMutation` helper
in `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleMutationResult.h`
rather than rebuilding `isError`, `valid`, `applied`, `planned`,
`resolvedRefs`, and `diagnostics` by hand in each tool. Individual tool modules
remain responsible for UE-specific parse, resolve, validate, plan details, and
apply logic.

## Result Shape

For a SAL-backed mutation surface, its mutation result extends the ordinary
SAL result. Query and mutation responses use the same optional
`object: ObjectText`, the same diagnostics, and the same formatter; mutation
adds execution fields around that object. The SAL executor must not return a
second mutation-only object or text model. This requirement does not force
non-SAL tools to adopt SAL Object Text or its formatter.

Mutation results should use the same core fields where applicable:

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

- `scope`: the tool domain, such as `blueprint.class` or `blueprint.member`
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

## Temporary Implementations

If a tool currently implements only lightweight validation for `dryRun`, its
public schema and documentation must say so. Do not expose `expectedRevision`,
`returnDiff`, or rich dry-run result fields unless the bridge actually enforces
or returns them.

`blueprint_member_edit` and `blueprint_class_edit` are the first validation
surfaces for this contract. New mutation refactors should use the same shared
helper before migrating more complex graph, material, PCG, and widget edit
tools.
