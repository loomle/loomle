# Tool Surface Cleanup

## Intent

Loomle's public MCP tool surface now uses UE-domain names such as
`blueprint.graph.inspect`, `material.graph.edit`, `pcg.compile`, and
`widget.tree.inspect`. Internal bridge implementation, transform names, tests,
and diagnostics should use the same terms unless they are explicitly asserting
that a retired tool name is absent.

## Boundary

This cleanup is naming and diagnostic alignment only. It does not add legacy
aliases, does not change schemas, and does not reintroduce script execution
ops. Rejected `runScript` operations remain explicit unsupported-operation
tests for the current graph edit tools.

## Current Audit

- Bridge dispatch and helper names use current tool names rather than
  `query`, `mutate`, `describe`, or `verify`.
- Runtime error messages for Material and PCG graph operations report current
  tool names.
- Diagnostic `sourceKind` for graph edit failures is `graph.edit`.
- Python MCP result transform naming uses `blueprint.graph.edit.result.v1`
  instead of the retired Blueprint mutate name.
- Retired names may still appear in tests that assert those tools are not
  declared by the public manifest.

## Manifest Consistency

The Python manifest is allowed to expose local setup/project tools in addition
to the Rust runtime tools. Tests enforce that the local-only set is explicit,
that every manifest dispatch transform is implemented by the Python MCP
transform layer, and that `schema.inspect` tools match the Rust
second-layer schema inspector.

## Runtime Threading

Graph inspection reads UE `UObject`, graph, node, pin, and editor-facing data.
Even though inspection is read-only from the agent's perspective, it must run on
the Unreal game thread. The bridge dispatch layer only leaves `jobs` on the
pipe worker thread; graph inspect requests are marshaled to the game thread
like other UE object operations.

## Editor Lifecycle Guards

Graph and tree mutation tools reject work while the editor is shutting down,
while PIE is active, or while PIE is starting/stopping. This applies to
`blueprint.graph.edit`, `blueprint.node.edit`, `material.graph.edit`,
`pcg.graph.edit`, and `widget.tree.edit`.

Blocked mutations return a stable lifecycle code such as
`EDITOR_SHUTTING_DOWN`, `PIE_STARTING`, `PIE_ACTIVE`, or `PIE_STOPPING`.
The result also includes a `batchId`, `mutationContext`, skipped `opResults`,
and an `editor.lifecycle` diagnostic. The context records the tool, asset path,
graph name, command kinds, dry-run and revision flags, compile/save/layout
intent, current PIE flags, and an explicit note when asset editor open state is
not checked because the editor is already in an unsafe lifecycle state.
