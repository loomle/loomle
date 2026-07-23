---
layout: default
title: SAL Schema
parent: MCP Calls
nav_order: 3
description: List active SAL interface modules or read one static UE interface card.
---

# SAL Schema

`sal_schema` reads static resources embedded in the self-contained Client. It
does not require a project binding or an online Unreal Editor.

## Active Module Index

```text
sal_schema({})
```

The result lists only modules supported by the installed Client and Bridge
contract. Loomle 0.7 currently defines:

```text
asset
blueprint
class
graph
state_tree
widget
```

## One Static Card

```text
sal_schema({ module: "graph" })
```

The result describes target rules, query operations and clauses, relationships,
Palette behavior, Patch operations, finalization, and domain boundaries.

## Resident Guide

The compact SAL guide appears once in the `sal_schema` tool description. A
normal call returns only the active module index or requested static card; it
does not repeat the guide in every response.

## Static Is Not Dynamic

`sal_schema` does not inspect one concrete UE object. Query an exact subject
with `with schema` when fields, constraints, or operations depend on the
resolved object and current editor context.

See [Schema Discovery](../concepts/schema.html) for the complete three-layer
model.
