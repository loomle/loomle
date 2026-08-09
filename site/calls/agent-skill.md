---
layout: default
title: Agent Skill
parent: MCP Calls
nav_order: 5
description: Discover and load MCP-managed Loomle workflow Skills without a separate host installation.
---

# Agent Skill

`agent_skill` discovers and loads workflow guidance embedded in the same
self-contained Loomle Client as the other MCP tools. It is local, read-only,
and does not require a project binding, Loomle Bridge, or running Unreal
Editor.

The tool does not add UE capabilities. A Skill tells an agent how to compose
the existing Loomle calls for a specialized task; tool schemas, SAL interfaces,
live UE state, dry-run validation, and user authorization remain authoritative.

## Automatic Discovery

The `agent_skill` tool description includes every resident Skill name and
trigger description. MCP hosts expose that description to the model with the
normal tool catalog. When a task matches one of those descriptions, the agent
loads the exact Skill before planning the specialized work.

Users configure Loomle MCP once. They do not install a second Skill copy in
Codex, Claude, or another MCP host.

## List Resident Skills

```text
agent_skill({})
```

The result lists the Skill names and trigger descriptions compiled into the
installed Client.

## Load One Skill

```text
agent_skill({ name: "format-unreal-blueprints" })
```

For runtime PIE debugging through the Python fallback:

```text
agent_skill({ name: "debug-unreal-pie-with-python" })
```

Before any unrestricted Python fallback:

```text
agent_skill({ name: "use-unreal-python" })
```

The Python Skill owns general capability selection, API discovery,
idempotency, continuation recovery, persistence, and verification. PIE tasks
load both Skills; the PIE Skill adds only play-session lifecycle and Game World
semantics.

The result begins with `SKILL.md`, followed by its Markdown references in
deterministic relative-path order. Each text block names its source-relative
file. The returned guidance is not SAL Result Text.

`name` must exactly match a resident name. Unknown names, empty names, and
additional arguments are invalid.

## Version and Packaging

Resident Skills share the Client product version. Replacing the complete
Loomle plugin updates the Client and its Skills together, so there is no
separate Skill install, update, or uninstall lifecycle.

Resident workflows currently cover safe Unreal Python fallback, PIE debugging,
and Blueprint Graph formatting.
