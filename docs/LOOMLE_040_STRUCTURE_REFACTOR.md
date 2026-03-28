# LOOMLE 0.4.0 Structure Refactor

## Summary

The first `LOOMLE 0.4.0` structure refactor should be much smaller than the
earlier broad proposal.

For this phase, structure changes should be limited to:

- native MCP runtime ownership moving into `LoomleBridge`
- project-local client/runtime ownership staying under `Loomle/`
- removal of the old Rust `mcp/server` transitional layer

It should explicitly **not** include:

- global LOOMLE home
- global capability split
- Studio artifact migration
- `loomle/` vs `.loomle-core/` project migration

## Installed Project Shape For First 0.4 Cut

The installed project shape remains:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    loomle(.exe)
    runtime/
    workflows/
    examples/
```

This shape is intentionally conservative.

## What Actually Changes

The structural change in this phase is runtime-internal, not user-facing.

Before:

- project-local client
- Unreal custom RPC behind that client

After:

- project-local client
- native MCP runtime inside `LoomleBridge`
- no separate Rust runtime server layer

So the practical structure move is:

```text
Current                         -> First 0.4 target
client/                         -> retained
engine/LoomleBridge/            -> runtime authority + native MCP core
workspace/Loomle/               -> retained as project-local install material
```

## Repository Meaning In This Phase

### `engine/LoomleBridge/`

Owns:

- Unreal integration
- native MCP runtime
- authority-side tool dispatch

### `client/`

Owns:

- project-local `loomle`
- MCP stdio proxy behavior
- bootstrap and maintenance scripts in source form

### `workspace/Loomle/`

Still owns project-local install material.

For this phase, it should not be reinterpreted as Studio artifact storage.

## Deferred Structure Work

The following remain valid future directions but are not part of this cut:

- global `loomle`
- global skills/workflows/templates
- visible `loomle/` collaboration layer
- hidden `.loomle-core/` internal layer
- Studio-specific project artifact ownership

Those changes should wait until after runtime and install are stable on the new
native MCP path.

## Why This Scope Is Correct

If `0.4` tries to move runtime protocol, install model, and Studio/project
layout at the same time, then:

- tests become harder to interpret
- upgrade behavior becomes harder to reason about
- docs have to explain too many migrations at once

The first `0.4` structure refactor should therefore only remove the obsolete
runtime layer and clarify current project-local ownership.

## Decision

For the first `LOOMLE 0.4.0` cut:

- keep project-local install shape
- remove Rust `mcp/server`
- move runtime protocol ownership fully into `LoomleBridge`
- defer global and Studio structure migration
