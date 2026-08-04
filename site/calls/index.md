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
selection, and Editor presentation are separate calls; rich authored UE-domain
reads and mutations flow through SAL.

| Call | Input | Responsibility |
| --- | --- | --- |
| [`status`](status.html) | empty | Inspect Client/update status and bound session/Bridge health. |
| `project` | empty, `projectId`, or `projectRoot` | Inspect, bind, or switch the session's project. |
| `sal_query` | one `text` value | Parse, validate, execute, and format Query Text. |
| `sal_patch` | one `text` value | Parse, validate, execute, and format ordered Patch Text. |
| `sal_schema` | empty or one `module` | Return the active module index or one static interface card. |
| [`agent_skill`](agent-skill.html) | empty or one `name` | Discover or load resident Loomle workflow guidance. |
| [`editor`](editor-context.html) | empty, `context`, `open`, or `close` | Read context or idempotently open, focus, and close exact Blueprint presentations. |
| `editor_context` | empty | Compatibility alias for `editor({})`. |

These are the complete public Client surface. Authored UObject behavior belongs
in SAL and its interface cards; transient Blueprint presentation belongs to
`editor`.

## Calls and Interfaces Are Different

The eight MCP calls describe transport, session, and resident workflow
boundaries. The six active
interface modules—Asset, Blueprint, Class, Graph, StateTree, and Widget—describe
the UE objects and operations carried through `sal_query` and `sal_patch`.

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
[SAL Query and Patch](sal.html).
