# Tool Surface Cleanup

## Intent

Loomle's public MCP tool surface now uses UE-domain names such as
`blueprint_graph_inspect`, `material_graph_edit`, `pcg_compile`, and
`widget_tree_inspect`. Internal bridge implementation, transform names, tests,
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
- Python MCP result transform naming uses `blueprint_graph_edit.result.v1`
  instead of the retired Blueprint mutate name.
- Retired names may still appear in tests that assert those tools are not
  declared by the public manifest.

## Manifest Consistency

The Python manifest is allowed to expose local setup/project tools in addition
to the Rust runtime tools. Tests enforce that the local-only set is explicit,
that every manifest dispatch transform is implemented by the Python MCP
transform layer, and that `schema_inspect` tools match the Rust
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
`blueprint_graph_edit`, `blueprint_node_edit`, `material_graph_edit`,
`pcg_graph_edit`, and `widget_tree_edit`.

Blocked mutations return a stable lifecycle code such as
`EDITOR_SHUTTING_DOWN`, `PIE_STARTING`, `PIE_ACTIVE`, or `PIE_STOPPING`.
The result also includes a `batchId`, `mutationContext`, skipped `opResults`,
and an `editor.lifecycle` diagnostic. The context records the tool, asset path,
graph name, command kinds, dry-run and revision flags, compile/save/layout
intent, current PIE flags, and an explicit note when asset editor open state is
not checked because the editor is already in an unsafe lifecycle state.
