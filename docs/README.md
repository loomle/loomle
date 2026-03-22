# LOOMLE Docs

Project-level technical documentation for `LOOMLE` and `LoomleBridge` (plugin + MCP server).

## Documents

1. `ARCHITECTURE.md`
- Full-system boundary, responsibilities, and call flow.

2. `REPO_STRUCTURE.md`
- Source repository, release bundle, and installed user-project structure.

3. `INSTALL_ENTRYPOINT_DESIGN.md`
- Prompt-first install entrypoint strategy for humans and agents.

4. `INSTALL_PAGE_CONTENT.md`
- Concrete homepage content and information architecture for `loomle.ai` / `loomle.ai/i`.

5. `RPC_INTERFACE.md`
- Unreal bridge RPC contract (`rpc.health`, `rpc.capabilities`, `rpc.invoke`), request/response schema, error shape, and current status notes for graph-semantic tools.

6. `MCP_PROTOCOL.md`
- MCP tool surface, tool schemas, routing rules, and behavior contract.

7. `PCG_LEARNING_RESOURCES_UE57.md`
- Curated UE 5.7 PCG learning resources, including official docs, videos, and transcript-first video reading guidance.

8. `PCG_NODE_CATALOG_VS_OPS.md`
- Clarifies the boundary between the `pcg-weaver` node catalog, LOOMLE semantic PCG ops, and generic graph construction.

9. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
- Aligns Epic's official PCG node categories with a behavior-first semantic taxonomy for future LOOMLE PCG ops.

10. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
- Final proposed LOOMLE PCG semantic-op family tree, canonical names, compatibility aliases, and first-wave op set.

11. `../workspace/Loomle/README.md`
- Agent-facing workspace entrypoint. Start here first for the current graph workflow, then follow each graph domain's `GUIDE.md`, `SEMANTICS.md`, catalogs, and examples.

12. `GRAPH_OPS_DESIGN.md`
- Historical `graph.ops` / `graph.ops.resolve` design plus the current direction toward workspace-local graph references and primitive mutate.

13. `GRAPH_OPS_PROTOCOL_DRAFT.md`
- MCP-style draft contract for `graph.ops` and `graph.ops.resolve`, annotated with current workflow positioning.

14. `UE_SCENE_EDITING_EXECUTE_AUDIT.md`
- Audit of what Unreal scene editing is already proven through `execute`, what is only partially proven, and what gaps remain before it becomes a comfortable agent workflow.

15. `issues/README.md`
- Local issue tracker index for detailed design issues that live in-repo.

16. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
- Local parent issue for making PCG `graph.query` the primary full-coverage node readback surface.

## Recommended Read Order

1. `../workspace/Loomle/README.md`
2. `ARCHITECTURE.md`
3. `REPO_STRUCTURE.md`
4. `INSTALL_ENTRYPOINT_DESIGN.md`
5. `INSTALL_PAGE_CONTENT.md`
6. `RPC_INTERFACE.md`
7. `MCP_PROTOCOL.md`
8. `PCG_LEARNING_RESOURCES_UE57.md`
9. `PCG_NODE_CATALOG_VS_OPS.md`
10. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
11. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
12. `GRAPH_OPS_DESIGN.md`
13. `GRAPH_OPS_PROTOCOL_DRAFT.md`
14. `UE_SCENE_EDITING_EXECUTE_AUDIT.md`
15. `issues/README.md`
16. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
