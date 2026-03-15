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

For day-to-day local development, prefer the unified script:

- `python3 tools/dev_verify.py --project-root /path/to/Project`

That flow refreshes the project-local install first, then validates through `<ProjectRoot>/Loomle/loomle(.exe)`.
