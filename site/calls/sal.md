---
layout: default
title: SAL Query and Patch
parent: MCP Calls
nav_order: 2
description: Execute self-contained SAL Query Text and ordered Patch Text.
---

# SAL Query and Patch

`sal_query` and `sal_patch` are the two UE-domain execution calls.

## `sal_query`

Pass one self-contained Query Text:

```text
sal_query({
  text: "door = blueprint(asset: \"/Game/BP_Door.BP_Door\")\n\nquery door\nsummary"
})
```

The Client parses and validates the text, sends its normalized form to the
bound Bridge, validates the result, and formats ordered SAL Object Text.

A Query reads. It must not repair, compile, dirty, or save the selected object
unless an interface explicitly documents a read with different native
semantics.

## `sal_patch`

Pass one self-contained Patch Text:

```text
sal_patch({
  text: "door = blueprint(asset: \"/Game/BP_Door.BP_Door\", id: \"blueprint-guid\")\n\npatch door dry run\nset door.BlueprintDescription = \"Interactive door\""
})
```

The Patch header owns dry-run state. There is no parallel MCP `dryRun`
argument.

A Patch is ordered. Bindings and operations execute in their written order
after the complete request passes parsing, resolution, validation, and
planning.

## One Request, One Planner

Composed targets can expose several query interfaces, but one authored Patch is
owned atomically by one interface planner. For example, Blueprint declarations
and Widget-tree edits use following requests rather than one mixed Patch.

## Text Results

Both calls return ordinary Object Text with adjacent comments for:

- diagnostics;
- pagination;
- resolved identities;
- dry-run and apply state;
- planned operations and effects;
- diffs; and
- compiler or object health.

See [SAL Working Model](../concepts/sal.html) and
[Mutations and Finalization](../concepts/mutations.html).
