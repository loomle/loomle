# Loomle Documentation

This directory is for current, trusted Loomle design documentation.

Loomle documents are written for both users and developers. They should be
concise and explicit about three things:

- Design intent: what agent workflow the interface supports and where its
  boundary is.
- Interface schema: tool inputs, outputs, edit operations, and error responses.
- UE implementation model: the Unreal Engine concepts and execution paths that
  Loomle maps to.

Historical design notes, old specs, and exploratory material are archived under
`archive/legacy/`. The frozen 0.6 distribution and MCP documents are grouped in
`archive/legacy/0.6/`. They may be useful background, but they are not the
current interface contract.

## Current Documents

- `LOOMLE_070_REPOSITORY_AND_DISTRIBUTION.md`: canonical 0.7 component,
  artifact, Fab, and legacy-retirement boundaries.
- `MUTATION_DRY_RUN_CONTRACT.md`: shared mutation validation, planning,
  revision, diff, and dry-run rules.
- `runtime-execute.md`: current Bridge-internal `execute` safety and shutdown
  boundary. The four-tool TypeScript Client does not expose it yet.
- `observability-tail.md`: current Bridge-internal `diagnostic.tail` and
  `log.tail` persistence and cursor semantics. The four-tool TypeScript Client
  does not expose them yet.

The current public language and UE-domain contracts live with their runtime
owners rather than under `docs/`:

- `../sal/`: SAL grammar, schemas, fixtures, and SDK implementation.
- `../interfaces/`: the five active Asset, Blueprint, Class, Graph, and Widget
  interface cards, their shared reference-query contract, and the resident
  guide.
- `../client/README.md`: current four-tool TypeScript Client boundary.

## Planned Designs

Files under `planned/` are not current public interfaces. They preserve useful
UE research and explicitly identify the SAL/interface work still required:

- `planned/BLUEPRINT_USER_DEFINED_STRUCT_DESIGN.md`: UserDefinedStruct UE
  semantics and the retired 0.6 contract awaiting a SAL redesign.
- `planned/blueprint/graph-layout.md`: automatic Blueprint graph formatting
  awaiting a new SAL design. Current SAL supports stored layout reads and
  explicit movement, not automatic layout.

Future Material, PCG, runtime, and other domain documents should be added here
until their interfaces are designed, confirmed, and implemented.

## Research

- `research/UE_SCENE_EDITING_EXECUTE_AUDIT.md`: historical scene-editing
  investigation that must be revalidated before it informs a current design.

## Archive

- `archive/legacy/0.6/`: retired 0.6 distribution, Rust/Python MCP, standalone
  JSON direct-tool, `addFromPalette`, generic member-edit, Widget, and PCG
  contracts.
- `archive/legacy/`: other retired architecture and protocol documents.
- `archive/workspace/`: frozen legacy workspace guides and examples.

Archived and planned documents must not be cited as evidence for the current
public tool surface.
