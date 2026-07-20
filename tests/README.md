# Loomle Test Migration

> **Status: 0.7 migration in progress.** The Python and PowerShell suites in
> this directory exercise the retired 0.6 Client and direct-tool surface. They
> remain only as coverage and fixture input until the SAL/TypeScript E2E harness
> replaces them. They are not valid 0.7 verification entrypoints.

## Current 0.7 Validation

The active repository checks are owned by the npm workspaces and packaging
layer:

```bash
npm test
npm run build:executable -- --target darwin-arm64
npm run test:executable -- --target darwin-arm64
npm run assemble:fab -- \
  --output-dir .tmp/fab/darwin-arm64 \
  --target darwin-arm64
```

UE Bridge and final artifact compilation use UE 5.7 `RunUAT BuildPlugin`.
`.github/workflows/verify-fab-mac.yml` is the current manual QA path. It does
not publish a release.

## Legacy Migration Input

- `e2e/test_bridge_smoke.py`: 0.6 umbrella smoke and reusable UE setup/fixture
  logic.
- `e2e/test_bridge_regression.py`: 0.6 direct-tool regression coverage.
- `e2e/test_bridge_windows.ps1`: 0.6 Windows verification.
- `integration/test_loomle_latency.py`: old Client path and MCP benchmark.
- `tools/run_blueprint_*.py`, `tools/run_material_*.py`, and
  `tools/run_pcg_*.py`: old public-contract suites and domain-specific
  assertions.
- the root-level test handoff, framework, profile, recipe, and roadmap
  documents: historical coverage requirements that must be reclassified while
  their tests migrate.

The replacement harness must launch the standalone TypeScript Client, discover
the Bridge through current runtime records, call the four 0.7 MCP tools, and
express Blueprint/Widget work through SAL. Establish that coverage before
deleting the old suites or their reusable Unreal fixture helpers.
