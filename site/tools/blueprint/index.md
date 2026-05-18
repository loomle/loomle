---
layout: default
title: Blueprint
parent: Tools
nav_order: 1
has_children: true
---

# Blueprint Tools

Blueprint tools are organized around UE's Blueprint semantics:

- Class contract: `blueprint.class.inspect`, `blueprint.class.edit`.
- Members: `blueprint.member.inspect`, `blueprint.member.edit`.
- Graphs: `blueprint.graph.list`, `blueprint.graph.inspect`,
  `blueprint.graph.edit`, `blueprint.graph.layout`.
- Node-local structure: `blueprint.node.inspect`, `blueprint.node.edit`.
- Creation: `blueprint.palette`.
- Verification: `blueprint.compile`.

Use `blueprint.palette` before graph node creation. Use
`blueprint.node.inspect` when `blueprint.graph.inspect` returns
`hasNodeEditCapabilities: true`.
