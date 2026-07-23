---
layout: default
title: Core Concepts
nav_order: 4
has_children: true
description: The SAL language, identity, schema discovery, and mutation model behind every Loomle workflow.
permalink: /concepts/
---

# Core Concepts

Loomle gives structured Unreal Editor objects a text workflow without
replacing UE's own object model. Four ideas explain nearly every request.

## SAL Is the Shared Language

`sal_query` and `sal_patch` accept self-contained SAL Text and return ordered
SAL Object Text. Bindings, objects, fields, relationships, and diagnostics stay
readable in one format.

[Learn the SAL working model](sal.html)

## Identity Is Scoped

An Asset Path locates an asset. Typed ids such as `graph@id`, `node@id`, and
`widget@id` select stable contained objects inside a complete owner chain.
Local aliases make one request readable but do not persist into another
request.

[Understand targets and stable references](identity.html)

## Schema Is Discovered in Layers

The resident guide gives the minimum language model. Static interface cards
describe one UE domain. Exact `with schema` reads expose the fields,
constraints, and operations available for one resolved object or Palette
capability.

[Use schema discovery](schema.html)

## Mutation Is Explicit

A Patch is ordered and self-contained. Dry run uses the real validation and
planning path before any live mutation. Authored edits, readback, compile, and
save remain explicit steps so an agent can verify what happened.

[Understand dry run and finalization](mutations.html)

## The Complete Loop

```text
project binding
  → locate the UE target
  → query a compact view
  → discover exact capability
  → dry-run the complete Patch
  → apply the same authored Patch
  → read back the result
  → compile and save through the owner
```

The [Quickstart](../quickstart.html) demonstrates this loop end to end.
