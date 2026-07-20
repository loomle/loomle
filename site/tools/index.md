---
layout: default
title: Interfaces
nav_order: 5
has_children: true
description: Loomle 0.7 MCP calls and current SAL interface modules.
permalink: /tools/
---

# Interfaces

Loomle has a deliberately small MCP surface. Rich UE behavior is expressed in
SAL Text and discovered through interface cards rather than registered as
hundreds of independent tools.

## MCP Calls

| Call | Purpose |
| --- | --- |
| `sal_query({ text })` | Read one target using self-contained SAL Query Text. |
| `sal_patch({ text })` | Dry-run or apply one ordered SAL Patch Text. |
| `sal_schema({})` | Return the active interface-module index. |
| `sal_schema({ module })` | Return one static interface card. |
| `editor_context({})` | Read the user's current UE interaction target as SAL Object Text. |

`sal_schema` is one MCP tool with two argument forms. Its tool description
contains the resident SAL guide exactly once, so agents receive the basic
language model without repeating it across every tool.

## Current Modules

- [Asset](asset.html): search the UE Asset Registry and obtain exact paths.
- [Blueprint](blueprint/): inspect and edit declarations, Graph lifecycle,
  Components, Class Settings, compile, and save.
- [Class](blueprint/class.html): inspect reflection and effective Defaults;
  edit durable Blueprint Generated Class Defaults.
- [Graph](blueprint/graph.html): inspect and edit Nodes, Pins, Edges, flow,
  Palette-backed creation, stored layout, explicit movement, and native graph
  health.
- [Widget](widget/): inspect and edit authored Widget trees and Slot state.

Use `sal_schema({ module: "<module>" })` as the authoritative current contract.
The website provides orientation and examples; the Client's interface card is
the copy that matches its installed Bridge.

## Common Query Shape

```text
query <bound-target>
<one primary operation>
[where <condition>]
[with <detail>]
[order by <field> asc|desc]
[page limit <count>]
[page after "<cursor>"]
```

Every Query and Patch is self-contained. Existing nested UE objects use typed
stable references such as `graph@id`, `node@id`, `pin@id`, and `widget@id`
inside a complete owner binding.

## Common Patch Shape

```text
patch <bound-target> [dry run]
<ordered binding or operation>
<ordered binding or operation>
```

Core operations include `add`, `remove`, `set`, `reset`, `move`, `invoke`, and
`save`. Modules add operations such as Graph `connect` and Widget `wrap`.
Creation always starts from a Palette result; exact `with schema` gives current
fields, constraints, and invokable UE operations.

## Diagnostics

Results remain valid ordered Object Text. Compiler messages, object health,
pagination guidance, dry-run plans, and failures are returned as adjacent SAL
comments rather than a second response language. See [Diagnostics](diagnostics.html).
