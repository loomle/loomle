# LOOMLE Repository Structure

## Summary

The current `0.4` structure is intentionally narrow:

- Unreal owns runtime authority through `Plugins/LoomleBridge/`
- `loomle` remains a project-local client under `Loomle/`
- install and update are script-first

This phase does not introduce:

- global machine install
- global skills/workflows layers
- Studio directory migration
- a separate Rust runtime server layer

## Source Layout

### `engine/LoomleBridge/`

Owns:

- Unreal integration
- native MCP runtime
- authority-side tool execution

### `client/`

Owns:

- the `loomle` client
- MCP client/session behavior
- source-of-truth install and update scripts

Rules:

- `install.*` is site-served only
- `update.*` is copied into installed projects
- `loomle` itself is not a multi-command install CLI

### `workspace/Loomle/`

Owns project-local install material copied into:

- `<ProjectRoot>/Loomle/`

It remains install material, not a Studio artifact layer.

### `site/`

Owns the published install site:

- `https://loomle.ai/`
- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

### `packaging/`

Owns:

- release bundle assembly
- manifest shape
- install contract
- local install helpers

## Installed Project Shape

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    loomle(.exe)
    update.(sh|ps1)
    README.md
    install/
    state/
    workflows/
    examples/
```

## Runtime Shape

At runtime, the split is:

- `Loomle/loomle(.exe)` = MCP client / launcher only
- `Plugins/LoomleBridge/` = native MCP runtime authority
- `Loomle/install/` = install metadata and versioned payloads
- `Loomle/state/` = machine-written runtime outputs

The old Rust `mcp/server` layer is gone. Runtime protocol ownership now lives in
`LoomleBridge`.

## Product Direction In This Structure

For the current `0.4` cut:

- `loomle` remains project-local
- `loomle` is the primary agent-facing MCP entrypoint
- install/update stay script-first
- bootstrap is site-served
- installed projects keep only project-local maintenance scripts

## Deferred Work

Still deferred beyond this cut:

- global `loomle`
- global capability layers
- hidden `.loomle-core/`
- Studio/project artifact restructuring

## Decision

Document the current structure as:

- project-local `loomle`
- Unreal-hosted native MCP runtime
- script-first install/update
- no global install or Studio migration in this phase
