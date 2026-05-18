---
layout: default
title: Project Model
parent: Concepts
nav_order: 1
---

# Project Model

LOOMLE has two installation scopes:

- Global install: the `loomle` command, active version state, release payloads,
  plugin cache, and runtime/project registry under `~/.loomle`.
- Project install: the Unreal project plugin under `Plugins/LoomleBridge` plus
  required project support settings.

The global install is not tied to one Unreal project. When Unreal Editor starts
with `LoomleBridge` loaded, the project reports a runtime endpoint. Call
`project.list` and `project.attach` so the current MCP session has a single
active UE project.

When a project needs support installed or updated, call `project.install` while
that project's Unreal Editor instance is closed. `project.install` copies the
active global plugin cache into that project; it does not install a second
global LOOMLE client inside the project.
