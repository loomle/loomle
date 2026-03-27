# LOOMLE 0.4.0 Runtime Connectivity

## Summary

This document defines the `0.4.0` runtime connectivity model.

Target direction:

- `loomle mcp` is the primary agent-facing runtime protocol entrypoint
- `loomle` is a client/launcher, not the runtime server
- Unreal hosts the runtime authority
- the first `0.4.0` transport direction is local pipe/socket

## Responsibility Split

The runtime stack should be understood like this:

- agent -> `loomle mcp`
- `loomle mcp` -> project runtime server
- project runtime server -> Unreal runtime/editor authority

Key clarification:

- `loomle` replaces the Codex-side connection/configuration layer
- `loomle` does not replace the backend runtime server

## `loomle mcp` Role

`loomle mcp` should be the formal long-lived protocol surface for agents.

It should provide a stable stdin/stdout request/response session.

Minimum supported methods:

- `initialize`
- `tools/list`
- `tools/call`

Protocol guarantees:

- stdout emits protocol frames only
- stderr emits logs and diagnostics only
- errors use a stable structured model

## Why Session Mode Is Primary

The runtime path should be session-oriented rather than one-shot-oriented.

Reasons:

- this aligns with MCP's native interaction model
- agent workflows are usually multi-call rather than single-call
- project discovery and runtime readiness should be paid once per session
- repeated connection setup should be avoided where possible

One-shot wrappers may still exist temporarily during migration, but they should
not define the long-term runtime contract.

## Project Discovery

First `0.4.0` scope should not depend on an active-project registry.

Instead, `loomle` should discover the target project in this order:

1. explicit `--project-root`
2. current working directory upward search for `.uproject`

If neither works, the session should fail with an explicit project-discovery
error.

## Runtime Endpoint Resolution

After discovering the project, `loomle` should derive the runtime endpoint from
project-local rules rather than broad machine scanning.

First-phase direction:

- Unix: project-scoped socket path
- Windows: project-scoped named pipe

This keeps runtime lookup:

- deterministic
- project-bound
- easy to validate

## Handshake Requirements

After resolving the candidate endpoint, `loomle` should perform a lightweight
handshake before treating the runtime as ready.

The handshake should confirm:

- a server is actually listening
- the server is ready
- the server belongs to the expected project
- protocol/runtime versions are compatible

Directionally, the handshake should expose:

- project name
- project root
- `.uproject` path
- editor/runtime pid or instance identity
- protocol version
- runtime readiness state

## First 0.4 Server Direction

The first `0.4.0` server direction should be:

- Unreal-hosted Python MCP server
- plugin-managed lifecycle
- no extra Rust/bridge RPC hop between server and Unreal runtime

This direction is appropriate because:

- Unreal Python is already required
- `execute` already depends on Python execution
- it removes an unnecessary middle layer

## Transport Direction

The first `0.4.0` transport direction should be local pipe/socket.

Reasons:

- better local-IPC fit for the first in-process runtime model
- lower product friction than introducing a local network-service assumption
- reuse of existing project-scoped endpoint patterns

HTTP may be added later if broader client interoperability is needed.

HTTP is not required for the first `0.4.0` runtime migration.

## Server Lifecycle Direction

Directionally, Unreal/plugin startup should ensure the runtime server becomes
available and remains ready for `loomle mcp` client sessions.

That means:

- the runtime server should be started or initialized as part of plugin startup
- readiness must be explicit
- `loomle` should not carry primary responsibility for spawning the runtime
  server each time

## Editor Status Signal

The Unreal plugin should expose a minimal editor-visible status signal for the
runtime server.

The first `0.4.0` version should stay intentionally small:

- show a small status icon in the editor UI
- provide hover text only
- do not open a dedicated panel
- do not provide restart controls in the first version

The purpose is simple operational visibility.

Users should be able to tell at a glance whether the runtime is:

- `Disabled`
- `Starting`
- `Ready`
- `Error`

Hover text should stay concise and prefer:

- current LOOMLE runtime state
- transport or endpoint summary
- project identity when useful
- last high-level error summary when not ready

This should provide a visible readiness signal without turning the first
runtime integration into a larger UI surface.

## Single-Project Scope

The first runtime scope should assume one active project per agent workflow.

Implications:

- no active-project registry required initially
- no automatic multi-project routing required initially
- runtime identity must still be explicit in handshake/status responses

## Failure Model

Runtime errors should distinguish at least:

- project not found
- project endpoint missing
- server not listening
- server not ready
- project identity mismatch
- protocol/version mismatch

`doctor` should evolve to surface these runtime states directly.

## Relationship To Install/Upgrade

This runtime model implies:

- `loomle` must exist as a global machine-level executable
- project integration must install the runtime server side with the Unreal
  project/plugin
- compatibility between global `loomle` and project/runtime integration must be
  checked explicitly

## Decision

The first `0.4.0` runtime connectivity model should be:

- global `loomle` client/launcher
- `loomle mcp` as the primary agent protocol surface
- Unreal-hosted Python runtime server
- local pipe/socket transport
- project-derived endpoint lookup
- no active-project registry in first scope
