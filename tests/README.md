# Test Layer

This directory holds formal LOOMLE test organization.

Current split:
- `unit/`
- `integration/`
- `e2e/`
- `fixtures/`

Current contents:
- `e2e/test_bridge_smoke.py`
- `e2e/test_bridge_regression.py`
- `e2e/test_bridge_windows.ps1`
- `e2e/cleanup_bridge_test_assets.py`
- `integration/test_loomle_latency.py`
- `GRAPH_TEST_FRAMEWORK.md`
- `GRAPH_TEST_PROFILES.md`
- `GRAPH_TEST_RECIPES.md`
- `GRAPH_TEST_FIXTURES.md`
- `GRAPH_TEST_CATALOG_SCHEMA.md`
- `GRAPH_TEST_PLAN_SCHEMA.md`
- `GRAPH_TEST_ROADMAP.md`
 - `MATERIAL_TEST_HANDOFF.md`

Design references:
- `GRAPH_TEST_FRAMEWORK.md` - top-level architecture for unified graph testing across Blueprint, Material, and PCG
- `GRAPH_TEST_PROFILES.md` - testing profile vocabulary and family defaults
- `GRAPH_TEST_RECIPES.md` - minimal legal context templates for context-bound nodes
- `GRAPH_TEST_FIXTURES.md` - standard fixture registry for reusable graph testing bottoms
- `GRAPH_TEST_CATALOG_SCHEMA.md` - how testing metadata should live in node databases
- `GRAPH_TEST_PLAN_SCHEMA.md` - JSON schema for generated per-graph test plans
- `GRAPH_TEST_ROADMAP.md` - phased implementation order for the `0.4` test refactor
- `MATERIAL_TEST_HANDOFF.md` - short handoff guide for product engineers using the current Material test system
- `PCG_TEST_HANDOFF.md` - short handoff guide for product engineers using the current PCG test system

For day-to-day local development, prefer the unified script:

- `python3 tools/dev_verify.py --project-root /path/to/Project`

That flow refreshes the project-local install first, then validates through `<ProjectRoot>/Loomle/loomle(.exe)`.
