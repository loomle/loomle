# LOOMLE 0.4.0 Product Direction

## Summary

`LOOMLE 0.4.0` should formalize a new product boundary:

- `loomle` becomes a globally installed client/launcher
- `loomle mcp` becomes the primary agent-facing protocol entrypoint
- Unreal runtime authority stays with the project integration layer
- Codex-managed MCP configuration is no longer the primary runtime path

`0.4.0` is therefore not just an install refactor. It is also a runtime access
refactor.

## Why 0.4 Needs A Product Reset

The current `0.3.x` and `LOOMLE Lite` direction emphasized:

- project-local install surfaces
- manual project-local runtime entrypoints
- minimal machine-wide assumptions

That model was useful while LOOMLE was still proving a compact local kernel.

It becomes weaker once LOOMLE needs:

- a stable agent-facing entrypoint
- a clean approval story for host execution
- reusable machine-level runtime tooling
- a runtime path that does not depend on Codex MCP session/config lifecycle

`0.4.0` should explicitly move to a global-entrypoint model.

## Product Decision

`LOOMLE 0.4.0` should adopt the following direction.

### 1. Global `loomle` CLI

`loomle` should be installed globally on the machine.

Its role is:

- discover the current Unreal project
- discover and connect to the project runtime/server
- expose a stable protocol entrypoint for agents
- provide install/update/doctor lifecycle commands

`loomle` is not the Unreal runtime server.

### 2. `loomle mcp` As The Primary Agent Interface

The primary agent-facing runtime interface should be:

- `loomle mcp`

This command should expose a long-lived stdin/stdout protocol surface that is
session-oriented and MCP-aligned.

This is the main runtime contract for agents.

### 3. LOOMLE Owns Runtime Connection

LOOMLE should stop depending on Codex-native MCP configuration as the main
runtime integration path.

Instead:

- the host/agent executes `loomle`
- `loomle` owns project discovery
- `loomle` owns runtime discovery
- `loomle` owns backend connection details

This keeps the approval story understandable:

- the host approves running `loomle`
- LOOMLE handles the rest

### 4. Unreal Owns Runtime Authority

The runtime authority remains on the Unreal side.

Directionally for `0.4.0`:

- Unreal plugin startup should ensure the runtime server is available
- the runtime server should be hosted with the project integration
- `loomle` should connect to that server as a client

### 5. Early Install And Upgrade Should Stay Transparent

The first `0.4.0` install/upgrade direction should optimize for agent-friendly
execution and fast product iteration.

That means:

- prefer small official bootstrap/update scripts over a temporary downloaded
  installer binary
- keep install/update actions user-level by default
- keep complex compatibility and project migration logic inside `loomle`, not
  inside scripts

This preserves flexibility while the machine install and project attach model
are still evolving.

## First 0.4 Runtime Direction

The first `0.4.0` runtime direction should be:

- Unreal-hosted Python MCP server
- `loomle mcp` as the external client/launcher
- local pipe/socket transport

This direction is practical because:

- Unreal Python is already a product dependency
- `execute` already depends on Python execution
- it removes the extra Rust/bridge RPC hop
- it avoids requiring a full C++ MCP implementation immediately

HTTP may still be added later, but it should not be required for the first
`0.4.0` runtime migration.

## Single-Project Assumption

The first `0.4.0` runtime model should assume one active project per agent
workflow.

That means:

- do not require active-project registry in the first phase
- prefer current-project discovery plus project-derived endpoint rules
- prefer explicit, understandable runtime identity over automatic multi-project
  routing

This scope choice should not be expanded into alternate install modes for
`0.4.0`.

Directionally:

- support global `loomle` install only
- support project-level attach only
- do not introduce a project-local trial install mode
- do not introduce an engine-level plugin install mode

## Approval Model

`0.4.0` should treat host approval as a normal part of runtime use.

The design goal is not to eliminate approval entirely.

The design goal is to make approval:

- concentrated on one stable executable
- understandable to users
- decoupled from Codex MCP config mutation or hot-attach behavior

## Relationship To Earlier Lite Direction

This document supersedes the earlier lightweight assumption that LOOMLE should
avoid:

- global commands
- machine-wide installation
- stronger runtime infrastructure

That earlier direction was useful for the local-kernel phase.

`0.4.0` should now move beyond it.

## Related 0.4 Documents

- `LOOMLE_040_STRUCTURE_REFACTOR.md`
- `LOOMLE_040_INSTALL_UPGRADE_DESIGN.md`
- `LOOMLE_040_RUNTIME_CONNECTIVITY.md`

## Decision

`LOOMLE 0.4.0` should be documented and implemented as:

- a globally installed `loomle` client/launcher
- a project-attached Unreal integration
- a `loomle mcp` primary agent protocol entrypoint
- a runtime model owned by LOOMLE rather than Codex MCP configuration
