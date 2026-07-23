---
layout: default
title: Project Binding
parent: MCP Calls
nav_order: 1
description: Inspect projects and bind one Loomle MCP session to one stable Unreal project.
---

# Project Binding

One Loomle MCP process is one session, and one session operates on one Unreal
project.

## Inspect

Call without a selector:

```text
project({})
```

The result reports the current binding and known project candidates, including
whether each project is ready, unresponsive, or offline.

## Bind or Switch

Bind by the returned stable project identity:

```text
project({ projectId: "<returned-project-id>" })
```

An exact project root is also accepted:

```text
project({ projectRoot: "/absolute/path/to/project" })
```

An invalid selector leaves the previous binding unchanged.

## Sticky Project Intent

Once selected, the project binding remains session-local and sticky:

- an Editor restart preserves the project intent;
- an offline project may remain bound;
- later UE-backed calls become available when its Editor is healthy again; and
- Loomle never falls through to a different online project.

If one project has multiple healthy Editors, Loomle requires an unambiguous
runtime choice rather than guessing.

## Automatic Binding

Loomle may bind automatically when deployment context, MCP Roots, the Client
working directory, or the sole known project identifies exactly one project.
If no step is unambiguous, the session remains unbound and the agent calls
`project({})`.

## Local Schema Still Works

`sal_schema` reads the Client's local interface catalog and remains available
without a bound or online project. `sal_query`, `sal_patch`, and
`editor_context` require the bound project to resolve to one healthy Editor
runtime.

Project binding selects where UE work runs. It does not create hidden SAL
aliases or retarget the complete locators inside Query and Patch Text.
