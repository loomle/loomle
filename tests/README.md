# Test Layer

This directory holds formal LOOMLE test organization.

Current split:
- `unit/`
- `integration/`
- `e2e/`
- `fixtures/`
- `tools/`

Current contents:
- `e2e/test_bridge_smoke.py`
- `e2e/test_bridge_regression.py`
- `e2e/test_bridge_windows.ps1`
- `e2e/cleanup_bridge_test_assets.py`
- `integration/test_loomle_latency.py`
- `tools/runner_init_host_project.sh`
- `tools/runner_init_host_project.ps1`
- `tools/generate_graph_test_plan.py`
- `tools/generate_graph_test_coverage_report.py`
- `tools/generate_graph_test_surface_report.py`
- `tools/generate_graph_test_truth_report.py`
- `tools/run_blueprint_*.py`
- `tools/run_material_*.py`
- `tools/run_pcg_*.py`
- `GRAPH_TEST_FRAMEWORK.md`
- `GRAPH_TEST_PROFILES.md`
- `GRAPH_TEST_RECIPES.md`
- `GRAPH_TEST_FIXTURES.md`
- `GRAPH_TEST_CATALOG_SCHEMA.md`
- `GRAPH_TEST_PLAN_SCHEMA.md`
- `GRAPH_TEST_ROADMAP.md`
- `BLUEPRINT_TEST_HANDOFF.md`
- `JOBS_RUNTIME_SYNC_HANDOFF.md`
- `MATERIAL_TEST_HANDOFF.md`
- `PCG_TEST_HANDOFF.md`

Design references:
- `GRAPH_TEST_FRAMEWORK.md` - top-level architecture for unified graph testing across Blueprint, Material, and PCG
- `GRAPH_TEST_PROFILES.md` - testing profile vocabulary and family defaults
- `GRAPH_TEST_RECIPES.md` - minimal legal context templates for context-bound nodes
- `GRAPH_TEST_FIXTURES.md` - standard fixture registry for reusable graph testing bottoms
- `GRAPH_TEST_CATALOG_SCHEMA.md` - how testing metadata should live in node databases
- `GRAPH_TEST_PLAN_SCHEMA.md` - JSON schema for generated per-graph test plans
- `GRAPH_TEST_ROADMAP.md` - phased implementation order for the `0.4` test refactor
- `BLUEPRINT_TEST_HANDOFF.md` - short handoff guide for product engineers using the current Blueprint test system
- `JOBS_RUNTIME_SYNC_HANDOFF.md` - test-sync handoff for the new top-level `jobs` runtime and `execute` job-mode submission
- `MATERIAL_TEST_HANDOFF.md` - short handoff guide for product engineers using the current Material test system
- `PCG_TEST_HANDOFF.md` - short handoff guide for product engineers using the current PCG test system

Test infrastructure helpers:
- `tests/tools/` - graph-test generators, suite runners, and host-project bootstrap scripts
- `tests/e2e/test_bridge_smoke.py` - umbrella validation entrypoint that shells into many `tests/tools/*` workers

For day-to-day local development, prefer the unified script:

- `python3 tools/dev_verify.py --project-root /path/to/Project`

That flow refreshes the project-local install first, then validates through `<ProjectRoot>/Loomle/loomle(.exe)`.

Current smoke and regression coverage also includes the shared long-running
runtime:

- top-level `jobs`
- `execute` submission with `execution.mode = "job"`
- lifecycle reads through `jobs.status`, `jobs.result`, `jobs.logs`, and `jobs.list`
