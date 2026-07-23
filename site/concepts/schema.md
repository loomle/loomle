---
layout: default
title: Schema Discovery
parent: Core Concepts
nav_order: 3
description: Use resident, static, and dynamic schema layers without front-loading the entire UE surface.
---

# Schema Discovery

Loomle discovers capability in three layers. Load only the layer needed for
the current decision.

## 1. Resident Guide

The `sal_schema` MCP tool description carries the compact resident SAL guide.
It explains bindings, Query and Patch shapes, Object Text, references,
creation, and diagnostics once.

An agent does not need to request this guide before ordinary work.

## 2. Static Interface Cards

List the active modules:

```text
sal_schema({})
```

Load one domain contract:

```text
sal_schema({ module: "state_tree" })
```

A static card describes:

- target and identity rules;
- query operations and clauses;
- Object Text relationships;
- Palette and creation rules;
- Patch operations;
- compile, save, and handoff boundaries; and
- features intentionally outside the interface.

Static cards do not load UE objects and do not describe the runtime state of
one concrete asset.

## 3. Dynamic Exact Schema

Use `with schema` on an exact subject:

```sal
query eventGraph
node@node-guid
with schema
```

Or inspect an exact Palette capability:

```sal
query eventGraph
palette @palette-entry-id
with schema
```

The result reports fields, constraints, direct Patch statements, available UE
operations, parameters, outputs, and copyable templates for that resolved
context.

Dynamic schema is intentionally unavailable on broad summaries and ambiguous
collections. First resolve an exact object or Palette entry.

## Which Layer Should I Use?

| Question | Discovery layer |
| --- | --- |
| How does SAL generally work? | Resident guide |
| Which operations does Graph support? | Static `graph` card |
| Can this exact Node add a dynamic Pin now? | Exact Node `with schema` |
| Which constructor creates this capability here? | Exact Palette entry |
| Which field or destination is writable? | Exact object schema |

## Diagnostics Close the Loop

Useful failures should point to the next discovery action:

- unknown syntax → load the relevant static interface card;
- unknown field or operation → query the exact subject `with schema`;
- stale id → repeat the relevant summary, collection, or tree query;
- unavailable capability → follow the returned reason and next-query guidance.
