# LOOMLE Docs

Project-level technical documentation for `LOOMLE` and `LoomleBridge`.

## Current Core Docs

1. `ARCHITECTURE.md`
- Full-system boundary, responsibilities, and call flow.

2. `REPO_STRUCTURE.md`
- Current repository structure, installed project shape, and `0.4` product/structure direction.

3. `LOOMLE_040_RUNTIME_CONNECTIVITY.md`
- Runtime connection model for the project-local `loomle` client and Unreal-hosted MCP runtime.

4. `LOOMLE_040_CLI_SURFACE.md`
- Current `loomle` CLI contraction: stdio MCP proxy only, with install/update moved to scripts.

5. `LOOMLE_040_PROJECT_LOCAL_UPDATE_MODEL.md`
- Project-local update model using stable entrypoints, versioned client payloads, and `Loomle/install/active.json`.

6. `MCP_PROTOCOL.md`
- Current MCP tool surface and behavior contract.

7. `RPC_INTERFACE.md`
- Historical Unreal bridge RPC contract, kept as transition/reference material.

8. `../workspace/Loomle/README.md`
- Agent-facing workspace entrypoint for installed project content, graph guides, catalogs, and examples.

## Supporting Design Docs

9. `INSTALL_ENTRYPOINT_DESIGN.md`
- Install entrypoint strategy and homepage content guidance for humans and agents.

10. `packaging/install/INSTALL_CONTRACT.md`
- Implementation-level install contract for bundle layout, owned paths, and installer responsibilities.

11. `LOOMLE_040_CPP_MCP_SDK_MINIMAL_DESIGN.md`
- Design note for the native C++ MCP runtime layer inside `LoomleBridge`.

12. `LOOMLE_PERMISSION_MODEL.md`
- Permission and trust model for LOOMLE runtime access.

## PCG / Graph Reference Docs

13. `PCG_LEARNING_RESOURCES_UE57.md`
- Curated UE 5.7 PCG learning resources.

14. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
- Mapping from Epic PCG categories to LOOMLE semantic taxonomy.

15. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
- Proposed PCG semantic-op family tree and naming.

## Local Issues

17. `issues/README.md`
- Local issue tracker index for in-repo design issues.

18. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
- Parent issue for making PCG `graph.query` the primary full-coverage readback surface.

19. `issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md`
- Blueprint embedded-template node classification and testing direction.

20. `issues/GRAPH_QUERY_SURFACE_MODEL.md`
- Shared graph-query surface issue covering `effectiveSettings`, `childGraphRef`, and promoted read surfaces.

21. `issues/BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md`
- Blueprint query-gap issue for promoted product capabilities.

22. `issues/JOBS_LONG_RUNNING_TASK_RUNTIME.md`
- Shared long-task runtime and top-level `jobs` issue.

23. `issues/PROFILING_RUNTIME_ANALYSIS_INTERFACE.md`
- Top-level `profiling` interface issue and implementation notes.

## Recommended Read Order

1. `../workspace/Loomle/README.md`
2. `ARCHITECTURE.md`
3. `REPO_STRUCTURE.md`
4. `LOOMLE_040_RUNTIME_CONNECTIVITY.md`
5. `LOOMLE_040_CLI_SURFACE.md`
6. `LOOMLE_040_PROJECT_LOCAL_UPDATE_MODEL.md`
7. `MCP_PROTOCOL.md`
8. `RPC_INTERFACE.md`
9. `INSTALL_ENTRYPOINT_DESIGN.md`
10. `packaging/install/INSTALL_CONTRACT.md`
11. `LOOMLE_040_CPP_MCP_SDK_MINIMAL_DESIGN.md`
12. `LOOMLE_PERMISSION_MODEL.md`
13. `PCG_LEARNING_RESOURCES_UE57.md`
14. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
15. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
16. `issues/README.md`
17. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
18. `issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md`
19. `issues/GRAPH_QUERY_SURFACE_MODEL.md`
20. `issues/BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md`
21. `issues/JOBS_LONG_RUNNING_TASK_RUNTIME.md`
22. `issues/PROFILING_RUNTIME_ANALYSIS_INTERFACE.md`
