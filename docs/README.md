# Loomle Documentation

This directory is for current, trusted Loomle design documentation.

Loomle documents are written for both users and developers. They should be
concise and explicit about three things:

- Design intent: what agent workflow the interface supports and where its
  boundary is.
- Interface schema: tool inputs, outputs, edit operations, and error responses.
- UE implementation model: the Unreal Engine concepts and execution paths that
  Loomle maps to.

Only current contracts and source-grounded planned work belong here. Loomle 0.6
source and documents remain available from the `0.6` branch, its release tags,
and repository history instead of being duplicated on `main`.

## Current Documents

- `LOOMLE_070_REPOSITORY_AND_DISTRIBUTION.md`: canonical 0.7 component,
  artifact, Fab, and repository boundaries.
- `MUTATION_DRY_RUN_CONTRACT.md`: shared mutation validation, planning,
  revision, diff, and dry-run rules.
- `TESTING_AND_RELEASE_GATES.md`: native Bridge coverage, packaged end-to-end,
  lifecycle, fixture, runner, and release-gate contracts.

The current public language and UE-domain contracts live with their runtime
owners rather than under `docs/`:

- `../sal/`: SAL grammar, schemas, fixtures, and SDK implementation.
- `../interfaces/`: the six active Asset, Blueprint, Class, Graph, StateTree,
  and Widget interface cards, their shared reference-query contract, and the
  resident guide.
- `../client/README.md`: current five-tool TypeScript Client boundary.

## Planned Designs

Files under `planned/` are not current public interfaces. They preserve useful
UE research and explicitly identify the SAL/interface work still required:

- `planned/BLUEPRINT_USER_DEFINED_STRUCT_DESIGN.md`: UserDefinedStruct UE
  semantics and the open SAL design questions.
- `planned/blueprint/graph-layout.md`: automatic Blueprint graph formatting
  source facts and design questions. Current SAL supports stored layout reads
  and explicit movement, not automatic layout.

Future Material, PCG, runtime, and other domain documents should be added here
until their interfaces are designed, confirmed, and implemented.

Planned documents are not evidence for the current public tool surface.
