---
layout: default
title: Project Model
parent: Concepts
nav_order: 1
---

# Project Model

LOOMLE has two installation scopes:

- Global install: the `loomle` command and cached release assets.
- Project install: the Unreal project plugin under `Plugins/LoomleBridge`.

The global MCP process does not automatically choose a project. Call
`project.list` and `project.attach` so the session has a single active UE
project.

When a project needs support installed or updated, call `project.install` while
that project's Unreal Editor instance is closed.
