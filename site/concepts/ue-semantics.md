---
layout: default
title: UE Semantics
parent: Concepts
nav_order: 2
---

# UE Semantics

LOOMLE should expose UE behavior, not a parallel model of UE.

Examples:

- Blueprint node creation flows through `blueprint.palette`, which reflects
  UE's Blueprint Action Menu.
- Blueprint node-local edits, such as switch cases or Format Text arguments,
  use `blueprint.node.inspect` and `blueprint.node.edit`.
- Material expression creation uses `material.palette`.
- PCG graph node creation uses `pcg.palette`, and graph parameters are separate
  from graph nodes.
- Widget creation uses `widget.palette`, and hierarchy edits operate on the
  UMG WidgetTree.

When in doubt, inspect first and prefer the tool that matches the UE editor
concept.
