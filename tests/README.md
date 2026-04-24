# Test Layer

This directory holds formal LOOMLE test organization.

## Layout

- `unit/`
- `integration/`
- `e2e/`
- `fixtures/`
- `tools/`

## Main Entrypoints

- `e2e/test_bridge_smoke.py`: umbrella smoke validation
- `e2e/test_bridge_regression.py`: broader regression validation
- `e2e/test_bridge_windows.ps1`: Windows bridge validation
- `integration/test_loomle_latency.py`: latency checks
- `tools/run_blueprint_*.py`: Blueprint suites using the `blueprint.*` public contract
- `tools/run_material_*.py`: Material suites using the `material.*` public contract
- `tools/run_pcg_*.py`: PCG suites using the `pcg.*` public contract

For day-to-day local development, prefer:

```bash
python3 tools/dev_verify.py --project-root /path/to/Project
```

That flow builds the checkout client, syncs `LoomleBridge` into the dev
project, starts Unreal Editor, attaches through MCP, and runs the selected
validation suites.

## Current Coverage

Smoke and regression coverage includes:

- global-client MCP startup
- project runtime discovery and attach
- editor context and command execution
- domain query/mutation/verification coverage
- top-level `jobs`
- `execute` submission with `execution.mode = "job"`
- lifecycle reads through `jobs.status`, `jobs.result`, `jobs.logs`, and
  `jobs.list`

## Design References

- `GRAPH_TEST_FRAMEWORK.md`
- `GRAPH_TEST_PROFILES.md`
- `GRAPH_TEST_RECIPES.md`
- `GRAPH_TEST_FIXTURES.md`
- `GRAPH_TEST_CATALOG_SCHEMA.md`
- `GRAPH_TEST_PLAN_SCHEMA.md`
- `GRAPH_TEST_ROADMAP.md`
- `BLUEPRINT_TEST_HANDOFF.md`
- `MATERIAL_TEST_HANDOFF.md`
- `PCG_TEST_HANDOFF.md`
- `JOBS_RUNTIME_SYNC_HANDOFF.md`
