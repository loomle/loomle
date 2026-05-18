---
layout: default
title: Node-Local Edits
parent: Blueprint
nav_order: 3
---

# Blueprint Node-Local Edits

Some Blueprint nodes own local structure that is not a graph-level edit and not
a Blueprint member signature.

Examples:

- Switch cases.
- Sequence pins.
- Select option pins.
- Format Text arguments.
- SetFieldsInStruct field visibility.

Use `blueprint.node.inspect` first. It returns current pins, node-local state,
and `editCapabilities`.

Then call `blueprint.node.edit`. Use `schema.inspect` for the selected
operation schema.

## Boundary

Use `blueprint.node.edit` only for structure owned by one existing node.

Use `blueprint.graph.edit` for links, pin defaults, comments, enabled state,
node creation, movement, and removal.

Use `blueprint.member.edit` for Blueprint-owned member signatures.
