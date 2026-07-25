---
layout: default
title: SAL Query and Patch
parent: MCP Calls
nav_order: 3
description: Execute self-contained SAL Query Text and ordered Patch Text.
---

# SAL Query and Patch

`sal_query` and `sal_patch` are the two UE-domain execution calls.

## `sal_query`

Pass one self-contained Query Text:

```text
sal_query({
  text: "door = target {\n  domain: blueprint,\n  asset: \"/Game/BP_Door.BP_Door\"\n}\n\nquery door\nsummary"
})
```

The Client parses and validates the text, sends its normalized form to the
bound Bridge, validates the result, and formats canonical SAL Result Text.

A Query reads. It must not repair, compile, dirty, or save the selected object
unless an interface explicitly documents a read with different native
semantics.

## `sal_patch`

Pass one self-contained Patch Text:

```text
sal_patch({
  text: "door = target {\n  domain: blueprint,\n  asset: \"/Game/BP_Door.BP_Door\",\n  id: \"11111111-1111-1111-1111-111111111111\"\n}\n\npatch door dry run\nset door.BlueprintDescription = \"Interactive door\""
})
```

The Patch header owns dry-run state. There is no parallel MCP `dryRun`
argument.

A Patch is ordered. Bindings and operations execute in their written order
after the complete request passes parsing, resolution, validation, and
planning.

## One Request, One Domain

Every request binds one active flat Target and therefore operates in one
Domain. Cross-Domain work uses a returned independent related Target and an
explicit handoff, followed by a new request. For example, Blueprint
declarations and Widget-tree edits cannot be mixed into one Patch.

## Text Results

Both calls return a first MCP text block containing only canonical Result Text:

- `result exact_target` with a canonical Target after the Target opens;
- `result domain_root` for the Asset collection root; or
- `result unresolved_target` when no Target opens.

An `objects` marker begins ordered Object Text. When no Object Text exists,
`no_objects` is the final line. Mutation metadata and diagnostics appear only
in later independent MCP text blocks formatted as SAL comments.

See [SAL Working Model](../concepts/sal.html) and
[Mutations and Finalization](../concepts/mutations.html).
