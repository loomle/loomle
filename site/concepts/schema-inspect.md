---
layout: default
title: Schema Inspect
parent: Concepts
nav_order: 3
---

# Schema Inspect

Some LOOMLE tools intentionally keep their `tools/list` schema small. Those
tools expose a stable first-level envelope, then use `schema.inspect` for
operation-specific details.

Use `schema.inspect` when a tool description says so, especially for:

- `blueprint.graph.edit`
- `blueprint.member.edit`
- `blueprint.node.edit`
- `material.graph.edit`
- `pcg.graph.edit`
- `pcg.parameter.edit`
- `widget.tree.edit`

Tools with simple, complete first-level schemas do not need this second layer.
