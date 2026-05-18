---
layout: home
title: LOOMLE
nav_order: 1
description: Agent-native Unreal Engine tooling for Blueprint, Material, PCG, and Widget workflows.
permalink: /
---

# LOOMLE
{: .fs-9 }

Agent-native Unreal Engine tooling for Blueprint, Material, PCG, and Widget workflows.
{: .fs-6 .fw-300 }

[Install LOOMLE](install.html){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Start with the Quickstart](quickstart.html){: .btn .fs-5 .mb-4 .mb-md-0 }

LOOMLE helps coding agents operate Unreal Engine through clear MCP tools that
match UE semantics. It does not ask agents to guess internal node classes,
invent graph transformations, or treat UE assets as generic JSON.

## What LOOMLE Provides

- A global `loomle mcp` command for Codex, Claude, and other MCP hosts.
- A UE editor bridge installed into each project as `Plugins/LoomleBridge`.
- UE-semantic tools for assets, Blueprint, Material, PCG, UMG widgets, editor
  focus, diagnostics, logs, and play sessions.
- Palette-driven creation so agents use UE's own creation model instead of
  guessing classes.
- Compact first-level schemas with `schema.inspect` for detailed operation
  schemas when a tool intentionally has a second layer.

## Core Principle

LOOMLE exposes Unreal Engine in terms that agents can use reliably, while
staying faithful to UE's own concepts and execution paths.

Start with [Install](install.html), then follow the [Quickstart](quickstart.html).
