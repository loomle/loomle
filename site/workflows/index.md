---
layout: default
title: Workflows
nav_order: 7
has_children: true
description: Task-oriented Loomle workflows from project binding through readback and finalization.
permalink: /workflows/
---

# Workflows

Loomle workflows follow one inspect-and-verify loop:

1. Bind one project with `project`.
2. Locate the real UE target with `editor_context` or an exact Asset Path.
3. Read a compact summary, collection, tree, context, or flow.
4. Load a static interface card or exact `with schema` only when needed.
5. Search the target Palette before creating a UE object.
6. Send one complete `sal_patch` with `dry run`.
7. Apply the same authored Patch after validation.
8. Read back the affected state.
9. Compile and save through the exact owning asset.

The examples use placeholder ids because real ids and Palette capabilities must
come from the current project. Copy returned owner bindings, typed references,
constructors, and invocation templates; do not invent them.

## Available Workflows

- [Add a Blueprint Node](blueprint-add-node.html) follows Graph discovery,
  Palette creation, dry run, apply, and Blueprint finalization.
- [Edit Blueprint Switch Cases](blueprint-edit-switch-cases.html) uses exact
  Node schema for a dynamic UE operation.
- [Add Widget Text](widget-add-text.html) edits an authored Widget tree and
  finalizes the owning WidgetBlueprint.

## Foundation Guides

Use these guides before a domain workflow when the underlying step is
unfamiliar:

- [Project Binding](../calls/project.html)
- [Editor Context](../calls/editor-context.html)
- [SAL Working Model](../concepts/sal.html)
- [Targets and Stable References](../concepts/identity.html)
- [Schema Discovery](../concepts/schema.html)
- [Mutations and Finalization](../concepts/mutations.html)

Additional workflows for factual reference queries, StateTree editing, and
diagnostic recovery will follow this same model.
