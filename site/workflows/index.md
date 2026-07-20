---
layout: default
title: Workflows
nav_order: 6
has_children: true
description: Current SAL inspect, discover, dry-run, apply, and finalize workflows.
---

# Workflows

Loomle workflows follow one stable sequence:

1. Locate the real UE target with `editor_context` or an exact Asset Path.
2. Read a compact `summary`, collection, tree, context, or flow.
3. Load one static interface card or exact `with schema` only when needed.
4. Search Palette before creating any UE object.
5. Send one self-contained `sal_patch` with `dry run`.
6. Apply the same authored Patch after validation.
7. Finalize through the exact owner as its interface requires. Blueprint edits
   use a separate Blueprint `compile`/`save` request.

The examples use placeholder ids because real ids and Palette entries must come
from the current project. Copy returned bindings and invocation templates; do
not invent them.
