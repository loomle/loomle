---
layout: default
title: MCP Calls
nav_order: 5
has_children: true
description: The eight public Loomle MCP tools and the boundary each one owns.
permalink: /calls/
---

# MCP Calls

The Loomle Client exposes eight public MCP tools. Client status, project
selection, Editor presentation, resident Skills, and the Unreal Python fallback
remain focused support calls; structured authored UE-domain reads and mutations
flow through three core SAL calls rather than hundreds of action-specific MCP
tools.

| Call | Input | Responsibility |
| --- | --- | --- |
| [`status`](status.html) | empty | Inspect Client/update status and bound session/Bridge health. |
| `project` | empty, `projectId`, or `projectRoot` | Inspect, bind, or switch the session's project. |
| `sal_query` | one `text` value | Parse, validate, execute, and format Query Text. |
| `sal_patch` | one `text` value | Parse, validate, execute, and format ordered Patch Text. |
| `sal_schema` | empty or one `module` | Return the active module index or one static interface card. |
| [`agent_skill`](agent-skill.html) | empty or one `name` | Discover or load resident Loomle workflow guidance. |
| [`editor`](editor.html) | empty, `context`, `open`, or `close` | Read context or idempotently open, focus, and close exact Blueprint presentations. |
| [`python`](python.html) | `run` with one script, or the exact returned `poll` continuation | Use Unreal Editor Python only when no structured Loomle interface covers the required capability. |

These are the complete public Client surface. New structured UObject behavior
belongs in SAL and its interface cards; transient Blueprint presentation
belongs to `editor`. `python` is an explicit fallback with no SAL dry run,
rollback, safe cancellation, or idempotency guarantees.

## Calls and Interfaces Are Different

The eight MCP calls describe transport, session, resident workflow, and
fallback boundaries. The nine active interface modules—Asset, Blueprint,
Class, Graph, StateTree, Widget, Level, PCG, and PCG Component—describe the UE
objects and operations carried through SAL. Level and PCG accept authored
`sal_patch` statements with a terminal `save`; PCG Component is Query-only and
is not accepted by `sal_patch` in this release.

`sal_schema` connects the call and interface layers by exposing the active
interface catalog. `agent_skill` supplies workflow policy for composing the
same calls without adding UE capabilities or a separate host installation.

## Result Model

Query, Patch, and Editor share validated canonical SAL Result Text. Its
`objects` section contains ordered Object Text when objects exist; otherwise it
ends with `no_objects`. Editor open/close outcomes, mutation metadata, and
diagnostics appear as later independent MCP text blocks formatted as SAL
comments, never appended to the canonical Result Text block.

Start with [Status](status.html) and [Project Binding](project.html), then read
[SAL Query and Patch](sal.html). Use the [Python fallback](python.html) only
when the installed SAL interfaces do not cover the required operation.
