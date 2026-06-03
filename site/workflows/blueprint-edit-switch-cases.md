---
layout: default
title: Edit Blueprint Switch Cases
parent: Workflows
nav_order: 2
---

# Edit Blueprint Switch Cases

Recommended sequence:

1. `blueprint_graph_inspect` with `view: "overview"` to find the switch node.
2. If the node has `hasNodeEditCapabilities: true`, call
   `blueprint_node_inspect`.
3. Read `editCapabilities` and current case pins.
4. `schema_inspect` for the needed `blueprint_node_edit` operation.
5. `blueprint_node_edit` to add, remove, or rename the case.
6. `blueprint_compile`.

Use `blueprint_node_edit` for cases. Use `blueprint_graph_edit` for links and
pin defaults around the node.
