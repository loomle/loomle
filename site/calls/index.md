---
layout: default
title: MCP Calls
nav_order: 5
has_children: true
description: The six public Loomle MCP tools and the boundary each one owns.
permalink: /calls/
---

# MCP Calls

The Loomle Client exposes six public MCP tools. Client status, project
selection, and editor
context are separate calls; all rich UE-domain reads and mutations flow through
SAL.

| Call | Input | Responsibility |
| --- | --- | --- |
| [`status`](status.html) | empty | Inspect Client/update status and bound session/Bridge health. |
| `project` | empty, `projectId`, or `projectRoot` | Inspect, bind, or switch the session's project. |
| `sal_query` | one `text` value | Parse, validate, execute, and format Query Text. |
| `sal_patch` | one `text` value | Parse, validate, execute, and format ordered Patch Text. |
| `sal_schema` | empty or one `module` | Return the active module index or one static interface card. |
| `editor_context` | empty | Read the current UE interaction target as Object Text. |

These are the complete public Client surface. New UE behavior belongs in SAL
and its interface cards rather than in parallel compatibility tools.

## Calls and Interfaces Are Different

The six MCP calls describe transport and session boundaries. The six active
interface modules—Asset, Blueprint, Class, Graph, StateTree, and Widget—describe
the UE objects and operations carried through `sal_query` and `sal_patch`.

`sal_schema` connects the two layers by exposing the active interface catalog.

## Result Model

Query, Patch, and Editor Context results share validated ordered SAL Object
Text. Diagnostics, mutation state, compiler output, and navigation guidance
appear as adjacent SAL comments rather than a second JSON result language.

Start with [Status](status.html) and [Project Binding](project.html), then read
[SAL Query and Patch](sal.html).
