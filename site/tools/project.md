---
layout: default
title: Project and Session
parent: Tools
nav_order: 0
---

# Project and Session Tools

Project tools manage the MCP session's connection to Unreal Editor.

## `loomle`

Reports LOOMLE bridge health and runtime status.

Use this first when a session appears detached or a UE tool is unavailable.

## `project.list`

Lists Unreal Engine projects known to LOOMLE.

By default, use it to find online projects that can be attached to the current
MCP session.

## `project.attach`

Attaches the current MCP session to one online LOOMLE-enabled Unreal project.

After attach succeeds, the UE-facing tools become usable for that project.

## `project.install`

Installs or updates LOOMLE project support for an Unreal project.

It copies the cached `LoomleBridge` plugin into the project's
`Plugins/LoomleBridge/` directory and updates required project support files.

Close the Unreal Editor for that project before calling `project.install`.

## `schema.inspect`

Returns documented second-level operation schemas for tools that intentionally
keep their first-level `tools/list` schema compact.

Use it when a tool description says operation-specific arguments are omitted
from `tools/list`.
