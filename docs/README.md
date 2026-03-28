# LOOMLE Docs

Project-level technical documentation for `LOOMLE` and `LoomleBridge` (plugin + runtime authority).

## Documents

1. `ARCHITECTURE.md`
- Full-system boundary, responsibilities, and call flow.

2. `REPO_STRUCTURE.md`
- Source repository, release bundle, and installed user-project structure.

3. `LOOMLE_040_PRODUCT_DIRECTION.md`
- Product-level source of truth for the first `0.4.0` cut: project-local
  `loomle`, native MCP runtime in `LoomleBridge`, and script-first
  install/update.

4. `LOOMLE_040_STRUCTURE_REFACTOR.md`
- `0.4.0` structure decision for the narrowed first cut: keep project-local
  shape, remove Rust `mcp/server`, defer global and Studio restructuring.

5. `LOOMLE_040_INSTALL_UPGRADE_DESIGN.md`
- `0.4.0` install/update design for the narrowed first cut: project-local only,
  script-first, no installer binary, no global install.

6. `LOOMLE_040_RUNTIME_CONNECTIVITY.md`
- `0.4.0` runtime connection model: project-local `loomle`, `loomle mcp`,
  Unreal-hosted native MCP runtime, and project-scoped pipe/socket direction.

7. `LOOMLE_040_CPP_MCP_SDK_MINIMAL_DESIGN.md`
- `0.4.0` design for a self-owned minimal C++ MCP server layer inside
  `LoomleBridge`, intended to replace the custom RPC layer as the primary
  runtime protocol path without waiting for a mature external C++ MCP SDK.

8. `INSTALL_ENTRYPOINT_DESIGN.md`
- Prompt-first install entrypoint strategy for humans and agents.

9. `INSTALL_PAGE_CONTENT.md`
- Concrete homepage content and information architecture for `loomle.ai` / `loomle.ai/i`.

10. `RPC_INTERFACE.md`
- Historical/current pre-cutover Unreal bridge RPC contract (`rpc.health`,
  `rpc.capabilities`, `rpc.invoke`). Useful for understanding the old path, but
  not the target `0.4` runtime protocol direction.

11. `MCP_PROTOCOL.md`
- Current MCP tool surface, tool schemas, routing rules, long-running
  `execute` job mode, and top-level `jobs` behavior contract. This remains
  useful during transition, but the `0.4` target protocol source of truth moves
  toward native MCP in `LoomleBridge`.

12. `PCG_LEARNING_RESOURCES_UE57.md`
- Curated UE 5.7 PCG learning resources, including official docs, videos, and transcript-first video reading guidance.

13. `PCG_NODE_CATALOG_VS_OPS.md`
- Clarifies the boundary between the `pcg-weaver` node catalog, LOOMLE semantic PCG ops, and generic graph construction.

14. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
- Aligns Epic's official PCG node categories with a behavior-first semantic taxonomy for future LOOMLE PCG ops.

15. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
- Final proposed LOOMLE PCG semantic-op family tree, canonical names, compatibility aliases, and first-wave op set.

16. `../workspace/Loomle/README.md`
- Agent-facing workspace entrypoint. Start here first for the current graph workflow, then follow each graph domain's `GUIDE.md`, `SEMANTICS.md`, catalogs, and examples.

17. `GRAPH_OPS_DESIGN.md`
- Historical `graph.ops` / `graph.ops.resolve` design. These tools are no longer part of the active product tool surface and remain only as design history.

18. `GRAPH_OPS_PROTOCOL_DRAFT.md`
- Historical draft protocol for `graph.ops` and `graph.ops.resolve`. Keep only as archival context.

19. `UE_SCENE_EDITING_EXECUTE_AUDIT.md`
- Audit of what Unreal scene editing is already proven through `execute`, what is only partially proven, and what gaps remain before it becomes a comfortable agent workflow.

20. `issues/README.md`
- Local issue tracker index for detailed design issues that live in-repo.

21. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
- Local parent issue for making PCG `graph.query` the primary full-coverage node readback surface.

22. `issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md`
- Local issue for classifying Blueprint embedded-template nodes such as `Timeline`
  and `AddComponent`, and for defining their testing-system direction.

23. `issues/GRAPH_QUERY_SURFACE_MODEL.md`
- Shared local issue for the cross-graph `graph.query` surface model, including
  `effectiveSettings`, `childGraphRef`, and explicit promoted surface categories.

24. `issues/BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md`
- Local issue for promoting Blueprint query gaps into product capabilities such
  as `embedded_template`, `graph_boundary_summary`, and
  `context_sensitive_construct`.

25. `issues/JOBS_LONG_RUNNING_TASK_RUNTIME.md`
- Local issue for introducing a shared long-task runtime with top-level `jobs`
  management, action-based lifecycle inspection, and tool-level
  `execution.mode = "job"` submission.

26. `issues/PROFILING_RUNTIME_ANALYSIS_INTERFACE.md`
- Local issue for introducing a top-level `profiling` interface as an official
  Unreal profiling data bridge, including `unit`, `game`, `gpu`, `ticks`,
  `memory`, and `capture`.
- Current implementation status:
  - `profiling.action = "unit"` is live
  - `profiling.action = "game"` is live
  - `profiling.action = "gpu"` is live
  - `profiling.action = "ticks"` is live
  - `profiling.action = "memory"` is live for `kind = "summary"`

## Recommended Read Order

1. `../workspace/Loomle/README.md`
2. `ARCHITECTURE.md`
3. `LOOMLE_040_PRODUCT_DIRECTION.md`
4. `LOOMLE_040_STRUCTURE_REFACTOR.md`
5. `LOOMLE_040_INSTALL_UPGRADE_DESIGN.md`
6. `LOOMLE_040_RUNTIME_CONNECTIVITY.md`
7. `LOOMLE_040_CPP_MCP_SDK_MINIMAL_DESIGN.md`
8. `REPO_STRUCTURE.md`
9. `INSTALL_ENTRYPOINT_DESIGN.md`
10. `INSTALL_PAGE_CONTENT.md`
11. `RPC_INTERFACE.md`
12. `MCP_PROTOCOL.md`
13. `PCG_LEARNING_RESOURCES_UE57.md`
14. `PCG_NODE_CATALOG_VS_OPS.md`
15. `PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md`
16. `PCG_SEMANTIC_OPS_FINAL_DRAFT.md`
17. `GRAPH_OPS_DESIGN.md`
18. `GRAPH_OPS_PROTOCOL_DRAFT.md`
19. `UE_SCENE_EDITING_EXECUTE_AUDIT.md`
20. `issues/README.md`
21. `issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md`
22. `issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md`
23. `issues/GRAPH_QUERY_SURFACE_MODEL.md`
24. `issues/BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md`
25. `issues/JOBS_LONG_RUNNING_TASK_RUNTIME.md`
26. `issues/PROFILING_RUNTIME_ANALYSIS_INTERFACE.md`
