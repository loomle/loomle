# LOOMLE 0.4.0 Runtime Connectivity

## Summary

This document defines the first `0.4.0` runtime connectivity model.

Target direction:

- `loomle` is the primary agent-facing runtime entrypoint
- `loomle` remains project-local in this phase
- Unreal hosts the runtime authority
- `LoomleBridge` serves native MCP directly
- transport remains project-scoped socket/pipe

## Responsibility Split

The runtime stack in this phase is:

- agent/host -> `Loomle/loomle`
- project-local client -> project-derived endpoint
- `LoomleBridge` native MCP runtime -> Unreal authority

Key clarification:

- `loomle` is an MCP client and launcher
- `loomle` is not the runtime server
- `LoomleBridge` is the runtime server and authority

## `loomle` Role

`loomle` should be the standard MCP session surface for agents.

It should provide:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

The external protocol should stay standard MCP.

## Project Discovery

The first `0.4` scope should assume one project per workflow.

Discovery order:

1. explicit `--project-root`
2. current working directory upward search for `.uproject`

No active-project registry is required in this phase.

## Runtime Endpoint Resolution

After project discovery, the client should derive the endpoint from project
rules:

- Unix: `<ProjectRoot>/Intermediate/loomle.sock`
- Windows: `\\.\pipe\loomle-<fnv64(project_root)>`

This keeps runtime lookup:

- deterministic
- project-bound
- easy to validate

## Server Direction

The first `0.4` server direction is no longer Python-hosted MCP.

It is:

- Unreal-hosted native MCP runtime
- implemented inside `LoomleBridge`
- backed by the new `mcp core`

This is now the primary runtime direction because:

- it removes the old custom RPC layer from the main path
- it removes the extra Rust runtime server hop
- it keeps runtime authority exactly where Unreal already owns it

## Transport Direction

The first `0.4` transport direction remains local IPC:

- Unix socket
- Windows named pipe

No HTTP transport is required for this phase.

## Session Model

The client/runtime interaction should be session-oriented.

Reasons:

- standard MCP is session-oriented
- repeated project discovery and readiness should be paid once
- the runtime should support multi-call workflows cleanly

One-shot wrappers may still exist, but they are not the primary contract.

## Runtime Readiness

The runtime should expose a clear readiness distinction:

- endpoint missing
- endpoint exists but runtime not ready
- ready
- version/protocol mismatch

## Relationship To Install

This connectivity model assumes only project-local installation:

- `Loomle/loomle(.exe)` exists in the target project
- `Plugins/LoomleBridge/` exists in the target project
- no global LOOMLE install is required in this phase

## Deferred Work

Not part of this phase:

- global client install
- global attach/init model
- multi-project routing
- HTTP transport
- Studio directory migration

## Decision

The first `LOOMLE 0.4.0` runtime connectivity model should be:

- project-local `loomle`
- `loomle` as the primary MCP client entrypoint
- Unreal-hosted native MCP runtime in `LoomleBridge`
- project-derived socket/pipe endpoint lookup
- no global install requirement
