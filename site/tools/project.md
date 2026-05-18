---
layout: default
title: Project and Session
parent: Tools
nav_order: 1
---

# Project and Session Tools

Project tools manage the MCP session's connection to Unreal Editor.

## Tool List

- `loomle.status`: reports whether the MCP session is attached.
- `project.list`: lists known Unreal projects.
- `project.attach`: attaches this MCP session to one online project.
- `project.install`: installs or updates project-local LOOMLE support.
- `schema.inspect`: returns documented second-level operation schemas.
- `loomle`: reports bridge health after attach.
- `context`: reads active editor context and selection after attach.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `loomle.status` | none | No arguments. |
| `project.list` | none | `status: online/offline/all`, `includeDiagnostics` |
| `project.attach` | `projectId` or `projectRoot` | Attach target from `project.list`. |
| `project.install` | `projectRoot` | `force` |
| `schema.inspect` | `domain`, `tool` | `domain: blueprint/material/pcg/widget`, optional `operation`, optional `include` |
| `loomle` | none | No arguments. |
| `context` | none | Optional `resolveIds`, `resolveFields`. |

## `loomle`

Reports LOOMLE bridge health and runtime status.

Use this first when a session appears detached or a UE tool is unavailable.

## `project.list`

Lists Unreal Engine projects known to LOOMLE.

Known projects come from the global LOOMLE state directory and from Unreal
Editor runtimes that are currently reporting online. By default, use it to find
online projects that can be attached to the current MCP session.

## `project.attach`

Attaches the current MCP session to one online LOOMLE-enabled Unreal project.

After attach succeeds, the UE-facing tools become usable for that project.

## `project.install`

Installs or updates LOOMLE project support for an Unreal project.

It copies the cached `LoomleBridge` plugin into the project's
`Plugins/LoomleBridge/` directory, records the project in global LOOMLE state,
and updates required project support settings.

Close the Unreal Editor for that project before calling `project.install`.

## `schema.inspect`

Returns documented second-level operation schemas for tools that intentionally
keep their first-level `tools/list` schema compact.

Use it when a tool description says operation-specific arguments are omitted
from `tools/list`.
