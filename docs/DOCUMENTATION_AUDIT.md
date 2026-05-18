# Documentation Audit

Status: current planning document for turning Loomle documentation into a
public documentation site.

## Goal

Build a public documentation system that is useful to users, agents, and
developers:

- users can install Loomle and understand what it does
- agents can follow stable tool workflows without guessing
- developers can keep interface design aligned with UE semantics

The public site should be concise, task-oriented, and UE-semantic. It should not
publish exploratory design history as if it were current contract.

## Classification

- `publish`: content is safe to expose publicly after light editing
- `rewrite`: content has useful source material but needs public-facing rewrite
- `internal`: keep in repo, not in public navigation
- `archive`: historical context only
- `decide`: needs product or ownership decision before publication

## Current Public Site

| Path | Status | Action |
| --- | --- | --- |
| `site/index.md` | publish | Keep as product entrypoint; expand value proposition after tool docs stabilize. |
| `site/install.md` | publish | Keep canonical install page; verify install scripts stay at root URLs. |
| `site/quickstart.md` | publish | Expand with one complete UE workflow once examples are selected. |
| `site/concepts/*` | publish | Keep concise; cross-link into deeper tool docs. |
| `site/tools/*` | rewrite | Current pages are skeletal indexes; expand from audited source docs and live tool schemas. |
| `site/workflows/*` | rewrite | Convert into tested, task-oriented examples. |
| `site/llms.txt` | publish | Keep short and stable for agent ingestion. |

## Current Design Docs

| Path | Status | Action |
| --- | --- | --- |
| `docs/README.md` | internal | Keep as repo documentation policy; update as public site matures. |
| `docs/BLUEPRINT_INTERFACE_DESIGN.md` | rewrite | Main Blueprint source of truth, but too long and partly design-history-shaped for direct publication. Extract public tool docs and keep detailed design internal. |
| `docs/blueprint/palette.md` | publish | Strong candidate for public `tools/blueprint/palette`; trim implementation-heavy sections. |
| `docs/blueprint/graph-edit.md` | rewrite | Useful boundary and schema material; update legacy `graphName` compatibility references before public use. |
| `docs/blueprint/graph-layout.md` | rewrite | Useful but has outdated `operation: "format"` and `direction` material; align with current public schema before publication. |
| `docs/blueprint/schema-inspect.md` | rewrite | Useful concept doc; broaden beyond Blueprint because current `schema.inspect` covers Blueprint, Material, PCG, and Widget second-layer schemas. |
| `docs/UE_SCENE_EDITING_EXECUTE_AUDIT.md` | decide | Untracked local file. Do not publish or edit until ownership is explicit. |

## Archive Docs

| Path | Status | Action |
| --- | --- | --- |
| `docs/archive/README.md` | archive | Keep as archive marker. |
| `docs/archive/legacy/ARCHITECTURE.md` | rewrite | Useful architecture source, but tool names are obsolete. Extract into public `concepts/runtime-model` only after updating names. |
| `docs/archive/legacy/LOOMLE_GLOBAL_INSTALL_MODEL.md` | rewrite | Useful source for install/project model. Extract current parts into `site/concepts/project-model.md` and `site/install.md`. |
| `docs/archive/legacy/LOOMLE_PERMISSION_MODEL.md` | decide | Potentially public if permission model becomes product-facing; otherwise internal. |
| `docs/archive/legacy/MCP_PROTOCOL.md` | archive | Obsolete tool catalog and protocol baseline; do not publish as current contract. |
| `docs/archive/legacy/RPC_INTERFACE.md` | internal | Runtime implementation boundary, not public user documentation. |
| `docs/archive/legacy/REPO_STRUCTURE.md` | internal | Developer-only; update separately if needed. |
| `docs/archive/legacy/spec-graph-domain-split.md` | archive | Historical design context only. |
| `docs/archive/legacy/BLUEPRINT_GRAPH_REFACTOR_GENERATE_RETIRED.md` | archive | Useful history for retired tools; do not publish in primary docs. |
| `docs/archive/legacy/PCG_LEARNING_RESOURCES_UE57.md` | rewrite | Potential source list for PCG docs; verify links and UE version before publishing. |
| `docs/archive/legacy/PCG_OFFICIAL_CATEGORIES_AND_SEMANTIC_TAXONOMY.md` | rewrite | Valuable PCG semantic source; rewrite around current `pcg.palette`, `pcg.graph.*`, `pcg.node.inspect`, and `pcg.parameter.*`. |
| `docs/archive/legacy/PCG_SEMANTIC_OPS_FINAL_DRAFT.md` | archive | Likely superseded by current PCG public tool shape; mine for terminology only. |

## Local Issue Docs

| Path | Status | Action |
| --- | --- | --- |
| `docs/archive/legacy/issues/README.md` | internal | Local issue tracker policy; not public docs. |
| `docs/archive/legacy/issues/BLUEPRINT_EMBEDDED_TEMPLATE_NODES.md` | rewrite | Potential advanced Blueprint concept after current node docs mature. |
| `docs/archive/legacy/issues/BLUEPRINT_GAP_PRODUCT_CAPABILITIES.md` | internal | Product planning; not public contract. |
| `docs/archive/legacy/issues/GRAPH_QUERY_SURFACE_MODEL.md` | rewrite | Mine for graph inspect conceptual docs after current graph inspect pages exist. |
| `docs/archive/legacy/issues/JOBS_LONG_RUNNING_TASK_RUNTIME.md` | rewrite | Potential public `tools/runtime/jobs` and `concepts/jobs` source. Needs current API verification. |
| `docs/archive/legacy/issues/PCG_GRAPH_QUERY_FULL_COVERAGE_UPGRADE.md` | rewrite | Mine for PCG inspect documentation after current behavior is verified. |
| `docs/archive/legacy/issues/PIE_PLAY_SESSION_CONTROL.md` | rewrite | Potential public `tools/editor/play` source. Needs current API verification. |
| `docs/archive/legacy/issues/PROFILING_RUNTIME_ANALYSIS_INTERFACE.md` | decide | Profiling tool exists but is not in smoke required tools. Confirm public readiness before publishing. |

## Workspace Archive

| Path | Status | Action |
| --- | --- | --- |
| `docs/archive/workspace/Loomle/blueprint/*` | rewrite | Contains useful examples and catalogs, but public docs should not expose raw generated databases as primary docs. |
| `docs/archive/workspace/Loomle/material/*` | rewrite | Mine examples for Material workflows. |
| `docs/archive/workspace/Loomle/pcg/*` | rewrite | Mine examples for PCG workflows. |
| `docs/archive/workspace/Loomle/widget/GUIDE.md` | rewrite | Mine for Widget docs. |
| `docs/archive/workspace/tools/*` | internal | Generation tooling, not public docs. |
| `docs/archive/workspace/skills/*` | archive | Not part of current Loomle public docs. |

## Public Documentation Gaps

The first public site skeleton exists, but these pages need real content before
the docs can be considered externally complete:

- `tools/project`: `loomle`, `project.list`, `project.attach`, `project.install`, `schema.inspect`
- `tools/asset`: `asset.create`, `asset.inspect`, `asset.edit`
- `tools/blueprint`: graph inspect/edit/layout, palette, member/class/node docs, compile
- `tools/material`: palette, graph inspect/edit/layout, node inspect/edit, compile
- `tools/pcg`: palette, graph inspect/edit/layout, node inspect, parameter inspect/edit, compile
- `tools/widget`: palette, tree inspect/edit, widget inspect, compile
- `tools/editor`: `editor.open`, `editor.focus`, `editor.screenshot`, `play`
- `tools/observability`: `diagnostic.tail`, `log.tail`, `jobs`, `execute`
- workflow pages with tested examples

## Recommended Next Steps

1. Create public tool landing pages for `project` and `asset`, because every
   user and agent sees those before domain tools.
2. Expand Blueprint public docs from the current trusted Blueprint design docs.
3. Create `MATERIAL_INTERFACE_DESIGN.md`, `PCG_INTERFACE_DESIGN.md`, and
   `WIDGET_INTERFACE_DESIGN.md` internal source docs before expanding those
   public sections heavily.
4. Add a lightweight doc check that validates Jekyll front matter and rejects
   broken local links.
5. Keep archived docs out of public navigation unless explicitly rewritten.
