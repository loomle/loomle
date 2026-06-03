---
layout: default
title: Blueprint
parent: Tools
nav_order: 5
has_children: true
---

# Blueprint Tools

Blueprint tools follow UE's Blueprint boundaries. Pick the tool by the thing you
are changing:

- Asset and class contract: `blueprint_inspect`, `blueprint_class_inspect`,
  `blueprint_class_edit`.
- Members: variables, functions, macros, dispatchers, custom events, and
  components use `blueprint_member_inspect` and `blueprint_member_edit`.
- Graphs: nodes, pins, links, defaults, comments, and layout use
  `blueprint_graph_*`.
- Node-local structure: switch cases, sequence pins, Select options, Format
  Text arguments, and struct field visibility use `blueprint_node_*`.
- Creation: graph node creation starts with `blueprint_graph_palette`.
- Verification: compile with `blueprint_compile`.

Use `blueprint_graph_palette` before graph node creation. Use
`blueprint_node_inspect` when `blueprint_graph_inspect` returns
`hasNodeEditCapabilities: true`.

## Tool List

- `blueprint_inspect`: inspect a Blueprint asset and class-level contract.
- `blueprint_class_inspect`: inspect parent class and implemented interfaces.
- `blueprint_class_edit`: edit parent class and implemented interfaces.
- `blueprint_member_inspect`: inspect Blueprint-owned members.
- `blueprint_member_edit`: edit Blueprint-owned members.
- `blueprint_graph_list`: list graphs in a Blueprint asset.
- `blueprint_graph_inspect`: inspect graph nodes, pins, links, and views.
- `blueprint_graph_edit`: apply explicit local graph edit commands.
- `blueprint_graph_layout`: format selected graph regions.
- `blueprint_node_inspect`: inspect one node's node-local state and capabilities.
- `blueprint_node_edit`: edit node-local structure.
- `blueprint_graph_palette`: search UE Blueprint Action Menu entries.
- `blueprint_compile`: compile a Blueprint asset.

## API Pages

| Area | Tools | Page |
| --- | --- | --- |
| Palette creation | `blueprint_graph_palette` | [Blueprint Palette](palette.html) |
| Graphs | `blueprint_graph_list`, `blueprint_graph_inspect`, `blueprint_graph_edit`, `blueprint_graph_layout` | [Blueprint Graphs](graph.html) |
| Node-local structure | `blueprint_node_inspect`, `blueprint_node_edit` | [Node-Local Edits](node-local.html) |
| Members | `blueprint_member_inspect`, `blueprint_member_edit` | [Blueprint Members](members.html) |
| Class contract | `blueprint_inspect`, `blueprint_class_inspect`, `blueprint_class_edit` | [Blueprint Class](class.html) |
| Compile | `blueprint_compile` | [Blueprint Compile](compile.html) |

## Recommended Flow

1. Inspect the asset or graph.
2. Use `blueprint_graph_palette` when creating graph nodes.
3. Use `schema_inspect` when an edit tool has operation-specific arguments.
4. Apply one explicit edit.
5. Compile if behavior changed.
