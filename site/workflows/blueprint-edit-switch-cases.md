---
layout: default
title: Edit Blueprint Switch Cases
parent: Workflows
nav_order: 2
---

# Edit Blueprint Switch Cases

Recommended sequence:

1. `blueprint.graph.inspect` with `view: "overview"` to find the switch node.
2. If the node has `hasNodeEditCapabilities: true`, call
   `blueprint.node.inspect`.
3. Read `editCapabilities` and current case pins.
4. `schema.inspect` for the needed `blueprint.node.edit` operation.
5. `blueprint.node.edit` to add, remove, or rename the case.
6. `blueprint.compile`.

Use `blueprint.node.edit` for cases. Use `blueprint.graph.edit` for links and
pin defaults around the node.
